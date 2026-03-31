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

/* ─── Split-screen mode ─── */
enum zyl_split_mode {
    ZYL_SPLIT_NONE,         /* Full-screen (default)          */
    ZYL_SPLIT_HORIZONTAL,   /* Two panes side by side (left | right)   */
    ZYL_SPLIT_VERTICAL      /* Two panes top and bottom (top / bottom) */
};

/* ─── PiP (Picture-in-Picture) configuration ─── */
struct zyl_pip_config {
    bool                    active;        /* true = PiP layer is visible           */
    struct zyl_view        *pip_view;      /* The view rendered in the PiP window   */
    int                     x;             /* PiP window origin X (px from left)    */
    int                     y;             /* PiP window origin Y (px from top)     */
    int                     width;         /* PiP window width  (px)                */
    int                     height;        /* PiP window height (px)                */
    struct wlr_scene_tree  *pip_scene_tree;/* Dedicated scene sub-tree for PiP      */
};

/* ─── Gesture direction identifiers ─── */
enum gesture_direction {
    GESTURE_NONE,
    GESTURE_SWIPE_UP,      /* Go home          */
    GESTURE_SWIPE_DOWN,    /* Notification panel */
    GESTURE_SWIPE_LEFT,    /* Go back           */
    GESTURE_SWIPE_RIGHT,   /* App switcher      */
    GESTURE_DIRECTION_COUNT
};

/* ─── Runtime configuration (no #defines) ─── */
struct zyl_config {
    int swipe_threshold_px;    /* Min px to register a swipe          */
    int swipe_from_bottom_px;  /* Bottom-edge hot zone height         */
    int swipe_from_top_px;     /* Top-edge hot zone height            */
    int statusbar_height_px;   /* Pixels reserved for the status bar  */
};

/* Return a zyl_config populated with sensible defaults. */
struct zyl_config zyl_config_defaults(void);

/* ─── Multi-touch tracking ─── */
#define ZYL_MAX_TOUCH_POINTS 10

/**
 * Per-finger touch point, keyed by hardware touch_id.
 * Allocated from a fixed pool to avoid per-event heap churn.
 */
struct touch_point {
    bool     active;
    int32_t  id;            /* wlr touch_id (hardware assigned) */
    double   start_x, start_y;
    double   current_x, current_y;
    uint32_t start_time_ms;
};

/* ─── Touch tracking state ─── */
struct touch_state {
    /* Primary touch — backwards-compatible single-touch path */
    bool     active;
    double   start_x, start_y;
    double   current_x, current_y;
    uint32_t start_time_ms;
    enum gesture_direction pending;

    /* Multi-touch pool (indexed by slot, not touch_id) */
    struct touch_point points[ZYL_MAX_TOUCH_POINTS];
    int                num_active;   /* count of active fingers */
};

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
};

/* ─── Per-view (window) state ─── */
struct zyl_view {
    struct wl_list          link;
    struct zyl_server      *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree  *scene_tree;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_fullscreen;
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
    bool  home_screen_visible;
    int   screen_width;
    int   screen_height;

    /* Split-screen state */
    enum zyl_split_mode  split_mode;
    struct zyl_view     *split_primary;     /* Primary pane view            */
    struct zyl_view     *split_secondary;   /* Secondary pane view          */
    int                  split_ratio_pct;   /* Primary pane share 0-100; default 50 */

    /* Gesture → IPC signal dispatch (narrow stub interface).
     *
     * Coordinator follow-up required:
     *   Wire gesture_signal_fn to an actual D-Bus / GLib or Unix-socket
     *   sender in main.c after the event-loop integration is resolved.
     *   Until then the field defaults to NULL and the built-in log
     *   fallback in emit_compositor_signal() is used.
     *
     * Signature: fn(server, signal_name, detail_or_NULL)
     */
    void (*gesture_signal_fn)(struct zyl_server *server,
                              const char *signal_name,
                              const char *detail);

    /* PiP (Picture-in-Picture) */
    struct zyl_pip_config pip;

    /* Notification panel overlay */
    struct wlr_scene_rect *notif_overlay;
    bool  notif_panel_visible;

    /* Gesture dispatch table (indexed by enum gesture_direction) */
    gesture_handler_fn gesture_handlers[GESTURE_DIRECTION_COUNT];
};

/* ─── Utility ─── */
uint32_t zyl_now_ms(void);

#endif /* ZYL_COMPOSITOR_H */
