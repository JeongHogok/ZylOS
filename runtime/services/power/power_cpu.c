#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: CPU 거버너 제어 및 Doze 모드 관리
 * 수행범위: CPU 코어 온/오프라인, 거버너 전환, Doze 진입/해제
 * 의존방향: power_internal.h, HAL CPU
 * SOLID: SRP — CPU 절전 제어만 담당
 * ────────────────────────────────────────────────────────── */

#include "power_internal.h"

/* ─── Doze 모드 진입 ─── */
gboolean zyl_power_enter_doze(gpointer data) {
    ZylPowerService *svc = data;
    if (!svc || svc->state != ZYL_POWER_STATE_SCREEN_OFF) return G_SOURCE_REMOVE;
    if (svc->wakelock_count > 0) return G_SOURCE_REMOVE; /* 웨이크락 보유 → doze 안 함 */

    transition_state(svc, ZYL_POWER_STATE_DOZE);
    g_message("[Power] Entered DOZE mode — network restricted, alarms only");

    /* CPU 절전 거버너 전환 */
    zyl_cpu_set_governor("powersave");

    /* 비활성 코어 오프라인 (코어 4~7) */
    int ncores = zyl_cpu_get_core_count();
    for (int i = ncores / 2; i < ncores; i++) {
        zyl_cpu_set_core_online(i, 0);
    }

    /* Doze → Deep Sleep 전환 타이머 (30분) */
    if (svc->config.auto_suspend) {
        /* Cancel existing timer to prevent leak */
        if (svc->suspend_timer_id) {
            g_source_remove(svc->suspend_timer_id);
            svc->suspend_timer_id = 0;
        }
        svc->suspend_timer_id = g_timeout_add_seconds(
            1800, /* 30 minutes in doze → suspend */
            (GSourceFunc)zyl_power_request_suspend, svc);
    }

    return G_SOURCE_REMOVE;
}

/* ─── Doze 해제 — 화면 켜질 때 호출 ─── */
void exit_doze(ZylPowerService *svc) {
    if (!svc || svc->state != ZYL_POWER_STATE_DOZE) return;

    /* 코어 다시 온라인 */
    int ncores = zyl_cpu_get_core_count();
    for (int i = 1; i < ncores; i++) {
        zyl_cpu_set_core_online(i, 1);
    }

    /* 거버너 복원 */
    int profile = zyl_cpu_get_power_profile();
    zyl_cpu_set_power_profile(profile);

    g_message("[Power] Exited DOZE mode — all cores online");
}
