/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Entry Point
 *
 * 역할: Wayland 디스플레이, wlroots 백엔드, 렌더러 초기화 및 이벤트 루프 진입
 * 수행범위: 컴포지터 프로세스의 시작점으로 모든 모듈을 초기화하고 이벤트 루프 실행
 * 의존방향: output, view, gesture 모듈 → zyl_compositor.h
 * SOLID: SRP — main()은 초기화와 이벤트 루프 진입만 담당
 * ────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "zyl_compositor.h"
#include "input/gesture.h"
#include "output/output.h"
#include "view/view.h"

/* ─── Utility: monotonic clock in milliseconds ─── */
uint32_t zyl_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ─── Default configuration ─── */
struct zyl_config zyl_config_defaults(void)
{
    return (struct zyl_config){
        .swipe_threshold_px   = 50,
        .swipe_from_bottom_px = 40,
        .swipe_from_top_px    = 40,
        .statusbar_height_px  = 36,
    };
}

/* ─── Main ─── */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    wlr_log_init(WLR_DEBUG, NULL);
    wlr_log(WLR_INFO, "Zyl OS Compositor starting...");

    struct zyl_server server = {0};
    server.config              = zyl_config_defaults();
    server.home_screen_visible = true;
    server.split_ratio_pct     = 50;   /* Bug fix: 0-init → clamp(0,5,95)=5%; must be 50 */
    server.gesture_signal_fn   = NULL; /* Coordinator: wire D-Bus sender here */
    wl_list_init(&server.views);
    wl_list_init(&server.outputs);
    wl_list_init(&server.keyboards);

    /* ── Wayland display ── */
    server.wl_display = wl_display_create();
    if (!server.wl_display) {
        wlr_log(WLR_ERROR, "Failed to create wl_display");
        return 1;
    }

    /* ── Backend ── */
    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.wl_display), NULL);
    if (!server.backend) {
        wlr_log(WLR_ERROR, "Failed to create wlr_backend");
        return 1;
    }

    /* ── Renderer ── */
    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer) {
        wlr_log(WLR_ERROR, "Failed to create wlr_renderer");
        return 1;
    }
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    /* ── Allocator ── */
    server.allocator =
        wlr_allocator_autocreate(server.backend, server.renderer);
    if (!server.allocator) {
        wlr_log(WLR_ERROR, "Failed to create wlr_allocator");
        return 1;
    }

    /* ── Wayland globals ── */
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);

    /* ── Scene graph + output layout ── */
    server.output_layout = wlr_output_layout_create(server.wl_display);
    if (!server.output_layout) {
        wlr_log(WLR_ERROR, "Failed to create wlr_output_layout");
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    server.scene = wlr_scene_create();
    if (!server.scene) {
        wlr_log(WLR_ERROR, "Failed to create wlr_scene");
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    server.scene_layout =
        wlr_scene_attach_output_layout(server.scene, server.output_layout);
    if (!server.scene_layout) {
        wlr_log(WLR_ERROR, "Failed to attach output layout to scene");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* ── Outputs (module) ── */
    output_register_listeners(&server);

    /* ── XDG Shell + views (module) ── */
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    if (!server.xdg_shell) {
        wlr_log(WLR_ERROR, "Failed to create wlr_xdg_shell");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }
    view_register_listeners(&server);

    /* ── Cursor ── */
    server.cursor = wlr_cursor_create();
    if (!server.cursor) {
        wlr_log(WLR_ERROR, "Failed to create wlr_cursor");
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    /* Load cursor theme from environment or use "default" fallback */
    const char *cursor_theme = getenv("XCURSOR_THEME");
    if (!cursor_theme || cursor_theme[0] == '\0') {
        cursor_theme = "default";
    }

    /* DPI-aware cursor size: base 24px scaled by output scale factor */
    int cursor_scale = 1;
    const char *scale_env = getenv("XCURSOR_SIZE");
    if (scale_env && scale_env[0] != '\0') {
        int parsed = atoi(scale_env);
        if (parsed > 0) {
            cursor_scale = parsed;
        }
    }
    int cursor_size = (cursor_scale > 0) ? cursor_scale : 24;

    server.cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
    if (!server.cursor_mgr) {
        wlr_log(WLR_ERROR, "Failed to create xcursor manager");
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* Pre-load cursor at scale 1; fallback to "default" theme on failure */
    if (wlr_xcursor_manager_load(server.cursor_mgr, 1)) {
        wlr_log(WLR_INFO, "Cursor theme '%s' loaded at size %d",
                cursor_theme, cursor_size);
    } else if (strcmp(cursor_theme, "default") != 0) {
        wlr_log(WLR_ERROR, "Cursor theme '%s' not found, trying 'default'",
                cursor_theme);
        wlr_xcursor_manager_destroy(server.cursor_mgr);
        server.cursor_mgr = wlr_xcursor_manager_create("default", 24);
        if (!server.cursor_mgr) {
            wlr_log(WLR_ERROR, "Failed to create fallback xcursor manager");
            wlr_cursor_destroy(server.cursor);
            wlr_scene_node_destroy(&server.scene->tree.node);
            wlr_output_layout_destroy(server.output_layout);
            wlr_allocator_destroy(server.allocator);
            wlr_renderer_destroy(server.renderer);
            wlr_backend_destroy(server.backend);
            wl_display_destroy(server.wl_display);
            return 1;
        }
        if (!wlr_xcursor_manager_load(server.cursor_mgr, 1)) {
            wlr_log(WLR_ERROR, "Failed to load fallback cursor theme 'default'");
            wlr_xcursor_manager_destroy(server.cursor_mgr);
            wlr_cursor_destroy(server.cursor);
            wlr_scene_node_destroy(&server.scene->tree.node);
            wlr_output_layout_destroy(server.output_layout);
            wlr_allocator_destroy(server.allocator);
            wlr_renderer_destroy(server.renderer);
            wlr_backend_destroy(server.backend);
            wl_display_destroy(server.wl_display);
            return 1;
        }
    } else {
        wlr_log(WLR_ERROR, "Failed to load cursor theme '%s'", cursor_theme);
        wlr_xcursor_manager_destroy(server.cursor_mgr);
        wlr_cursor_destroy(server.cursor);
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* ── Seat ── */
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    if (!server.seat) {
        wlr_log(WLR_ERROR, "Failed to create wlr_seat");
        wlr_xcursor_manager_destroy(server.cursor_mgr);
        wlr_cursor_destroy(server.cursor);
        wlr_scene_node_destroy(&server.scene->tree.node);
        wlr_output_layout_destroy(server.output_layout);
        wlr_allocator_destroy(server.allocator);
        wlr_renderer_destroy(server.renderer);
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* ── Gestures + input (module) ── */
    gesture_init_handlers(&server);
    gesture_register_listeners(&server);

    /* ── Wayland socket ── */
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
        "Zyl OS Compositor running on WAYLAND_DISPLAY=%s", socket);

    /* ── Event loop ── */
    wl_display_run(server.wl_display);

    /* ── Cleanup ── */
    wl_display_destroy_clients(server.wl_display);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);

    wlr_log(WLR_INFO, "Zyl OS Compositor shut down cleanly");
    return 0;
}
