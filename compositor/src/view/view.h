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

#endif /* ZYL_VIEW_VIEW_H */
