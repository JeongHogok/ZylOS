/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Driver
 *
 * 역할: 입력을 앱에 전달하고, 가장자리에서 시작한 터치는 시스템 제스처로
 *       바꿔 디스패치, 키보드/포인터/터치 입력 장치 라이프사이클 관리
 * 수행범위: 터치·포인터·키를 그 아래(포커스된) 앱 표면에 전달, 가장자리 터치가
 *           제스처가 되는 순간 앱의 터치를 취소, 판정(gesture_detect.c) 결과
 *           디스패치, 멀티터치 포인트 추적, seat 기능, 키보드 핫플러그
 * 의존방향: gesture.h → zyl_compositor.h, zyl_gesture.h, os_bus.h
 * SOLID: OCP — 함수 포인터 테이블로 제스처 액션을 교체 가능;
 *        SRP — 입력 장치 라이프사이클 별도 함수로 분리
 * ────────────────────────────────────────────────────────── */

#include "gesture.h"
#include "../ipc/os_bus.h"

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
 * Gesture → wlroots signal dispatch
 * ================================================================ */

static void emit_compositor_signal(struct zyl_server *server,
                                    const char *signal_name,
                                    const char *detail)
{
    wlr_log(WLR_INFO, "compositor signal: %s(%s)", signal_name,
            detail ? detail : "");

    /* main.c wires the WAM navigation sender (ipc/os_bus.c) */
    if (server->gesture_signal_fn) {
        server->gesture_signal_fn(server, signal_name, detail);
    }
}

/* ================================================================
 * Default gesture action implementations
 * ================================================================ */

/*
 * SWIPE UP from bottom — go home.
 * Sends GoHome to WAM: its shell policy decides (already home, the lock
 * screen while locked, nothing during setup).
 */
static void action_go_home(struct zyl_server *server)
{
    wlr_log(WLR_INFO, "Gesture: Go Home");
    emit_compositor_signal(server, "GoHome", NULL);
}

/*
 * SWIPE DOWN from the top — the notification panel. The WAM opens the system
 * UI's panel if the shell lets it (not while locked or in setup).
 */
static void action_notification_panel(struct zyl_server *server)
{
    wlr_log(WLR_INFO, "Gesture: Notification Panel");
    emit_compositor_signal(server, "OpenPanel", NULL);
}

/*
 * A short swipe up from the bottom — recents (the system UI draws them).
 */
static void action_recents(struct zyl_server *server)
{
    wlr_log(WLR_INFO, "Gesture: Recents");
    emit_compositor_signal(server, "ShowRecents", NULL);
}

/*
 * SWIPE LEFT — go back.
 * Sends GoBack to WAM: the app in front gets it first, an app that exits
 * goes where home goes.
 */
static void action_go_back(struct zyl_server *server)
{
    wlr_log(WLR_INFO, "Gesture: Go Back");
    emit_compositor_signal(server, "GoBack", NULL);
}

/*
 * SWIPE RIGHT — the next app the user returns to.
 * The WAM picks it and brings it up; raising a view here would raise one
 * over the lock screen.
 */
static void action_app_switcher(struct zyl_server *server)
{
    wlr_log(WLR_INFO, "Gesture: App Switch");
    emit_compositor_signal(server, "SwitchApp", "next");
}

void gesture_init_handlers(struct zyl_server *server)
{
    for (int i = 0; i < GESTURE_DIRECTION_COUNT; i++)
        server->gesture_handlers[i] = NULL;

    server->gesture_handlers[GESTURE_SWIPE_UP]    = action_go_home;
    server->gesture_handlers[GESTURE_SWIPE_DOWN]  = action_notification_panel;
    server->gesture_handlers[GESTURE_SWIPE_LEFT]  = action_go_back;
    server->gesture_handlers[GESTURE_SWIPE_RIGHT] = action_app_switcher;
    server->gesture_handlers[GESTURE_SWIPE_UP_SHORT] = action_recents;
}

/* ================================================================
 * Multi-touch point helpers
 * ================================================================ */

/**
 * Return the slot index for the given touch_id, or -1 if not found.
 */
static int touch_find_slot(struct touch_state *ts, int32_t id)
{
    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS; i++) {
        if (ts->points[i].active && ts->points[i].id == id)
            return i;
    }
    return -1;
}

/**
 * Allocate a free slot for a new touch_id.
 * Returns the slot index, or -1 if the pool is exhausted.
 */
static int touch_alloc_slot(struct touch_state *ts)
{
    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS; i++) {
        if (!ts->points[i].active)
            return i;
    }
    return -1;
}

/* ================================================================
 * Where input goes: the client surface under a layout point
 * ================================================================ */

static struct wlr_surface *surface_at(struct zyl_server *server, double lx, double ly,
                                      double *sx, double *sy)
{
    struct wlr_scene_node *node =
        wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return NULL;   /* nothing, or the notification overlay: not an app's */
    struct wlr_scene_surface *ss =
        wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
    return ss ? ss->surface : NULL;
}

