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
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data)
{
    struct zyl_view *view = wl_container_of(listener, view, destroy);
    (void)data;
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
    /* Mobile OS: every app is always fullscreen */
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
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
    view->server       = server;
    view->xdg_toplevel = toplevel;
    view->scene_tree   =
        wlr_scene_xdg_surface_create(&server->scene->tree,
                                     toplevel->base);
    view->scene_tree->node.data = view;
    toplevel->base->data        = view->scene_tree;

    /* Force fullscreen */
    wlr_xdg_toplevel_set_fullscreen(toplevel, true);
    if (server->screen_width > 0) {
        wlr_xdg_toplevel_set_size(toplevel,
            server->screen_width,
            server->screen_height - server->config.statusbar_height_px);
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

static void handle_new_xdg_popup(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_xdg_popup *popup = data;
    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (!parent)
        return;
    struct wlr_scene_tree *parent_tree = parent->data;
    popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, popup->base);
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
