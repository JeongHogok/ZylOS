#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: CPU 거버너 제어 및 Doze 모드 관리
 * 수행범위: CPU 코어 온/오프라인, 거버너 전환, Doze 진입/해제
 * 의존방향: power_internal.h, hal.h (CPU HAL)
 * SOLID: SRP — CPU 절전 제어만 담당
 * ────────────────────────────────────────────────────────── */

#include "power_internal.h"

/* The suspend timer firing: it is done, then suspend */
gboolean zyl_power_suspend_timeout(gpointer data) {
    ((ZylPowerService *)data)->suspend_timer_id = 0;
    zyl_power_request_suspend((ZylPowerService *)data);
    return G_SOURCE_REMOVE;
}

/* ─── Doze 모드 진입 ─── */
gboolean zyl_power_enter_doze(gpointer data) {
    ZylPowerService *svc = data;
    if (!svc) return G_SOURCE_REMOVE;
    svc->suspend_timer_id = 0;                           /* the doze timer: this call */
    if (svc->state != ZYL_POWER_STATE_SCREEN_OFF) return G_SOURCE_REMOVE;
    if (svc->wakelock_count > 0) return G_SOURCE_REMOVE; /* 웨이크락 보유 → doze 안 함 */

    transition_state(svc, ZYL_POWER_STATE_DOZE);
    g_message("[Power] Entered DOZE mode — network restricted, alarms only");

    /* CPU 절전 거버너 전환 */
    const ZylCpuHal *cpu = zyl_hal_cpu();
    cpu->set_governor("powersave");

    /* 비활성 코어 오프라인 (상위 절반) */
    int ncores = cpu->core_count();
    if (ncores > 1) {  /* guard: need at least 2 cores; core 0 stays online */
        int half = ncores / 2;
        if (half < 1) half = 1;
        for (int i = half; i < ncores; i++) {
            cpu->set_core_online(i, false);
        }
    }

    /* Doze → Deep Sleep 전환 타이머 (30분) */
    if (svc->config.auto_suspend) {
        svc->suspend_timer_id = g_timeout_add_seconds(
            1800, /* 30 minutes in doze → suspend */
            zyl_power_suspend_timeout, svc);
    }

    return G_SOURCE_REMOVE;
}

/* ─── Doze 해제 — 화면 켜질 때 호출 ─── */
void exit_doze(ZylPowerService *svc) {
    if (!svc || svc->state != ZYL_POWER_STATE_DOZE) return;

    /* 코어 다시 온라인 */
    const ZylCpuHal *cpu = zyl_hal_cpu();
    int ncores = cpu->core_count();
    for (int i = 1; i < ncores; i++) cpu->set_core_online(i, true);

    /* Awake again: the scheduler-driven governor */
    cpu->set_governor(ACTIVE_GOVERNOR);

    g_message("[Power] Exited DOZE mode — all cores online");
}
