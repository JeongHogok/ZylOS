/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Adapter
 *
 * 역할: XDG shell 윈도우/뷰 라이프사이클 관리
 * 수행범위: toplevel/popup 서피스 생성, 포커스, 풀스크린, map/unmap, 파괴
 * 의존방향: view.h → zyl_compositor.h
 * SOLID: SRP — 윈도우 뷰 관리 로직만 담당
 * ────────────────────────────────────────────────────────── */

#include "view.h"

#include <stdlib.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

/* ================================================================
 * Focus
 * ================================================================ */

void view_focus(struct zyl_view *view)
{
    if (!view)
        return;

    struct zyl_server *server = view->server;
    struct wlr_seat   *seat   = server->seat;
    struct wlr_surface *prev  = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface =
        view->xdg_toplevel->base->surface;

    if (prev == surface)
        return;

    /* Deactivate previous */
    if (prev) {
        struct wlr_xdg_toplevel *prev_tl =
            wlr_xdg_toplevel_try_from_wlr_surface(prev);
        if (prev_tl)
            wlr_xdg_toplevel_set_activated(prev_tl, false);
    }

    /* Raise to top of scene graph and view list */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);

    wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);

    /* Enforce fullscreen minus status bar */
    if (server->screen_width > 0 && server->screen_height > 0) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel,
            server->screen_width,
            server->screen_height - server->config.statusbar_height_px);
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes,
            &keyboard->modifiers);
    }

    server->active_view = view;
}

/* ================================================================
 * XDG toplevel event listeners
 * ================================================================ */

static void xdg_toplevel_map(struct wl_listener *listener, void *data)
{
    struct zyl_view *view = wl_container_of(listener, view, map);
    (void)data;
    wl_list_insert(&view->server->views, &view->link);
    view_focus(view);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data)
{
    struct zyl_view *view = wl_container_of(listener, view, unmap);
    (void)data;
    if (view == view->server->active_view)
        view->server->active_view = NULL;
    wl_list_remove(&view->link);
    wl_list_init(&view->link);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data)
{
    struct zyl_view *view = wl_container_of(listener, view, destroy);
    (void)data;
    if (view == view->server->active_view)
        view->server->active_view = NULL;
    wl_list_remove(&view->link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_fullscreen.link);
    free(view);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
                                             void *data)
{
    struct zyl_view *view =
        wl_container_of(listener, view, request_fullscreen);
    (void)data;
    /* GTK 는 첫 commit 전에도 전체화면을 요청한다 — 그때는 아무것도 하지 않고
       초기 commit 처리에서 함께 보낸다 (xdg_shell 규약) */
    if (!view->xdg_toplevel->base->initialized)
        return;
    /* Mobile OS: every app is always fullscreen */
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
}

/* 크기·전체화면 같은 configure 는 클라이언트의 첫 commit 뒤에 보내야 한다.
   먼저 보내면 wlroots 가 "configure is scheduled for an uninitialized
   xdg_surface" 로 거르고 앱은 자기 크기를 모른 채 그린다 */
static void xdg_toplevel_commit(struct wl_listener *listener, void *data)
{
    struct zyl_view *view = wl_container_of(listener, view, commit);
    (void)data;
    if (!view->xdg_toplevel->base->initial_commit)
        return;
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
    if (view->server->screen_width > 0) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel,
            view->server->screen_width,
            view->server->screen_height
                - view->server->config.statusbar_height_px);
    }
}

/* ================================================================
 * New XDG toplevel / popup
 * ================================================================ */

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    struct zyl_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    struct zyl_view *view = calloc(1, sizeof(*view));
    if (!view) {
        wlr_log(WLR_ERROR, "handle_new_xdg_toplevel: OOM — cannot allocate zyl_view");
        return;
    }
    wl_list_init(&view->link);
    view->server       = server;
    view->xdg_toplevel = toplevel;
    view->scene_tree   =
        wlr_scene_xdg_surface_create(&server->scene->tree,
                                     toplevel->base);
    if (!view->scene_tree) {
        wlr_log(WLR_ERROR,
                "handle_new_xdg_toplevel: wlr_scene_xdg_surface_create failed");
        free(view);
        return;
    }
    view->scene_tree->node.data = view;
    toplevel->base->data        = view->scene_tree;

    view->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
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

static void handle_new_xdg_popup(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_xdg_popup *popup = data;
    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (!parent)
        return;

    struct wlr_scene_tree *parent_tree = parent->data;
    if (!parent_tree) {
        wlr_log(WLR_ERROR,
                "handle_new_xdg_popup: parent scene tree missing");
        return;
    }

    popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, popup->base);
    if (!popup->base->data) {
        wlr_log(WLR_ERROR,
                "handle_new_xdg_popup: wlr_scene_xdg_surface_create failed");
    }
}


/* ================================================================
 * Public
 * ================================================================ */

void view_register_listeners(struct zyl_server *server)
{
    server->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel,
                  &server->new_xdg_toplevel);
    server->new_xdg_popup.notify = handle_new_xdg_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup,
                  &server->new_xdg_popup);
}
