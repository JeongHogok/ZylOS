#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Utility
 *
 * 역할: D-Bus 공통 유틸리티 구현 — 비동기/동기 호출, 연결 모니터링
 * 수행범위: g_dbus_connection_call 래핑, 에러 로깅, 타임아웃, 재연결
 * 의존방향: dbus_utils.h → gio/gio.h
 * SOLID: SRP — D-Bus 통신 유틸리티 구현만 담당
 * ────────────────────────────────────────────────────────── */

#include "dbus_utils.h"

#include <stdio.h>
#include <string.h>

/* ════════════════════════════════════════════════════════════════
 *  Internal: context for async call
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    ZylDbusReplyFn callback;
    gpointer       user_data;
    char          *bus_name;
    char          *method;
} AsyncCallCtx;

static void async_call_ctx_free(AsyncCallCtx *ctx) {
    if (!ctx) return;
    g_free(ctx->bus_name);
    g_free(ctx->method);
    g_free(ctx);
}

static void on_async_call_done(GObject      *source,
                               GAsyncResult *res,
                               gpointer      user_data) {
    AsyncCallCtx *ctx = user_data;
    GError *error = NULL;

    GVariant *result = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), res, &error);

    if (error) {
        g_warning("D-Bus async call failed [%s.%s]: %s",
                  ctx->bus_name ? ctx->bus_name : "unknown",
                  ctx->method  ? ctx->method  : "unknown",
                  error->message);
    }

    if (ctx->callback) {
        ctx->callback(result, error, ctx->user_data);
    }

    if (result) g_variant_unref(result);
    if (error)  g_error_free(error);
    async_call_ctx_free(ctx);
}

/* ════════════════════════════════════════════════════════════════
 *  Async D-Bus call
 * ════════════════════════════════════════════════════════════════ */

void zyl_dbus_call_async(GDBusConnection *conn,
                         const char *bus_name,
                         const char *path,
                         const char *iface,
                         const char *method,
                         GVariant *params,
                         int timeout_ms,
                         ZylDbusReplyFn callback,
                         gpointer data) {
    if (!conn || !bus_name || !path || !iface || !method) {
        g_warning("D-Bus async call: invalid arguments (NULL)");
        if (callback) {
            GError *err = g_error_new_literal(
                G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "NULL argument to zyl_dbus_call_async");
            callback(NULL, err, data);
            g_error_free(err);
        }
        return;
    }

    if (timeout_ms <= 0) {
        timeout_ms = ZYL_DBUS_DEFAULT_TIMEOUT_MS;
    }

    AsyncCallCtx *ctx = g_malloc(sizeof(*ctx));
    if (!ctx) {
        g_critical("D-Bus async call: allocation failed");
        return;
    }

    ctx->callback  = callback;
    ctx->user_data = data;
    ctx->bus_name  = g_strdup(bus_name);
    ctx->method    = g_strdup(method);

    if (!ctx->bus_name || !ctx->method) {
        g_critical("D-Bus async call: strdup allocation failed");
        async_call_ctx_free(ctx);
        return;
    }

    g_dbus_connection_call(conn,
                           bus_name,
                           path,
                           iface,
                           method,
                           params,
                           NULL,            /* reply type */
                           G_DBUS_CALL_FLAGS_NONE,
                           timeout_ms,
                           NULL,            /* cancellable */
                           on_async_call_done,
                           ctx);
}

/* ════════════════════════════════════════════════════════════════
 *  Synchronous safe call
 * ════════════════════════════════════════════════════════════════ */

GVariant *zyl_dbus_call_sync_safe(GDBusConnection *conn,
                                  const char *bus_name,
                                  const char *path,
                                  const char *iface,
                                  const char *method,
                                  GVariant *params,
                                  int timeout_ms) {
    if (!conn || !bus_name || !path || !iface || !method) {
        g_warning("D-Bus sync call: invalid arguments (NULL)");
        return NULL;
    }

    if (timeout_ms <= 0) {
        timeout_ms = ZYL_DBUS_DEFAULT_TIMEOUT_MS;
    }

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        conn,
        bus_name,
        path,
        iface,
        method,
        params,
        NULL,            /* reply type */
        G_DBUS_CALL_FLAGS_NONE,
        timeout_ms,
        NULL,            /* cancellable */
        &error);

    if (error) {
        g_warning("D-Bus sync call failed [%s.%s]: %s",
                  bus_name, method, error->message);
        g_error_free(error);
        return NULL;
    }

    return result;
}

/* ════════════════════════════════════════════════════════════════
 *  Connection health monitoring
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    ZylDbusDisconnectFn on_disconnect;
    gpointer            user_data;
} MonitorCtx;

static void on_connection_closed(GDBusConnection *conn,
                                 gboolean         remote_peer_vanished,
                                 GError          *error,
                                 gpointer         user_data) {
    MonitorCtx *ctx = user_data;
    (void)conn;

    if (remote_peer_vanished) {
        g_warning("D-Bus connection lost (peer vanished): %s",
                  error ? error->message : "unknown reason");
    } else {
        g_info("D-Bus connection closed locally");
    }

    if (ctx->on_disconnect) {
        ctx->on_disconnect(ctx->user_data);
    }
    /* ctx is freed when signal handler is disconnected or connection destroyed */
}

