/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Interface
 *
 * 역할: 제스처 → WAM 내비게이션 전달 인터페이스
 * 수행범위: zyl_server::gesture_signal_fn 에 꽂는 송신 함수
 * 의존방향: zyl_compositor.h
 * SOLID: DIP — 제스처 코드는 함수 포인터만 안다
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_IPC_WAM_NAV_H
#define ZYL_IPC_WAM_NAV_H

#include "zyl_compositor.h"

/* GoHome / GoBack / SwitchApp → org.zylos.WebAppManager; other signals stay
   in the log. What actually comes to the front is the WAM's shell policy —
   the compositor never raises an app itself (it would raise one over the
   lock screen). */
void zyl_wam_nav_send(struct zyl_server *server, const char *signal_name,
                      const char *detail);

#endif /* ZYL_IPC_WAM_NAV_H */
