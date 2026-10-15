/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - UseCase
 *
 * 역할: OOM Killer 구현 — 메모리 압박 감지 및 백그라운드 앱 종료
 * 수행범위: /proc/meminfo 주기적 모니터링,
 *           포그라운드 보호 LRU 종료, 상태 저장 알림(zyl:lowmemory)
 * 의존방향: oom.h → lifecycle.h → manifest.h
 * SOLID: SRP — 메모리 압박 정책만 담당, OCP — 임계값 상수로 확장
 * ────────────────────────────────────────────────────────── */

#include "oom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ZYL_USE_WEBKIT2GTK
#include <webkit2/webkit2.h>
#else
#include <webkit/webkit.h>
#endif

/* ════════════════════════════════════════════════════════════════
 *  /proc/meminfo 파싱
 * ════════════════════════════════════════════════════════════════ */

static bool read_meminfo(unsigned long *total_kb, unsigned long *avail_kb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return false;

    *total_kb = 0;
    *avail_kb = 0;
    char line[128];
    int  found = 0;

    while (fgets(line, sizeof(line), f) && found < 2) {
        if (sscanf(line, "MemTotal: %lu kB", total_kb) == 1)     found++;
        if (sscanf(line, "MemAvailable: %lu kB", avail_kb) == 1) found++;
    }
    fclose(f);
    return (*total_kb > 0);
}

/* ════════════════════════════════════════════════════════════════
 *  LRU 큐 관리
 * ════════════════════════════════════════════════════════════════ */

static void lru_touch(GQueue *q, const char *app_id) {
    if (!q || !app_id) return;
    for (GList *l = q->head; l; l = l->next) {
        if (g_strcmp0((const char *)l->data, app_id) == 0) {
            char *id = l->data;
            g_queue_delete_link(q, l);
            g_queue_push_tail(q, id);
            return;
        }
    }
    g_queue_push_tail(q, g_strdup(app_id));
}

static void lru_remove(GQueue *q, const char *app_id) {
    if (!q || !app_id) return;
    for (GList *l = q->head; l; l = l->next) {
        if (g_strcmp0((const char *)l->data, app_id) == 0) {
            g_free(l->data);
            g_queue_delete_link(q, l);
            return;
        }
    }
}

/* ════════════════════════════════════════════════════════════════
 *  앱에 lowmemory 이벤트 전달 (상태 저장 기회 제공)
 * ════════════════════════════════════════════════════════════════ */

static void notify_lowmemory(ZylAppInterface *iface, const char *app_id) {
    ZylAppInstance *inst = iface->get_instance(iface, app_id);
    if (!inst || !inst->webview_widget) return;

    const char *script =
        "document.dispatchEvent(new CustomEvent('zyl:lowmemory',"
        "{detail:{reason:'oom_killer'}}))";

    webkit_web_view_evaluate_javascript(
        WEBKIT_WEB_VIEW(inst->webview_widget),
        script, -1, NULL, NULL, NULL, NULL, NULL);

    g_message("[OOM] Sent zyl:lowmemory to %s", app_id);
}

/* ════════════════════════════════════════════════════════════════
 *  킬 대상 선정 — 포그라운드 보호 + LRU
 * ════════════════════════════════════════════════════════════════ */

/**
 * 가장 오래된 백그라운드 앱을 찾아 반환.
 * 포그라운드 앱은 건너뛴다. 반환된 문자열은 큐에서 제거되며 caller가 g_free.
 */
static char *select_kill_target(ZylOomKiller *oom) {
    if (g_queue_is_empty(oom->lru_order)) return NULL;

    /* LRU 순서로 순회하며 포그라운드가 아닌 첫 번째 앱 선택 */
    for (GList *l = oom->lru_order->head; l; l = l->next) {
        const char *id = (const char *)l->data;

        /* 포그라운드 앱 보호 */
        if (oom->foreground_id && g_strcmp0(id, oom->foreground_id) == 0)
            continue;

        /* suspended 앱 우선 종료 */
        ZylAppInstance *inst = oom->iface->get_instance(oom->iface, id);
        if (inst && inst->state == ZYL_APP_STATE_SUSPENDED) {
            char *target = l->data;
            g_queue_delete_link(oom->lru_order, l);
            return target;
        }
    }

    /* suspended 앱이 없으면 가장 오래된 비포그라운드 앱 */
    for (GList *l = oom->lru_order->head; l; l = l->next) {
        const char *id = (const char *)l->data;
        if (oom->foreground_id && g_strcmp0(id, oom->foreground_id) == 0)
            continue;

        char *target = l->data;
        g_queue_delete_link(oom->lru_order, l);
        return target;
    }

    return NULL;
}

