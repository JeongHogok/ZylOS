/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Interface
 *
 * 역할: 컴포지터 → OS 서비스 알림 인터페이스 (시스템 버스)
 * 수행범위: 제스처 → WAM 내비게이션(zyl_server::gesture_signal_fn 에 꽂는 함수),
 *           사용자 입력 → 전원 서비스의 사용자 활동, 화면이 켜져 있는지(전원 서비스의
 *           StateChanged — 꺼진 화면의 터치·포인터는 앱에 가지 않는다)
 * 의존방향: zyl_compositor.h
 * SOLID: DIP — 제스처 코드는 함수 포인터만 안다
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_IPC_OS_BUS_H
#define ZYL_IPC_OS_BUS_H

#include "zyl_compositor.h"

/* GoHome / GoBack / SwitchApp / OpenPanel / ShowRecents →
   org.zylos.WebAppManager; other signals stay in the log. What actually comes to the front is the WAM's shell policy —
   the compositor never raises an app itself (it would raise one over the
   lock screen). */
void zyl_wam_nav_send(struct zyl_server *server, const char *signal_name,
                      const char *detail);

/* A touch, click or key: the screen is in use, so it must not dim or go off
   (org.zylos.PowerManager.UserActivity — at most once a second) */
void zyl_os_user_activity(void);

/* Starts following the power service's screen state (its own thread and GLib
   context: the compositor runs a Wayland loop, not a GLib one) */
void zyl_os_bus_watch_screen(void);

/* Whether the screen is on (on until the power service says otherwise).
   A touch on a dark screen is a pocket, not the user: it reaches no app */
bool zyl_os_screen_on(void);

#endif /* ZYL_IPC_OS_BUS_H */
