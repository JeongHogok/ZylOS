#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 디스플레이 상태 관리 — 화면 밝기, 타이머, 전원 상태 전환
 * 수행범위: 화면 on/off, dim 타이머, idle 타이머 리셋, 상태 전환
 * 의존방향: power_internal.h
 * SOLID: SRP — 디스플레이 전원 상태만 담당
 * ────────────────────────────────────────────────────────── */

#include "power_internal.h"

/* Static wrapper so zyl_power_request_screen_off can be used as GSourceFunc */
static gboolean screen_off_source_func(gpointer data) {
    zyl_power_request_screen_off((ZylPowerService *)data);
    return G_SOURCE_REMOVE;
}

/* ─── 상태 전환 (내부) ─── */
void transition_state(ZylPowerService *svc, ZylPowerState new_state) {
    if (svc->state == new_state) return;
    ZylPowerState old = svc->state;
    svc->state = new_state;

    g_message("[Power] %d → %d", old, new_state);

    if (svc->state_cb) {
        svc->state_cb(old, new_state, svc->state_cb_data);
    }

    /* D-Bus 시그널: StateChanged */
    if (svc->dbus) {
        g_dbus_connection_emit_signal(svc->dbus, NULL,
            ZYL_POWER_DBUS_PATH,
            ZYL_POWER_DBUS_NAME,
            "StateChanged",
            g_variant_new("(ii)", (int)old, (int)new_state),
            NULL);
    }
}

/* ─── 타이머 콜백: 화면 어둡게 ─── */
static gboolean on_dim_timeout(gpointer data) {
    ZylPowerService *svc = data;
    svc->dim_timer_id = 0;

    if (svc->state != ZYL_POWER_STATE_ACTIVE) return G_SOURCE_REMOVE;
    if (svc->wakelock_count > 0) return G_SOURCE_REMOVE;

    /* 밝기를 현재의 30%로 낮춤 */
    int dim_brightness = svc->brightness * 30 / 100;
    if (dim_brightness < 5) dim_brightness = 5;
    zyl_power_set_brightness(svc, dim_brightness);
    transition_state(svc, ZYL_POWER_STATE_DIM);

    /* screen_off 타이머 시작 */
    int remain = svc->config.screen_timeout_sec - svc->config.dim_timeout_sec;
    if (remain > 0) {
        svc->screen_off_timer_id = g_timeout_add_seconds(remain,
            screen_off_source_func, svc);
    }

    return G_SOURCE_REMOVE;
}

/* ─── 타이머 리셋 (사용자 활동 시 호출) ─── */
void reset_idle_timers(ZylPowerService *svc) {
    /* 기존 타이머 제거 */
    if (svc->dim_timer_id) { g_source_remove(svc->dim_timer_id); svc->dim_timer_id = 0; }
    if (svc->screen_off_timer_id) { g_source_remove(svc->screen_off_timer_id); svc->screen_off_timer_id = 0; }
    if (svc->suspend_timer_id) { g_source_remove(svc->suspend_timer_id); svc->suspend_timer_id = 0; }

    if (svc->config.screen_timeout_sec <= 0) return;

    /* dim 타이머 시작 */
    int dim_sec = svc->config.dim_timeout_sec;
    if (dim_sec <= 0) dim_sec = svc->config.screen_timeout_sec - 5;
    if (dim_sec < 5) dim_sec = 5;

    svc->dim_timer_id = g_timeout_add_seconds(dim_sec, on_dim_timeout, svc);
}

/* ─── 화면 끄기 ─── */
int zyl_power_request_screen_off(ZylPowerService *svc) {
    if (!svc) return -1;

    /* Save current brightness before turning off, so screen_on can restore it */
    if (svc->brightness > 0) {
        svc->saved_brightness = svc->brightness;
    }
    zyl_power_set_brightness(svc, 0);
    transition_state(svc, ZYL_POWER_STATE_SCREEN_OFF);

    /* Cancel any existing suspend timer before starting a new one */
    if (svc->suspend_timer_id) {
        g_source_remove(svc->suspend_timer_id);
        svc->suspend_timer_id = 0;
    }

    /* Doze → Suspend 단계적 전력 절감 */
    if (svc->wakelock_count == 0) {
        if (svc->config.doze_enabled) {
            /* Screen off → Doze 전환 (5분 후) */
            svc->suspend_timer_id = g_timeout_add_seconds(
                300, /* 5 minutes to doze */
                (GSourceFunc)zyl_power_enter_doze, svc);
        } else if (svc->config.auto_suspend) {
            svc->suspend_timer_id = g_timeout_add_seconds(
                svc->config.suspend_delay_sec,
                (GSourceFunc)zyl_power_request_suspend, svc);
        }
    }
    return 0;
}

/* ─── 화면 켜기 ─── */
int zyl_power_request_screen_on(ZylPowerService *svc) {
    if (!svc) return -1;
    /* Exit doze if in doze state */
    if (svc->state == ZYL_POWER_STATE_DOZE) exit_doze(svc);
    /* Restore saved brightness (brightness is 0 after screen_off) */
    int restore = (svc->saved_brightness > 0) ? svc->saved_brightness : 80;
    zyl_power_set_brightness(svc, restore);
    transition_state(svc, ZYL_POWER_STATE_ACTIVE);
    reset_idle_timers(svc);
    return 0;
}
