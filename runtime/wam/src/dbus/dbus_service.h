/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: D-Bus 서비스 인터페이스 정의 — 메서드 핸들러 타입 및 등록 함수
 * 수행범위: WAM_DBUS_NAME/PATH 상수, BpiDbusMethod 타입, 서비스 등록/해제 함수 선언
 * 의존방향: gio/gio.h
 * SOLID: ISP — D-Bus 서비스 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef BPI_WAM_DBUS_SERVICE_H
#define BPI_WAM_DBUS_SERVICE_H

#include <gio/gio.h>

#define WAM_DBUS_NAME "org.bpios.WebAppManager"
#define WAM_DBUS_PATH "/org/bpios/WebAppManager"

/*
 * Signature for a single D-Bus method implementation.
 *
 * parameters  - the incoming GVariant tuple
 * invocation  - the pending invocation to return from
 * user_data   - opaque context (typically the BpiWam*)
 */
typedef void (*BpiDbusMethodFunc)(GVariant              *parameters,
                                  GDBusMethodInvocation *invocation,
                                  gpointer               user_data);

/* One entry in the dispatch table. */
typedef struct {
    const char        *name;   /* D-Bus method name */
    BpiDbusMethodFunc  func;   /* Implementation */
} BpiDbusMethodEntry;

/*
 * Start D-Bus service.
 *
 * dispatch_table      - NULL-terminated array of BpiDbusMethodEntry
 * user_data           - forwarded to every handler
 *
 * Returns the bus-owner ID (for g_bus_unown_name on shutdown).
 */
guint bpi_dbus_service_start(const BpiDbusMethodEntry *dispatch_table,
                             gpointer                  user_data);

#endif /* BPI_WAM_DBUS_SERVICE_H */
