/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 컴포지터 핵심 타입 정의 및 모듈 간 인터페이스 선언
 * 수행범위: zyl_server 구조체, 설정값, 모든 모듈이 공유하는 타입 정의
 * 의존방향: 없음 (최상위 인터페이스, 다른 모듈이 이 헤더에 의존)
 * SOLID: ISP — 각 모듈이 필요한 인터페이스만 참조하도록 타입 분리
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_COMPOSITOR_H
#define ZYL_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>


#include "zyl_gesture.h"   /* gesture types, config, touch state (no wlroots) */

/* Return a zyl_config populated with sensible defaults. */
struct zyl_config zyl_config_defaults(void);

/* ─── Per-keyboard lifecycle state ─── */
/**
 * Allocated per connected keyboard device.  Holds the destroy listener
 * so that hot-unplug does not leave dangling pointers in the seat.
 */
struct zyl_keyboard {
    struct wl_list       link;          /* in zyl_server::keyboards */
    struct zyl_server   *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener   destroy;
    struct wl_listener   key;           /* → the focused app */
    struct wl_listener   modifiers;
};

/* ─── Per-view (window) state ─── */
struct zyl_view {
    struct wl_list          link;
    struct zyl_server      *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree  *scene_tree;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_fullscreen;
    bool               is_panel;   /* the system UI's surface (not an app) */
};

/* ─── Per-output (display) state ─── */
struct zyl_output {
    struct wl_list      link;
    struct zyl_server  *server;
    struct wlr_output  *wlr_output;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

/* ─── Gesture handler callback ─── */
typedef void (*gesture_handler_fn)(struct zyl_server *server);

/* ─── Server (root compositor state) ─── */
struct zyl_server {
    struct zyl_config config;

    struct wl_display          *wl_display;
    struct wlr_backend         *backend;
    struct wlr_renderer        *renderer;
    struct wlr_allocator       *allocator;
    struct wlr_scene           *scene;
    struct wlr_scene_output_layout *scene_layout;

    /* XDG shell */
    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener     new_xdg_toplevel;
    struct wl_listener     new_xdg_popup;
    /* A running app brought forward (gtk_window_present) — view/view.c */
    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wl_listener     request_activate;
    struct wl_list         views;          /* zyl_view::link */

    /* Cursor / pointer */
    struct wlr_cursor         *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    /* Touch */
    struct wl_listener touch_down;
    struct wl_listener touch_up;
    struct wl_listener touch_motion;
    struct wl_listener touch_frame;
    bool               has_touch;      /* a touchscreen was seen: seat capability */
    bool               has_pointer;
    struct touch_state touch;

    /* Seat */
    struct wlr_seat    *seat;
    struct wl_list      keyboards;     /* zyl_keyboard::link */
    struct wl_listener  new_input;
    struct wl_listener  request_cursor;
    struct wl_listener  request_set_selection;

    /* Outputs */
    struct wlr_output_layout *output_layout;
    struct wl_list            outputs;     /* zyl_output::link */
    struct wl_listener        new_output;

    /* Mobile state */
    struct zyl_view *active_view;
    int   screen_width;
    int   screen_height;

    /* Gesture → IPC: main.c wires the WAM navigation sender
     * (ipc/os_bus.c). NULL leaves only the log line.
     *
     * Signature: fn(server, signal_name, detail_or_NULL)
     */
    void (*gesture_signal_fn)(struct zyl_server *server,
                              const char *signal_name,
                              const char *detail);

    /* The system UI's surface (status bar, notification panel, recents):
       over every app, never in the app list — view/view.c */
    struct zyl_view *panel;

    /* Gesture dispatch table (indexed by enum gesture_direction) */
    gesture_handler_fn gesture_handlers[GESTURE_DIRECTION_COUNT];
};

/* ─── Utility ─── */
uint32_t zyl_now_ms(void);

#endif /* ZYL_COMPOSITOR_H */
