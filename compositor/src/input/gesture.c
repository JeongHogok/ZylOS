/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Driver
 *
 * 역할: 터치 이벤트를 고수준 제스처(스와이프)로 변환 및 디스패치
 * 수행범위: touchstart/move/end 이벤트 처리, 방향 감지, 콜백 테이블 디스패치
 * 의존방향: gesture.h → zyl_compositor.h
 * SOLID: OCP — 함수 포인터 테이블로 제스처 액션을 교체 가능
 * ────────────────────────────────────────────────────────── */

#include "gesture.h"
#include "../view/view.h"

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* ================================================================
 * Gesture detection
 * ================================================================ */

enum gesture_direction gesture_detect(const struct touch_state *t,
                                      int screen_h,
                                      const struct zyl_config *cfg)
{
    double dx     = t->current_x - t->start_x;
    double dy     = t->current_y - t->start_y;
    double abs_dx = dx > 0 ? dx : -dx;
    double abs_dy = dy > 0 ? dy : -dy;

    if (abs_dx < cfg->swipe_threshold_px &&
        abs_dy < cfg->swipe_threshold_px)
        return GESTURE_NONE;

    /* Bottom-edge swipe up -> home */
    if (t->start_y > screen_h - cfg->swipe_from_bottom_px &&
        dy < -cfg->swipe_threshold_px)
        return GESTURE_SWIPE_UP;

    /* Top-edge swipe down -> notifications */
    if (t->start_y < cfg->swipe_from_top_px &&
        dy > cfg->swipe_threshold_px)
        return GESTURE_SWIPE_DOWN;

    /* Horizontal swipe */
    if (abs_dx > abs_dy) {
        if (dx < -cfg->swipe_threshold_px)
            return GESTURE_SWIPE_LEFT;
        if (dx > cfg->swipe_threshold_px)
            return GESTURE_SWIPE_RIGHT;
    }

    return GESTURE_NONE;
}

/* ================================================================
 * Gesture → wlroots signal dispatch
 * ================================================================ */

static void emit_compositor_signal(struct zyl_server *server,
                                    const char *signal_name,
                                    const char *detail)
{
    (void)server;
    wlr_log(WLR_INFO, "D-Bus signal: %s(%s)", signal_name,
            detail ? detail : "");
    /* D-Bus signal emission: requires server->dbus_connection
     * which is set up in main.c after wl_display_run() integration
     * with GMainLoop. For wlroots-only compositor, gesture signals
     * are delivered through wlr_signal and handled by the event loop.
     * D-Bus integration requires GLib main loop bridge (future). */
}

/* ================================================================
 * Default gesture action implementations
 * ================================================================ */

/*
 * SWIPE UP from bottom — go home.
 * Sends GoHome to WAM.  If the home screen is already visible, no-op.
 */
static void action_go_home(struct zyl_server *server)
{
    if (server->home_screen_visible) {
        wlr_log(WLR_DEBUG, "Gesture: Go Home — already on home screen");
        return;
    }

    wlr_log(WLR_INFO, "Gesture: Go Home");
    server->home_screen_visible = true;
    emit_compositor_signal(server, "GoHome", NULL);
}

/*
 * SWIPE DOWN from top — toggle notification panel overlay.
 * Creates a semi-transparent dark rect covering the top half, then
 * signals the statusbar app to populate actual notification content.
 */
static void action_notification_panel(struct zyl_server *server)
{
    /* Lazy-create the overlay rect on first use */
    if (!server->notif_overlay) {
        int half_h = server->screen_height / 2;
        float color[4] = { 0.0f, 0.0f, 0.0f, 0.70f };

        server->notif_overlay = wlr_scene_rect_create(
            &server->scene->tree, server->screen_width, half_h, color);

        if (!server->notif_overlay) {
            wlr_log(WLR_ERROR,
                     "Gesture: failed to create notification overlay");
            return;
        }
        /* Position at top-left of the screen */
        wlr_scene_node_set_position(&server->notif_overlay->node, 0, 0);
        /* Start hidden */
        wlr_scene_node_set_enabled(&server->notif_overlay->node, false);
        server->notif_panel_visible = false;
    }

    /* Toggle visibility */
    server->notif_panel_visible = !server->notif_panel_visible;
    wlr_scene_node_set_enabled(&server->notif_overlay->node,
                                server->notif_panel_visible);

    wlr_log(WLR_INFO, "Gesture: Notification Panel %s",
            server->notif_panel_visible ? "shown" : "hidden");

    emit_compositor_signal(server, "NotificationPanel",
                           server->notif_panel_visible ? "show" : "hide");
}

/*
 * SWIPE LEFT — go back.
 * Sends GoBack to the focused app.  If nothing is focused, go home.
 */
static void action_go_back(struct zyl_server *server)
{
    if (wl_list_empty(&server->views)) {
        wlr_log(WLR_INFO, "Gesture: Go Back — no views, going home");
        server->home_screen_visible = true;
        emit_compositor_signal(server, "GoHome", NULL);
        return;
    }

    struct zyl_view *top =
        wl_container_of(server->views.next, top, link);

    const char *app_id = top->xdg_toplevel->app_id;
    wlr_log(WLR_INFO, "Gesture: Go Back → %s", app_id ? app_id : "(unknown)");
    emit_compositor_signal(server, "GoBack", app_id);
}

