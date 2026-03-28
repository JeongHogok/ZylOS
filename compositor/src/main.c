/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Entry Point
 *
 * 역할: Wayland 디스플레이, wlroots 백엔드, 렌더러 초기화 및 이벤트 루프 진입
 * 수행범위: 컴포지터 프로세스의 시작점으로 모든 모듈을 초기화하고 이벤트 루프 실행
 * 의존방향: output, view, gesture 모듈 → bpi_compositor.h
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

#include "bpi_compositor.h"
#include "input/gesture.h"
#include "output/output.h"
#include "view/view.h"

/* ─── Utility: monotonic clock in milliseconds ─── */
uint32_t bpi_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ─── Default configuration ─── */
struct bpi_config bpi_config_defaults(void)
{
    return (struct bpi_config){
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
    wlr_log(WLR_INFO, "BPI-OS Compositor starting...");

    struct bpi_server server = {0};
    server.config              = bpi_config_defaults();
    server.home_screen_visible = true;
    wl_list_init(&server.views);
    wl_list_init(&server.outputs);

    /* ── Wayland display ── */
    server.wl_display = wl_display_create();

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
    server.scene         = wlr_scene_create();
    server.scene_layout  =
        wlr_scene_attach_output_layout(server.scene, server.output_layout);

    /* ── Outputs (module) ── */
    output_register_listeners(&server);

    /* ── XDG Shell + views (module) ── */
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    view_register_listeners(&server);

    /* ── Cursor ── */
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

    /* ── Seat ── */
    server.seat = wlr_seat_create(server.wl_display, "seat0");

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
        "BPI-OS Compositor running on WAYLAND_DISPLAY=%s", socket);

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

    wlr_log(WLR_INFO, "BPI-OS Compositor shut down cleanly");
    return 0;
}