/* ================================================================
 * Touch event listeners
 * Every touch goes to the app under it. One that began in a system zone
 * (an edge) and turns into a gesture is taken back: the app is told to
 * cancel, and the gesture runs when the finger lifts.
 * ================================================================ */

static void handle_touch_down(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, touch_down);
    struct wlr_touch_down_event *event = data;
    zyl_power_user_activity();

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base,
                                         event->x, event->y, &lx, &ly);

    /* ── Multi-touch: track this finger in the pool ── */
    int slot = touch_alloc_slot(&server->touch);
    struct touch_point *tp = slot >= 0 ? &server->touch.points[slot] : NULL;
    if (tp) {
        tp->active        = true;
        tp->id            = event->touch_id;
        tp->start_x       = lx;
        tp->start_y       = ly;
        tp->current_x     = lx;
        tp->current_y     = ly;
        tp->start_time_ms = zyl_now_ms();
        server->touch.num_active++;
    } else {
        wlr_log(WLR_DEBUG, "touch pool exhausted (>%d fingers)",
                ZYL_MAX_TOUCH_POINTS);
    }

    /* ── Primary (first finger): the one a gesture is read from ── */
    if (!server->touch.active) {
        server->touch.active        = true;
        server->touch.start_x       = lx;
        server->touch.start_y       = ly;
        server->touch.current_x     = lx;
        server->touch.current_y     = ly;
        server->touch.start_time_ms = zyl_now_ms();
        server->touch.pending       = GESTURE_NONE;
        server->touch.claimed       = false;
        server->touch.at_edge       = gesture_at_edge(lx, ly, server->screen_width,
                                                      server->screen_height, &server->config);
    }

    if (server->touch.claimed) return;   /* a gesture is in progress */
    double sx, sy;
    struct wlr_surface *surface = surface_at(server, lx, ly, &sx, &sy);
    if (!surface) return;
    if (tp) {
        tp->surface_dx = lx - sx;
        tp->surface_dy = ly - sy;
    }
    wlr_seat_touch_notify_down(server->seat, surface, event->time_msec,
                               event->touch_id, sx, sy);
}

/* The edge touch became a gesture: the app it went to lets it go */
static void claim_for_gesture(struct zyl_server *server)
{
    server->touch.claimed = true;
    for (int i = 0; i < ZYL_MAX_TOUCH_POINTS; i++) {
        if (!server->touch.points[i].active) continue;
        struct wlr_touch_point *p =
            wlr_seat_touch_get_point(server->seat, server->touch.points[i].id);
        if (p && p->client) wlr_seat_touch_notify_cancel(server->seat, p->client);
    }
    wlr_log(WLR_DEBUG, "edge touch claimed as a gesture");
}

static void handle_touch_motion(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, touch_motion);
    struct wlr_touch_motion_event *event = data;

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base,
                                         event->x, event->y, &lx, &ly);

    /* ── Multi-touch: update the tracked finger ── */
    int slot = touch_find_slot(&server->touch, event->touch_id);
    if (slot >= 0) {
        server->touch.points[slot].current_x = lx;
        server->touch.points[slot].current_y = ly;
    }

    /* ── Primary update (first finger) ── */
    if (server->touch.active &&
        (server->touch.num_active == 1 || slot == 0)) {
        server->touch.current_x = lx;
        server->touch.current_y = ly;
    }

    if (server->touch.active && server->touch.at_edge && !server->touch.claimed &&
        gesture_detect(&server->touch, server->screen_width, server->screen_height,
                       &server->config) != GESTURE_NONE)
        claim_for_gesture(server);

    if (server->touch.claimed || slot < 0) return;
    struct touch_point *tp = &server->touch.points[slot];
    wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id,
                                 lx - tp->surface_dx, ly - tp->surface_dy);
}

static void handle_touch_up(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, touch_up);
    struct wlr_touch_up_event *event = data;

    if (!server->touch.claimed)
        wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);

    /* ── Multi-touch: release the finger slot ── */
    int slot = touch_find_slot(&server->touch, event->touch_id);
    if (slot >= 0) {
        server->touch.points[slot].active = false;
        if (server->touch.num_active > 0)
            server->touch.num_active--;
    }

    /* ── Primary: the gesture runs when the last finger lifts ── */
    if (!server->touch.active || server->touch.num_active > 0)
        return;
    server->touch.active = false;
    if (!server->touch.claimed)
        return;
    server->touch.claimed = false;
    enum gesture_direction g =
        gesture_detect(&server->touch, server->screen_width, server->screen_height,
                       &server->config);
    if (g > GESTURE_NONE && g < GESTURE_DIRECTION_COUNT && server->gesture_handlers[g])
        server->gesture_handlers[g](server);
}

static void handle_touch_frame(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, touch_frame);
    (void)data;
    wlr_seat_touch_notify_frame(server->seat);
}

/* ================================================================
 * Cursor event listeners: the pointer goes to the surface under it
 * ================================================================ */

