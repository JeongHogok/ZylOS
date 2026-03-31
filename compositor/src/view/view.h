/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: XDG shell 윈도우/뷰 라이프사이클 인터페이스 정의
 * 수행범위: view_focus, view_register_listeners 등 뷰 관련 함수 선언
 * 의존방향: zyl_compositor.h
 * SOLID: DIP — 뷰 구현이 아닌 추상 인터페이스에 의존
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_VIEW_VIEW_H
#define ZYL_VIEW_VIEW_H

#include "zyl_compositor.h"

/*
 * Focus a view: raise it to the top of the scene graph, activate it,
 * set it as the keyboard target, and enforce fullscreen sizing.
 * Passing NULL is a safe no-op.
 */
void view_focus(struct zyl_view *view);

/*
 * Register XDG shell listeners (new_toplevel, new_popup) on the server.
 * Must be called after xdg_shell is created.
 */
void view_register_listeners(struct zyl_server *server);

/* ── Split-screen layout ───────────────────────────────────────
 *
 * Apply the server's current split_mode to the primary and secondary
 * views.  Both views must be non-NULL when split_mode != ZYL_SPLIT_NONE.
 *
 * ZYL_SPLIT_HORIZONTAL: primary on the left, secondary on the right.
 * ZYL_SPLIT_VERTICAL:   primary on top,      secondary on the bottom.
 * split_ratio_pct (0-100) controls how much of the screen the primary
 * pane occupies; 50 means equal halves.
 *
 * Calls wlr_xdg_toplevel_set_size() on both views and repositions their
 * scene_tree nodes accordingly.  Safe to call when split_mode ==
 * ZYL_SPLIT_NONE (no-op in that case).
 */
void view_apply_split_layout(struct zyl_server *server);

/* ── PiP (Picture-in-Picture) rendering ──────────────────────
 *
 * Synchronise the PiP scene sub-tree with server->pip:
 *   - If pip.active && pip.pip_view != NULL: position and resize the
 *     pip_scene_tree node to (pip.x, pip.y) with size (pip.width, pip.height),
 *     raise it above all other nodes so it is always on top.
 *   - If !pip.active: hide the pip_scene_tree node (set enabled=false).
 *
 * Must be called after any change to server->pip fields.
 */
void view_update_pip(struct zyl_server *server);

#endif /* ZYL_VIEW_VIEW_H */
