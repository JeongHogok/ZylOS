/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 제스처 감지, 디스패치, Wayland 리스너 연결 인터페이스 정의
 * 수행범위: gesture_direction 열거형, gesture_detect/dispatch/register 함수 선언
 * 의존방향: zyl_compositor.h
 * SOLID: DIP — 구현이 아닌 인터페이스(함수 선언)에 의존
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_INPUT_GESTURE_H
#define ZYL_INPUT_GESTURE_H

#include "zyl_compositor.h"

/*
 * Detect which gesture (if any) a completed touch sequence represents.
 * Returns GESTURE_NONE when the movement is below the swipe threshold.
 */
enum gesture_direction gesture_detect(const struct touch_state *t,
                                      int screen_h,
                                      const struct zyl_config *cfg);

/*
 * Install default gesture handlers into server->gesture_handlers[].
 * Callers may override individual slots after this call.
 */
void gesture_init_handlers(struct zyl_server *server);

/*
 * Wire touch_down / touch_up / touch_motion listeners, cursor
 * listeners, seat listeners, and the new-input listener onto
 * the server.  Must be called after cursor, seat, and backend
 * are initialised.
 */
void gesture_register_listeners(struct zyl_server *server);

#endif /* ZYL_INPUT_GESTURE_H */
