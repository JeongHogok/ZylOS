#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 웨이크락 관리 — 획득, 해제, 자동 타임아웃
 * 수행범위: 웨이크락 슬롯 관리, 타임아웃 타이머, D-Bus 시그널
 * 의존방향: power_internal.h
 * SOLID: SRP — 웨이크락 생명주기만 담당
 * ────────────────────────────────────────────────────────── */

#include "power_internal.h"

/* ─── 웨이크락 타임아웃 타이머 콜백 ─── */
static gboolean on_wakelock_timeout(gpointer data) {
    WakelockTimeoutCtx *ctx = data;
    ZylPowerService    *svc = ctx->svc;
    const char         *tag = ctx->tag;

    g_warning("[Power] Wakelock timeout: %s — force releasing", tag);

    /* 타이머 슬롯 초기화 (이미 만료됐으므로 제거 불필요) */
    for (int i = 0; i < svc->wakelock_count; i++) {
        if (svc->wakelocks[i] && strcmp(svc->wakelocks[i], tag) == 0) {
            svc->wakelock_timers[i] = 0;
            break;
        }
    }

    /* D-Bus 시그널: WakelockExpired */
    if (svc->dbus) {
        g_dbus_connection_emit_signal(svc->dbus, NULL,
            ZYL_POWER_DBUS_PATH,
            ZYL_POWER_DBUS_NAME,
            "WakelockExpired",
            g_variant_new("(s)", tag),
            NULL);
    }

    /* 웨이크락 강제 해제 */
    zyl_power_release_wakelock(svc, tag);

    g_free(ctx);
    return G_SOURCE_REMOVE;
}

/* ─── 웨이크락 획득 ─── */
int zyl_power_acquire_wakelock(ZylPowerService *svc, const char *tag) {
    if (!svc || !tag || svc->wakelock_count >= MAX_WAKELOCKS) return -1;

    /* 중복 체크 */
    for (int i = 0; i < svc->wakelock_count; i++) {
        if (strcmp(svc->wakelocks[i], tag) == 0) return 0;
    }

    int slot = svc->wakelock_count;
    svc->wakelocks[slot] = g_strdup(tag);

    /* 웨이크락 최대 타임아웃 타이머 — 600초 후 자동 해제 + WakelockExpired 시그널 */
    WakelockTimeoutCtx *ctx = g_new(WakelockTimeoutCtx, 1);
    ctx->svc = svc;
    g_strlcpy(ctx->tag, tag, sizeof(ctx->tag));
    svc->wakelock_timers[slot] = g_timeout_add_seconds(WAKELOCK_TIMEOUT_SEC,
        on_wakelock_timeout, ctx);

    svc->wakelock_count++;
    g_message("[Power] Wakelock acquired: %s (total: %d, timeout: %ds)",
              tag, svc->wakelock_count, WAKELOCK_TIMEOUT_SEC);

    /* 서스펜드 타이머 취소 */
    if (svc->suspend_timer_id) {
        g_source_remove(svc->suspend_timer_id);
        svc->suspend_timer_id = 0;
    }

    return 0;
}

/* ─── 웨이크락 해제 ─── */
void zyl_power_release_wakelock(ZylPowerService *svc, const char *tag) {
    if (!svc || !tag) return;

    for (int i = 0; i < svc->wakelock_count; i++) {
        if (strcmp(svc->wakelocks[i], tag) == 0) {
            /* 타임아웃 타이머 취소 (정상 해제 시) */
            if (svc->wakelock_timers[i]) {
                g_source_remove(svc->wakelock_timers[i]);
                svc->wakelock_timers[i] = 0;
            }
            g_free(svc->wakelocks[i]);
            /* 마지막 요소로 교체 */
            int last = --svc->wakelock_count;
            svc->wakelocks[i]      = svc->wakelocks[last];
            svc->wakelock_timers[i] = svc->wakelock_timers[last];
            svc->wakelocks[last]      = NULL;
            svc->wakelock_timers[last] = 0;
            g_message("[Power] Wakelock released: %s (remaining: %d)", tag, svc->wakelock_count);
            return;
        }
    }
}

/* ─── 웨이크락 수 조회 ─── */
int zyl_power_get_wakelock_count(const ZylPowerService *svc) {
    return svc ? svc->wakelock_count : 0;
}