/* ════════════════════════════════════════════════════════════════
 *  메모리 압박 체크 — 다단계 대응
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    ZylOomKiller    *oom;
    char            *app_id;
} OomKillCtx;

/* 상태 저장 대기 후 실제 종료 */
static gboolean kill_after_grace(gpointer data) {
    OomKillCtx *ctx = data;

    g_warning("[OOM] Killing app: %s (after grace period)", ctx->app_id);
    zyl_lifecycle_close(ctx->oom->iface, ctx->app_id);

    g_free(ctx->app_id);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

void zyl_oom_check_pressure(ZylOomKiller *oom) {
    if (!oom) return;

    unsigned long total_kb = 0, avail_kb = 0;
    if (!read_meminfo(&total_kb, &avail_kb) || total_kb == 0) return;

    int pct = (int)((avail_kb * 100UL) / total_kb);

    if (pct < ZYL_OOM_CRITICAL_PCT) {
        /* ═══ 임계(< 10%): 즉시 종료 — 상태 저장 대기 없음 ═══ */
        char *target = select_kill_target(oom);
        if (target) {
            g_warning("[OOM] CRITICAL: MemAvailable %d%% < %d%% — "
                      "killing %s immediately",
                      pct, ZYL_OOM_CRITICAL_PCT, target);
            zyl_lifecycle_close(oom->iface, target);
            g_free(target);
        } else {
            g_warning("[OOM] CRITICAL: MemAvailable %d%% — "
                      "no killable app (foreground only)", pct);
        }

    } else if (pct < ZYL_OOM_MODERATE_PCT) {
        /* ═══ 보통(< 15%): lowmemory 알림 → grace 후 종료 ═══ */
        char *target = select_kill_target(oom);
        if (target) {
            g_warning("[OOM] MODERATE: MemAvailable %d%% < %d%% — "
                      "notifying %s, will kill in %d ms",
                      pct, ZYL_OOM_MODERATE_PCT, target,
                      ZYL_OOM_SAVE_GRACE_MS);

            notify_lowmemory(oom->iface, target);

            OomKillCtx *ctx = g_new(OomKillCtx, 1);
            ctx->oom    = oom;
            ctx->app_id = target; /* ownership transferred */
            g_timeout_add(ZYL_OOM_SAVE_GRACE_MS, kill_after_grace, ctx);
        }

    } else if (pct < ZYL_OOM_WARNING_PCT) {
        /* ═══ 경고(< 25%): 로그만 ═══ */
        g_info("[OOM] WARNING: MemAvailable %d%% < %d%%",
               pct, ZYL_OOM_WARNING_PCT);
    }
}

/* ════════════════════════════════════════════════════════════════
 *  주기적 타이머 콜백
 * ════════════════════════════════════════════════════════════════ */

static gboolean poll_memory_pressure(gpointer data) {
    ZylOomKiller *oom = data;
    zyl_oom_check_pressure(oom);
    return G_SOURCE_CONTINUE; /* 타이머 계속 유지 */
}

/* ════════════════════════════════════════════════════════════════
 *  공개 API
 * ════════════════════════════════════════════════════════════════ */

ZylOomKiller *zyl_oom_init(ZylAppInterface *iface) {
    if (!iface) return NULL;

    ZylOomKiller *oom = g_new0(ZylOomKiller, 1);
    oom->iface           = iface;
    oom->lru_order       = g_queue_new();
    oom->foreground_id   = NULL;

    /* 주기적 메모리 압박 모니터링 시작 */
    oom->poll_source_id = g_timeout_add(ZYL_OOM_POLL_INTERVAL_MS,
                                         poll_memory_pressure, oom);

    g_message("[OOM] Initialized (poll: %d ms)", ZYL_OOM_POLL_INTERVAL_MS);
    return oom;
}

void zyl_oom_destroy(ZylOomKiller *oom) {
    if (!oom) return;

    if (oom->poll_source_id > 0)
        g_source_remove(oom->poll_source_id);

    if (oom->lru_order)
        g_queue_free_full(oom->lru_order, g_free);

    g_free(oom->foreground_id);
    g_free(oom);

    g_message("[OOM] Destroyed");
}

void zyl_oom_on_app_launched(ZylOomKiller *oom, const char *app_id) {
    if (!oom || !app_id) return;

    lru_touch(oom->lru_order, app_id);
}

void zyl_oom_on_app_closed(ZylOomKiller *oom, const char *app_id) {
    if (!oom || !app_id) return;

    lru_remove(oom->lru_order, app_id);

    /* 포그라운드 앱이 종료되면 포그라운드 해제 */
    if (oom->foreground_id && g_strcmp0(oom->foreground_id, app_id) == 0) {
        g_free(oom->foreground_id);
        oom->foreground_id = NULL;
    }
}

void zyl_oom_on_app_foreground(ZylOomKiller *oom, const char *app_id) {
    if (!oom || !app_id) return;

    g_free(oom->foreground_id);
    oom->foreground_id = g_strdup(app_id);

    lru_touch(oom->lru_order, app_id);
}
