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
