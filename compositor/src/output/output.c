/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Driver
 *
 * 역할: 디스플레이 출력 관리 — 모드 선택, 프레임 렌더링, 상태 요청, 해제
 * 수행범위: output 생성/파괴 리스너, 프레임 렌더링, scene graph 커밋
 * 의존방향: output.h → zyl_compositor.h
 * SOLID: SRP — 디스플레이 출력 라이프사이클 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "output.h"

#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

/* ─── Frame render with damage tracking ─── */
static void output_frame(struct wl_listener *listener, void *data)
{
    struct zyl_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene   = output->server->scene;
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(scene, output->wlr_output);

    if (!scene_output) {
        return;
    }

    /*
     * Damage-aware commit: wlr_scene_output_commit() only re-renders
     * regions that changed since the last frame. Pass NULL for the
     * options to use the scene's built-in damage tracking.
     */
    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);

    /*
     * Frame pacing: schedule the next frame instead of rendering
     * continuously. The compositor will only wake when the output
     * is ready for a new frame or when damage occurs.
     */
    wlr_output_schedule_frame(output->wlr_output);
}

/* ─── Output state request (mode change, enable/disable, ...) ─── */
static void output_request_state(struct wl_listener *listener, void *data)
{
    struct zyl_output *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

/* ─── Output destroyed ─── */
static void output_destroy(struct wl_listener *listener, void *data)
{
    struct zyl_output *output = wl_container_of(listener, output, destroy);
    (void)data;

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

/* ─── New output connected ─── */
static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* Prefer the native mode */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode)
        wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct zyl_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server     = server;

    /* Record screen dimensions for gesture math and fullscreen sizing */
    server->screen_width  = wlr_output->width;
    server->screen_height = wlr_output->height;
    wlr_log(WLR_INFO, "Output: %s (%dx%d)",
            wlr_output->name, server->screen_width, server->screen_height);

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state,
                  &output->request_state);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
}

/* ================================================================
 * Public
 * ================================================================ */

void output_register_listeners(struct zyl_server *server)
{
    server->new_output.notify = handle_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
}
