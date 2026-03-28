/*
 * BPI-OS Compositor - wlroots 기반 모바일 Wayland 컴포지터
 *
 * 풀스크린 앱 스택, 터치 제스처, 상태바를 지원하는
 * 모바일 전용 Wayland 컴포지터
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

/* ─── 모바일 제스처 상수 ─── */
#define SWIPE_THRESHOLD_PX    50
#define SWIPE_FROM_BOTTOM_PX  40    /* 하단 스와이프 영역 */
#define SWIPE_FROM_TOP_PX     40    /* 상단 스와이프 영역 */
#define STATUSBAR_HEIGHT_PX   36

/* ─── 제스처 방향 ─── */
enum gesture_direction {
    GESTURE_NONE,
    GESTURE_SWIPE_UP,      /* 홈으로 가기 */
    GESTURE_SWIPE_DOWN,    /* 알림 패널 */
    GESTURE_SWIPE_LEFT,    /* 뒤로가기 */
    GESTURE_SWIPE_RIGHT,   /* 앱 전환 */
};

/* ─── 앱 뷰 (풀스크린 스택) ─── */
struct bpi_view {
    struct wl_list link;
    struct bpi_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;

    /* 이벤트 리스너 */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_fullscreen;
};

/* ─── 터치 제스처 추적 ─── */
struct touch_state {
    bool active;
    double start_x, start_y;
    double current_x, current_y;
    uint32_t start_time_ms;
    enum gesture_direction pending;
};

/* ─── 서버 (메인 상태) ─── */
struct bpi_server {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list views; /* bpi_view::link */

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    /* 터치 입력 */
    struct wl_listener touch_down;
    struct wl_listener touch_up;
    struct wl_listener touch_motion;
    struct touch_state touch;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs; /* bpi_output::link */
    struct wl_listener new_output;

    /* 모바일 상태 */
    struct bpi_view *active_view;
    bool home_screen_visible;
    int screen_width;
    int screen_height;
};

/* ─── 출력 (디스플레이) ─── */
struct bpi_output {
    struct wl_list link;
    struct bpi_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

/* ─── 유틸리티: 현재 시각 (밀리초) ─── */
static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ─── 제스처 판별 ─── */
static enum gesture_direction detect_gesture(struct touch_state *t,
                                             int screen_h) {
    double dx = t->current_x - t->start_x;
    double dy = t->current_y - t->start_y;
    double abs_dx = dx > 0 ? dx : -dx;
    double abs_dy = dy > 0 ? dy : -dy;

    if (abs_dx < SWIPE_THRESHOLD_PX && abs_dy < SWIPE_THRESHOLD_PX)
        return GESTURE_NONE;

    /* 하단에서 위로 스와이프 → 홈 */
    if (t->start_y > screen_h - SWIPE_FROM_BOTTOM_PX && dy < -SWIPE_THRESHOLD_PX)
        return GESTURE_SWIPE_UP;

    /* 상단에서 아래로 스와이프 → 알림 */
    if (t->start_y < SWIPE_FROM_TOP_PX && dy > SWIPE_THRESHOLD_PX)
        return GESTURE_SWIPE_DOWN;

    /* 좌우 스와이프 */
    if (abs_dx > abs_dy) {
        if (dx < -SWIPE_THRESHOLD_PX)
            return GESTURE_SWIPE_LEFT;
        if (dx > SWIPE_THRESHOLD_PX)
            return GESTURE_SWIPE_RIGHT;
    }

    return GESTURE_NONE;
}

/* ─── 뷰를 포커스 + 풀스크린으로 설정 ─── */
static void focus_view(struct bpi_view *view) {
    if (!view) return;
    struct bpi_server *server = view->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface = view->xdg_toplevel->base->surface;

    if (prev_surface == surface) return;

    if (prev_surface) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel)
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
    }

    /* 뷰를 스택 맨 위로 */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);

    wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);

    /* 풀스크린으로 크기 설정 */
    if (server->screen_width > 0 && server->screen_height > 0) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel,
            server->screen_width,
            server->screen_height - STATUSBAR_HEIGHT_PX);
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes,
            &keyboard->modifiers);
    }

    server->active_view = view;
}

/* ─── 홈 화면으로 이동 ─── */
static void go_home(struct bpi_server *server) {
    wlr_log(WLR_INFO, "Gesture: Go Home");
    server->home_screen_visible = true;
    /* 홈스크린 앱(첫 번째 등록된 뷰)을 활성화하거나,
       모든 뷰를 비활성화하여 빈 화면 표시 */
}

/* ─── 앱 전환기 표시 ─── */
static void show_app_switcher(struct bpi_server *server) {
    wlr_log(WLR_INFO, "Gesture: App Switcher");
    /* TODO: 앱 전환 오버레이 표시 */
}