static void pointer_moved(struct zyl_server *server, uint32_t time_msec)
{
    double sx, sy;
    struct wlr_surface *surface =
        surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
    if (!surface) {
        wlr_seat_pointer_clear_focus(server->seat);
        return;
    }
    wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
    pointer_moved(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
                                           void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
                             event->x, event->y);
    pointer_moved(server, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    zyl_power_user_activity();
    wlr_seat_pointer_notify_button(server->seat,
        event->time_msec, event->button, event->state);
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
 * Keyboard lifecycle: per-device destroy listener
 * ================================================================ */

/**
 * Called when a keyboard device is unplugged or destroyed by wlroots.
 * Removes the zyl_keyboard from the server list, clears the seat's
 * keyboard reference if it pointed at this device, and frees the
 * allocation — preventing dangling pointers and memory leaks.
 */
/* Keys go to the focused app (view_focus gives it the keyboard) */
static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
    struct zyl_keyboard *kb = wl_container_of(listener, kb, key);
    struct wlr_keyboard_key_event *event = data;
    zyl_power_user_activity();
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_key(kb->server->seat, event->time_msec,
                                 event->keycode, event->state);
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
    (void)data;
    struct zyl_keyboard *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat, &kb->wlr_keyboard->modifiers);
}

/* What the seat offers apps: what has been plugged in, all of it */
static void update_seat_caps(struct zyl_server *server)
{
    uint32_t caps = 0;
    if (server->has_pointer || server->has_touch) caps |= WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards))       caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (server->has_touch)                        caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server->seat, caps);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct zyl_keyboard *kb =
        wl_container_of(listener, kb, destroy);
    struct zyl_server *server = kb->server;

    wlr_log(WLR_INFO, "Keyboard removed: %s",
            kb->wlr_keyboard->base.name ? kb->wlr_keyboard->base.name : "(unnamed)");

    /* If this was the active seat keyboard, clear the reference.
     * wlr_seat_set_keyboard(NULL) notifies clients that no keyboard
     * is focused, preventing them from accessing a freed object. */
    if (wlr_seat_get_keyboard(server->seat) == kb->wlr_keyboard) {
        wlr_seat_set_keyboard(server->seat, NULL);

        /* Promote the next available keyboard, if any */
        struct zyl_keyboard *other;
        wl_list_for_each(other, &server->keyboards, link) {
            if (other != kb) {
                wlr_seat_set_keyboard(server->seat, other->wlr_keyboard);
                break;
            }
        }
    }

    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->link);
    free(kb);
    update_seat_caps(server);
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
        struct wlr_keyboard *wlr_kb =
            wlr_keyboard_from_input_device(device);

        /* Allocate per-keyboard state with destroy listener */
        struct zyl_keyboard *kb = calloc(1, sizeof(*kb));
        if (!kb) {
            wlr_log(WLR_ERROR, "handle_new_input: OOM for zyl_keyboard");
            break;
        }
        wl_list_init(&kb->link);
        kb->server       = server;
        kb->wlr_keyboard = wlr_kb;

        /* Register the destroy listener — prevents dangling pointer
         * on hot-unplug (the critical bug identified in the audit). */
        kb->destroy.notify = handle_keyboard_destroy;
        wl_signal_add(&device->events.destroy, &kb->destroy);

        wl_list_insert(&server->keyboards, &kb->link);

        /* Configure XKB keymap (default layout) */
        struct xkb_context *context =
            xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context) {
            wlr_log(WLR_ERROR, "handle_new_input: xkb_context_new failed");
            wl_list_remove(&kb->destroy.link);
            wl_list_remove(&kb->link);
            free(kb);
            break;
        }

        struct xkb_keymap *keymap = xkb_keymap_new_from_names(
            context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) {
            wlr_log(WLR_ERROR, "handle_new_input: xkb_keymap_new_from_names failed");
            xkb_context_unref(context);
            wl_list_remove(&kb->destroy.link);
            wl_list_remove(&kb->link);
            free(kb);
            break;
        }

        wlr_keyboard_set_keymap(wlr_kb, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);

        wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);
        kb->key.notify = handle_keyboard_key;
        wl_signal_add(&wlr_kb->events.key, &kb->key);
        kb->modifiers.notify = handle_keyboard_modifiers;
        wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
        wlr_seat_set_keyboard(server->seat, wlr_kb);

        wlr_log(WLR_INFO, "Keyboard added: %s",
                device->name ? device->name : "(unnamed)");
        break;
    }
    case WLR_INPUT_DEVICE_TOUCH:
        /* The cursor turns the touchscreen's events into touch_down/motion/up
           in layout coordinates — without this no touch reached anything */
        wlr_cursor_attach_input_device(server->cursor, device);
        wlr_cursor_map_input_to_output(server->cursor, device, NULL);
        server->has_touch = true;
        wlr_log(WLR_INFO, "Touch device: %s",
                device->name ? device->name : "(unnamed)");
        break;
    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(server->cursor, device);
        server->has_pointer = true;
        break;
    default:
        break;
    }
    update_seat_caps(server);
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
    server->touch_frame.notify = handle_touch_frame;
    wl_signal_add(&server->cursor->events.touch_frame, &server->touch_frame);

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
