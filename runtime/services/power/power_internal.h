/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Internal Header
 *
 * 역할: 전력 관리 서비스 내부 공유 헤더 — 구조체, 상수, 유틸리티
 * 수행범위: 서브시스템 간 공유 타입·함수 선언
 * 의존방향: power.h, gio/gio.h, hal.h (Display·CPU·Battery HAL)
 * SOLID: ISP — 내부 서브시스템에만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_POWER_INTERNAL_H
#define ZYL_POWER_INTERNAL_H

#include "power.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <gio/gio.h>
#include "hal.h"

/* ─── 내부 상수 ─── */
#define ACTIVE_GOVERNOR      "schedutil"   /* awake: the scheduler drives the clock */
#define MAX_WAKELOCKS        32
#define WAKELOCK_TIMEOUT_SEC 600


/* ─── 내부 구조체 ─── */
struct ZylPowerService {
    ZylPowerState state;
    ZylPowerConfig config;
    int brightness;               /* the user's level 0-100; dimming and screen-off
                                     change only what the backlight shows */

    /* 웨이크락 */
    char *wakelocks[MAX_WAKELOCKS];
    int wakelock_count;
    /* 웨이크락 타임아웃 타이머 — 최대 600초 후 자동 release */
    guint wakelock_timers[MAX_WAKELOCKS];

    /* 타이머 */
    guint dim_timer_id;
    guint screen_off_timer_id;
    guint suspend_timer_id;

    /* 콜백 */
    zyl_power_state_fn state_cb;
    void *state_cb_data;
    zyl_wake_fn wake_cb;
    void *wake_cb_data;

    /* D-Bus */
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* ─── 웨이크락 타임아웃 컨텍스트 ─── */
typedef struct {
    ZylPowerService *svc;
    char             tag[128];
} WakelockTimeoutCtx;

/* ─── 상태 전환 (여러 서브시스템에서 사용) ─── */
void transition_state(ZylPowerService *svc, ZylPowerState new_state);

/* ─── 디스플레이 서브시스템 ─── */
void reset_idle_timers(ZylPowerService *svc);
void apply_brightness(ZylPowerService *svc, int level);   /* the backlight only */

/* ─── CPU/Doze 서브시스템 ─── */
gboolean zyl_power_enter_doze(gpointer data);
void     exit_doze(ZylPowerService *svc);

#endif /* ZYL_POWER_INTERNAL_H */