void zyl_dbus_monitor_connection(GDBusConnection *conn,
                                 ZylDbusDisconnectFn on_disconnect,
                                 gpointer data) {
    if (!conn) {
        g_warning("D-Bus monitor: NULL connection");
        return;
    }

    MonitorCtx *ctx = g_malloc(sizeof(*ctx));
    if (!ctx) {
        g_critical("D-Bus monitor: allocation failed");
        return;
    }

    ctx->on_disconnect = on_disconnect;
    ctx->user_data     = data;

    /* Do not exit on close — allow reconnection logic */
    g_dbus_connection_set_exit_on_close(conn, FALSE);

    g_signal_connect(conn, "closed",
                     G_CALLBACK(on_connection_closed), ctx);
}

/* ════════════════════════════════════════════════════════════════
 *  L3: D-Bus Rate Limiting
 *
 *  슬라이딩 윈도우 기반 호출 레이트 리밋.
 *  각 D-Bus sender별로 윈도우 내 호출 횟수를 추적하여,
 *  과도한 호출(알림 스팸, 서비스 남용 등)을 차단한다.
 * ════════════════════════════════════════════════════════════════ */

/* 호출 기록 엔트리 */
typedef struct {
    gint64  *timestamps;  /* 호출 타임스탬프 배열 (ring buffer) */
    int      capacity;
    int      count;
    int      head;        /* 다음 쓰기 위치 */
} RateLimitEntry;

static GHashTable *_rate_table = NULL;  /* sender → RateLimitEntry* */

static void rate_entry_free(gpointer data) {
    RateLimitEntry *entry = data;
    if (!entry) return;
    g_free(entry->timestamps);
    g_free(entry);
}

void zyl_dbus_rate_limit_init(void) {
    if (_rate_table) return;
    _rate_table = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, rate_entry_free);
}

void zyl_dbus_rate_limit_destroy(void) {
    if (_rate_table) {
        g_hash_table_destroy(_rate_table);
        _rate_table = NULL;
    }
}

gboolean zyl_dbus_rate_limit_check(const char *sender,
                                    int max_calls_per_window,
                                    int window_ms) {
    if (!_rate_table || !sender || max_calls_per_window <= 0 || window_ms <= 0)
        return TRUE;  /* 설정 오류 시 허용 */

    gint64 now = g_get_monotonic_time() / 1000;  /* microsec → ms */
    gint64 window_start = now - window_ms;

    RateLimitEntry *entry = g_hash_table_lookup(_rate_table, sender);

    if (!entry) {
        /* 새 sender — 엔트리 생성 */
        entry = g_new0(RateLimitEntry, 1);
        if (!entry) return TRUE;
        entry->capacity = max_calls_per_window + 1;
        entry->timestamps = g_new0(gint64, entry->capacity);
        if (!entry->timestamps) { g_free(entry); return TRUE; }
        entry->count = 0;
        entry->head = 0;
        g_hash_table_insert(_rate_table, g_strdup(sender), entry);
    }

    /* 윈도우 내 유효 호출 수 계산 */
    int valid_count = 0;
    for (int i = 0; i < entry->count && i < entry->capacity; i++) {
        if (entry->timestamps[i] >= window_start) {
            valid_count++;
        }
    }

    if (valid_count >= max_calls_per_window) {
        g_warning("[RateLimit] %s: %d calls in %dms window (limit: %d) — BLOCKED",
                  sender, valid_count, window_ms, max_calls_per_window);
        return FALSE;  /* 차단 */
    }

    /* 호출 기록 추가 (ring buffer) */
    entry->timestamps[entry->head] = now;
    entry->head = (entry->head + 1) % entry->capacity;
    if (entry->count < entry->capacity) entry->count++;

    return TRUE;  /* 허용 */
}

void zyl_dbus_rate_limit_cleanup(int max_age_ms) {
    if (!_rate_table) return;

    gint64 now = g_get_monotonic_time() / 1000;
    gint64 cutoff = now - max_age_ms;

    GHashTableIter iter;
    gpointer key, value;
    GList *to_remove = NULL;

    g_hash_table_iter_init(&iter, _rate_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        RateLimitEntry *entry = value;
        /* 모든 타임스탬프가 cutoff보다 오래되었으면 제거 */
        gboolean all_stale = TRUE;
        for (int i = 0; i < entry->count && i < entry->capacity; i++) {
            if (entry->timestamps[i] >= cutoff) {
                all_stale = FALSE;
                break;
            }
        }
        if (all_stale) {
            to_remove = g_list_prepend(to_remove, key);
        }
    }

    /* 안전한 제거 (iteration 외부) */
    for (GList *l = to_remove; l; l = l->next) {
        g_hash_table_remove(_rate_table, l->data);
    }
    g_list_free(to_remove);
}
