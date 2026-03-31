/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Adapter
 *
 * 역할: XDG shell 윈도우/뷰 라이프사이클 관리 + 분할화면/PiP 레이아웃
 * 수행범위: toplevel/popup 서피스 생성, 포커스, 풀스크린, map/unmap, 파괴,
 *           split-screen layout 계산, PiP 레이어 렌더링
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
 * Split-screen layout
 * ================================================================ */

/**
 * Clamp a value to [lo, hi].
 */
static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void view_apply_split_layout(struct zyl_server *server)
{
    if (!server) return;
    if (server->split_mode == ZYL_SPLIT_NONE) return;

    struct zyl_view *primary   = server->split_primary;
    struct zyl_view *secondary = server->split_secondary;

    if (!primary || !secondary) {
        wlr_log(WLR_ERROR,
                "view_apply_split_layout: split_mode set but primary/secondary view is NULL");
        return;
    }

    int sw = server->screen_width;
    int sh = server->screen_height - server->config.statusbar_height_px;
    if (sw <= 0 || sh <= 0) return;

    /* ratio_pct: portion of the screen the primary pane takes (0-100) */
    int ratio = clamp_int(server->split_ratio_pct, 5, 95);

    int primary_w, primary_h, secondary_w, secondary_h;
    int primary_x = 0, primary_y = 0;
    int secondary_x, secondary_y;

    if (server->split_mode == ZYL_SPLIT_HORIZONTAL) {
        /* Left | Right */
        primary_w   = (sw * ratio) / 100;
        primary_h   = sh;
        secondary_x = primary_w;
        secondary_y = 0;
        secondary_w = sw - primary_w;
        secondary_h = sh;
    } else {
        /* Top / Bottom  (ZYL_SPLIT_VERTICAL) */
        primary_w   = sw;
        primary_h   = (sh * ratio) / 100;
        secondary_x = 0;
        secondary_y = primary_h;
        secondary_w = sw;
        secondary_h = sh - primary_h;
    }

    /* Resize and reposition primary */
    wlr_xdg_toplevel_set_size(primary->xdg_toplevel, primary_w, primary_h);
    wlr_scene_node_set_position(&primary->scene_tree->node, primary_x, primary_y);

    /* Resize and reposition secondary */
    wlr_xdg_toplevel_set_size(secondary->xdg_toplevel, secondary_w, secondary_h);
    wlr_scene_node_set_position(&secondary->scene_tree->node, secondary_x, secondary_y);

    wlr_log(WLR_DEBUG,
            "split layout: mode=%d ratio=%d%%  primary=%dx%d@%d,%d  secondary=%dx%d@%d,%d",
            server->split_mode, ratio,
            primary_w, primary_h, primary_x, primary_y,
            secondary_w, secondary_h, secondary_x, secondary_y);
}

/* ================================================================
 * PiP (Picture-in-Picture) rendering
 * ================================================================ */

void view_update_pip(struct zyl_server *server)
{
    if (!server) return;

    struct zyl_pip_config *pip = &server->pip;

    if (!pip->active || !pip->pip_view) {
        /* Hide PiP scene tree if it exists */
        if (pip->pip_scene_tree) {
            wlr_scene_node_set_enabled(&pip->pip_scene_tree->node, false);
        }
        return;
    }

    /* Lazily create a dedicated scene sub-tree for the PiP layer */
    if (!pip->pip_scene_tree) {
        pip->pip_scene_tree =
            wlr_scene_tree_create(&server->scene->tree);
        if (!pip->pip_scene_tree) {
            wlr_log(WLR_ERROR, "view_update_pip: failed to create pip scene tree");
            return;
        }
    }

    /* Re-parent the PiP view's scene node into the PiP sub-tree if needed.
     * (The scene tree owns the node; we just reposition it.) */
    wlr_scene_node_set_enabled(&pip->pip_scene_tree->node, true);

    /* Position and size */
    int x = clamp_int(pip->x, 0, server->screen_width  - 1);
    int y = clamp_int(pip->y, 0, server->screen_height - 1);

    wlr_scene_node_set_position(&pip->pip_scene_tree->node, x, y);
    wlr_xdg_toplevel_set_size(pip->pip_view->xdg_toplevel,
                              pip->width  > 0 ? pip->width  : 320,
                              pip->height > 0 ? pip->height : 180);

    /* Raise the PiP sub-tree to the top of the scene graph so it
     * always renders above regular views and the split-screen layout. */
    wlr_scene_node_raise_to_top(&pip->pip_scene_tree->node);

    wlr_log(WLR_DEBUG,
            "pip update: active=true pos=%d,%d size=%dx%d",
            x, y, pip->width, pip->height);
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