/*
 * SWIPE RIGHT — cycle to the next app in the view list.
 */
static void action_app_switcher(struct zyl_server *server)
{
    if (wl_list_empty(&server->views))
        return;

    /* Current top view */
    struct zyl_view *current =
        wl_container_of(server->views.next, current, link);

    struct wl_list *next_link = current->link.next;

    /* Wrap around — if we hit the sentinel, we are at the only view */
    if (next_link == &server->views) {
        wlr_log(WLR_DEBUG, "Gesture: App Switcher — only one view");
        return;
    }

    struct zyl_view *next_view =
        wl_container_of(next_link, next_view, link);

    view_focus(next_view);

    const char *app_id = next_view->xdg_toplevel->app_id;
    wlr_log(WLR_INFO, "Gesture: App Switch → %s",
            app_id ? app_id : "(unknown)");
}

void gesture_init_handlers(struct zyl_server *server)
{
    for (int i = 0; i < GESTURE_DIRECTION_COUNT; i++)
        server->gesture_handlers[i] = NULL;

    server->gesture_handlers[GESTURE_SWIPE_UP]    = action_go_home;
    server->gesture_handlers[GESTURE_SWIPE_DOWN]  = action_notification_panel;
    server->gesture_handlers[GESTURE_SWIPE_LEFT]  = action_go_back;
    server->gesture_handlers[GESTURE_SWIPE_RIGHT] = action_app_switcher;
}

/* ================================================================
 * Touch event listeners
 * ================================================================ */

static void handle_touch_down(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, touch_down);
    struct wlr_touch_down_event *event = data;

    double abs_x = event->x * server->screen_width;
    double abs_y = event->y * server->screen_height;

    server->touch.active        = true;
    server->touch.start_x       = abs_x;
    server->touch.start_y       = abs_y;
    server->touch.current_x     = abs_x;
    server->touch.current_y     = abs_y;
    server->touch.start_time_ms = zyl_now_ms();
    server->touch.pending       = GESTURE_NONE;

    wlr_cursor_warp_absolute(server->cursor, NULL, event->x, event->y);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void handle_touch_motion(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, touch_motion);
    struct wlr_touch_motion_event *event = data;

    server->touch.current_x = event->x * server->screen_width;
    server->touch.current_y = event->y * server->screen_height;
}

static void handle_touch_up(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, touch_up);
    (void)data;

    if (!server->touch.active)
        return;
    server->touch.active = false;

    enum gesture_direction g =
        gesture_detect(&server->touch, server->screen_height,
                       &server->config);

    if (g > GESTURE_NONE && g < GESTURE_DIRECTION_COUNT &&
        server->gesture_handlers[g])
        server->gesture_handlers[g](server);
}

/* ================================================================
 * Cursor event listeners
 * ================================================================ */

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
                                           void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
                             event->x, event->y);
}

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(server->seat,
        event->time_msec, event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
        wlr_seat_pointer_notify_frame(server->seat);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_frame);
    (void)data;
    wlr_seat_pointer_notify_frame(server->seat);
}

/* ================================================================
 * Seat event listeners
 * ================================================================ */

static void handle_request_cursor(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused =
        server->seat->pointer_state.focused_client;
    if (focused == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
                               event->hotspot_x, event->hotspot_y);
    }
}

static void handle_request_set_selection(struct wl_listener *listener,
                                          void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* ================================================================
 * New-input listener
 * ================================================================ */

static void handle_new_input(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct wlr_keyboard *keyboard =
            wlr_keyboard_from_input_device(device);
        struct xkb_context *context =
            xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(
            context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
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

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER |
                    WL_SEAT_CAPABILITY_KEYBOARD;
    if (device->type == WLR_INPUT_DEVICE_TOUCH)
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server->seat, caps);
}

/* ================================================================
 * Public: wire all input listeners
 * ================================================================ */

void gesture_register_listeners(struct zyl_server *server)
{
    /* Touch */
    server->touch_down.notify = handle_touch_down;
    wl_signal_add(&server->cursor->events.touch_down, &server->touch_down);
    server->touch_up.notify = handle_touch_up;
    wl_signal_add(&server->cursor->events.touch_up, &server->touch_up);
    server->touch_motion.notify = handle_touch_motion;
    wl_signal_add(&server->cursor->events.touch_motion,
                  &server->touch_motion);

    /* Cursor / pointer */
    server->cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    server->cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute,
                  &server->cursor_motion_absolute);
    server->cursor_button.notify = handle_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    server->cursor_axis.notify = handle_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    server->cursor_frame.notify = handle_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    /* Seat */
    server->request_cursor.notify = handle_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor,
                  &server->request_cursor);
    server->request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection,
                  &server->request_set_selection);

    /* Input devices */
    server->new_input.notify = handle_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
}
