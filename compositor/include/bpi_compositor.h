/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 컴포지터 핵심 타입 정의 및 모듈 간 인터페이스 선언
 * 수행범위: bpi_server 구조체, 설정값, 모든 모듈이 공유하는 타입 정의
 * 의존방향: 없음 (최상위 인터페이스, 다른 모듈이 이 헤더에 의존)
 * SOLID: ISP — 각 모듈이 필요한 인터페이스만 참조하도록 타입 분리
 * ────────────────────────────────────────────────────────── */

#ifndef BPI_COMPOSITOR_H
#define BPI_COMPOSITOR_H

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
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

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
struct bpi_config {
    int swipe_threshold_px;    /* Min px to register a swipe          */
    int swipe_from_bottom_px;  /* Bottom-edge hot zone height         */
    int swipe_from_top_px;     /* Top-edge hot zone height            */
    int statusbar_height_px;   /* Pixels reserved for the status bar  */
};

/* Return a bpi_config populated with sensible defaults. */
struct bpi_config bpi_config_defaults(void);

/* ─── Touch tracking state ─── */
struct touch_state {
    bool     active;
    double   start_x, start_y;
    double   current_x, current_y;
    uint32_t start_time_ms;
    enum gesture_direction pending;
};

/* ─── Per-view (window) state ─── */
struct bpi_view {
    struct wl_list          link;
    struct bpi_server      *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree  *scene_tree;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_fullscreen;
};

/* ─── Per-output (display) state ─── */
struct bpi_output {
    struct wl_list      link;
    struct bpi_server  *server;
    struct wlr_output  *wlr_output;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

/* ─── Gesture handler callback ─── */
typedef void (*gesture_handler_fn)(struct bpi_server *server);

/* ─── Server (root compositor state) ─── */
struct bpi_server {
    struct bpi_config config;

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
    struct wl_list         views;          /* bpi_view::link */

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
    struct wl_listener  new_input;
    struct wl_listener  request_cursor;
    struct wl_listener  request_set_selection;

    /* Outputs */
    struct wlr_output_layout *output_layout;
    struct wl_list            outputs;     /* bpi_output::link */
    struct wl_listener        new_output;

    /* Mobile state */
    struct bpi_view *active_view;
    bool  home_screen_visible;
    int   screen_width;
    int   screen_height;

    /* Gesture dispatch table (indexed by enum gesture_direction) */
    gesture_handler_fn gesture_handlers[GESTURE_DIRECTION_COUNT];
};

/* ─── Utility ─── */
uint32_t bpi_now_ms(void);

#endif /* BPI_COMPOSITOR_H */