/* ─── 알림 패널 토글 ─── */
static void toggle_notification_panel(struct bpi_server *server) {
    wlr_log(WLR_INFO, "Gesture: Notification Panel");
    /* TODO: 상단 알림 드로어 */
}

/* ─── 뒤로가기 ─── */
static void go_back(struct bpi_server *server) {
    wlr_log(WLR_INFO, "Gesture: Go Back");
    /* TODO: 현재 앱에 뒤로가기 이벤트 전달 */
}

/* ─── 터치 이벤트: 터치 시작 ─── */
static void handle_touch_down(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, touch_down);
    struct wlr_touch_down_event *event = data;

    /* 정규화된 좌표(0~1)를 픽셀로 변환 */
    double abs_x = event->x * server->screen_width;
    double abs_y = event->y * server->screen_height;

    server->touch.active = true;
    server->touch.start_x = abs_x;
    server->touch.start_y = abs_y;
    server->touch.current_x = abs_x;
    server->touch.current_y = abs_y;
    server->touch.start_time_ms = now_ms();
    server->touch.pending = GESTURE_NONE;

    /* 터치를 커서 위치로도 매핑 (앱에 전달) */
    wlr_cursor_warp_absolute(server->cursor, NULL, event->x, event->y);
    wlr_seat_pointer_notify_frame(server->seat);
}

/* ─── 터치 이벤트: 터치 이동 ─── */
static void handle_touch_motion(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, touch_motion);
    struct wlr_touch_motion_event *event = data;

    server->touch.current_x = event->x * server->screen_width;
    server->touch.current_y = event->y * server->screen_height;
}

/* ─── 터치 이벤트: 터치 종료 → 제스처 판정 ─── */
static void handle_touch_up(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, touch_up);

    if (!server->touch.active) return;
    server->touch.active = false;

    enum gesture_direction gesture =
        detect_gesture(&server->touch, server->screen_height);

    switch (gesture) {
    case GESTURE_SWIPE_UP:    go_home(server); break;
    case GESTURE_SWIPE_DOWN:  toggle_notification_panel(server); break;
    case GESTURE_SWIPE_LEFT:  go_back(server); break;
    case GESTURE_SWIPE_RIGHT: show_app_switcher(server); break;
    case GESTURE_NONE:        break;
    }
}

/* ─── XDG Toplevel 이벤트: map (앱 표시) ─── */
static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct bpi_view *view = wl_container_of(listener, view, map);
    wl_list_insert(&view->server->views, &view->link);
    focus_view(view);
}

/* ─── XDG Toplevel 이벤트: unmap (앱 숨김) ─── */
static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct bpi_view *view = wl_container_of(listener, view, unmap);
    if (view == view->server->active_view)
        view->server->active_view = NULL;
    wl_list_remove(&view->link);
}

/* ─── XDG Toplevel 이벤트: destroy (앱 종료) ─── */
static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct bpi_view *view = wl_container_of(listener, view, destroy);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_fullscreen.link);
    free(view);
}

/* ─── XDG Toplevel 이벤트: 풀스크린 요청 ─── */
static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
                                            void *data) {
    struct bpi_view *view =
        wl_container_of(listener, view, request_fullscreen);
    /* 모바일 OS에서 모든 앱은 항상 풀스크린 */
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
}

/* ─── 새 XDG Toplevel (새 앱 윈도우) ─── */
static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    struct bpi_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->xdg_toplevel = toplevel;
    view->scene_tree =
        wlr_scene_xdg_surface_create(&server->scene->tree,
                                     toplevel->base);
    view->scene_tree->node.data = view;
    toplevel->base->data = view->scene_tree;

    /* 풀스크린 강제 */
    wlr_xdg_toplevel_set_fullscreen(toplevel, true);
    if (server->screen_width > 0) {
        wlr_xdg_toplevel_set_size(toplevel,
            server->screen_width,
            server->screen_height - STATUSBAR_HEIGHT_PX);
    }

    view->map.notify = xdg_toplevel_map;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);
    view->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
    view->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&toplevel->events.destroy, &view->destroy);
    view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen,
                  &view->request_fullscreen);
}

/* ─── 새 XDG Popup ─── */
static void handle_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct wlr_xdg_popup *popup = data;
    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (!parent) return;
    struct wlr_scene_tree *parent_tree = parent->data;
    popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, popup->base);
}

