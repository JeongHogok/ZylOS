/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Utility
 *
 * 역할: D-Bus 공통 유틸리티 — 비동기 호출, 타임아웃, 연결 모니터링
 * 수행범위: D-Bus 메서드 호출 래핑, 에러 처리, 자동 재연결, 로깅
 * 의존방향: gio/gio.h (GLib D-Bus)
 * SOLID: SRP — D-Bus 통신 유틸리티만 담당
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_DBUS_UTILS_H
#define ZYL_DBUS_UTILS_H

#include <gio/gio.h>

/* ─── Default timeout (ms) ─── */
#define ZYL_DBUS_DEFAULT_TIMEOUT_MS 5000

/* ─── Async reply callback ─── */
typedef void (*ZylDbusReplyFn)(GVariant *result, GError *error, gpointer data);

/**
 * Async D-Bus method call with timeout.
 * params may be NULL. timeout_ms <= 0 uses default (5000ms).
 */
void zyl_dbus_call_async(GDBusConnection *conn,
                         const char *bus_name,
                         const char *path,
                         const char *iface,
                         const char *method,
                         GVariant *params,
                         int timeout_ms,
                         ZylDbusReplyFn callback,
                         gpointer data);

/* ─── Connection health monitoring ─── */
typedef void (*ZylDbusDisconnectFn)(gpointer data);

/**
 * Monitor a D-Bus connection for disconnection.
 * Calls on_disconnect when the connection is lost.
 */
void zyl_dbus_monitor_connection(GDBusConnection *conn,
                                 ZylDbusDisconnectFn on_disconnect,
                                 gpointer data);

/**
 * Safe synchronous D-Bus method call with error handling.
 * Returns NULL on error (error is logged). Caller owns the returned GVariant.
 * timeout_ms <= 0 uses default (5000ms).
 */
GVariant *zyl_dbus_call_sync_safe(GDBusConnection *conn,
                                  const char *bus_name,
                                  const char *path,
                                  const char *iface,
                                  const char *method,
                                  GVariant *params,
                                  int timeout_ms);

/* ─── Rate Limiting ─── */

/**
 * Per-sender rate limiter for D-Bus method calls.
 * Tracks call counts per unique sender within a sliding time window.
 * Returns true if the call should be ALLOWED, false if rate-limited.
 *
 * max_calls_per_window: maximum allowed calls per sender
 * window_ms: sliding window duration in milliseconds
 *
 * Usage in D-Bus method handler:
 *   if (!zyl_dbus_rate_limit_check(sender, 60, 10000)) {
 *       g_dbus_method_invocation_return_error(..., "Rate limited");
 *       return;
 *   }
 */
gboolean zyl_dbus_rate_limit_check(const char *sender,
                                    int max_calls_per_window,
                                    int window_ms);

/**
 * Clean up stale rate limit entries (call periodically).
 * Removes entries older than max_age_ms.
 */
void zyl_dbus_rate_limit_cleanup(int max_age_ms);

/**
 * Initialize the rate limiter. Must be called once at startup.
 */
void zyl_dbus_rate_limit_init(void);

/**
 * Destroy the rate limiter. Call at shutdown.
 */
void zyl_dbus_rate_limit_destroy(void);

#endif /* ZYL_DBUS_UTILS_H */