/* ─── 출력 이벤트: 프레임 ─── */
static void output_frame(struct wl_listener *listener, void *data) {
    struct bpi_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene = output->server->scene;
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(scene, output->wlr_output);
    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

/* ─── 출력 이벤트: 상태 요청 ─── */
static void output_request_state(struct wl_listener *listener, void *data) {
    struct bpi_output *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

/* ─── 출력 이벤트: 파괴 ─── */
static void output_destroy(struct wl_listener *listener, void *data) {
    struct bpi_output *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

/* ─── 새 출력 (디스플레이 연결) ─── */
static void handle_new_output(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* 기본 모드 설정 */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode)
        wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct bpi_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;

    /* 화면 크기 기록 */
    server->screen_width = wlr_output->width;
    server->screen_height = wlr_output->height;
    wlr_log(WLR_INFO, "Output: %s (%dx%d)",
            wlr_output->name, server->screen_width, server->screen_height);

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
}

/* ─── 키보드 처리 ─── */
static void handle_keyboard_key(struct wl_listener *listener, void *data) {
    /* TODO: 하드웨어 키 매핑 (전원, 볼륨) */
}

/* ─── 새 입력 장치 ─── */
static void handle_new_input(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(device);
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
        wlr_keyboard_set_keymap(keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        wlr_keyboard_set_repeat_info(keyboard, 25, 600);
        wlr_seat_set_keyboard(server->seat, keyboard);
        break;
    }
    case WLR_INPUT_DEVICE_TOUCH:
        wlr_log(WLR_INFO, "Touch device: %s", device->name);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(server->cursor, device);
        break;
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    if (device->type == WLR_INPUT_DEVICE_TOUCH)
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server->seat, caps);
}

/* ─── 커서 이벤트 핸들러 ─── */
static void handle_cursor_motion(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
                                          void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
                             event->x, event->y);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(server->seat,
        event->time_msec, event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        wlr_seat_pointer_notify_frame(server->seat);
    }
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

/* ─── 시트 이벤트 ─── */
static void handle_request_cursor(struct wl_listener *listener, void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused = server->seat->pointer_state.focused_client;
    if (focused == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
                               event->hotspot_x, event->hotspot_y);
    }
}

static void handle_request_set_selection(struct wl_listener *listener,
                                         void *data) {
    struct bpi_server *server =
        wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* ─── 메인 ─── */
int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);
    wlr_log(WLR_INFO, "BPI-OS Compositor starting...");

    struct bpi_server server = {0};
    server.home_screen_visible = true;
    wl_list_init(&server.views);
    wl_list_init(&server.outputs);

    server.wl_display = wl_display_create();

    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.wl_display), NULL);
    if (!server.backend) {
        wlr_log(WLR_ERROR, "Failed to create wlr_backend");
        return 1;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer) {
        wlr_log(WLR_ERROR, "Failed to create wlr_renderer");
        return 1;
    }
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (!server.allocator) {
        wlr_log(WLR_ERROR, "Failed to create wlr_allocator");
        return 1;
    }

    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);

    server.output_layout = wlr_output_layout_create(server.wl_display);
    server.scene = wlr_scene_create();
    server.scene_layout =
        wlr_scene_attach_output_layout(server.scene, server.output_layout);

    server.new_output.notify = handle_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    /* XDG Shell */
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_toplevel.notify = handle_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel,
                  &server.new_xdg_toplevel);
    server.new_xdg_popup.notify = handle_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup,
                  &server.new_xdg_popup);

    /* 커서 */
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

    server.cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute,
                  &server.cursor_motion_absolute);
    server.cursor_button.notify = handle_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    server.cursor_axis.notify = handle_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    server.cursor_frame.notify = handle_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    /* 터치 */
    server.touch_down.notify = handle_touch_down;
    wl_signal_add(&server.cursor->events.touch_down, &server.touch_down);
    server.touch_up.notify = handle_touch_up;
    wl_signal_add(&server.cursor->events.touch_up, &server.touch_up);
    server.touch_motion.notify = handle_touch_motion;
    wl_signal_add(&server.cursor->events.touch_motion, &server.touch_motion);

    /* Seat */
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    server.request_cursor.notify = handle_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor,
                  &server.request_cursor);
    server.request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection,
                  &server.request_set_selection);

    /* 입력 장치 */
    server.new_input.notify = handle_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);

    /* Wayland 소켓 */
    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        wlr_log(WLR_ERROR, "Failed to create Wayland socket");
        wlr_backend_destroy(server.backend);
        return 1;
    }

    if (!wlr_backend_start(server.backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    wlr_log(WLR_INFO,
        "BPI-OS Compositor running on WAYLAND_DISPLAY=%s", socket);

    wl_display_run(server.wl_display);

    wl_display_destroy_clients(server.wl_display);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);

    wlr_log(WLR_INFO, "BPI-OS Compositor shut down cleanly");
    return 0;
}
