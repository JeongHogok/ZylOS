/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: D-Bus 서비스 인터페이스 정의 — 메서드 핸들러 타입 및 등록 함수
 * 수행범위: WAM_DBUS_NAME/PATH 상수, ZylDbusMethod 타입, 서비스 등록/해제 함수 선언
 * 의존방향: gio/gio.h
 * SOLID: ISP — D-Bus 서비스 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_DBUS_SERVICE_H
#define ZYL_WAM_DBUS_SERVICE_H

#include <gio/gio.h>

#define WAM_DBUS_NAME "org.zylos.WebAppManager"
#define WAM_DBUS_PATH "/org/zylos/WebAppManager"

/*
 * Signature for a single D-Bus method implementation.
 *
 * parameters  - the incoming GVariant tuple
 * invocation  - the pending invocation to return from
 * user_data   - opaque context (typically the ZylWam*)
 */
typedef void (*ZylDbusMethodFunc)(GVariant              *parameters,
                                  GDBusMethodInvocation *invocation,
                                  gpointer               user_data);

/* One entry in the dispatch table. */
typedef struct {
    const char        *name;   /* D-Bus method name */
    ZylDbusMethodFunc  func;   /* Implementation */
} ZylDbusMethodEntry;

/*
 * Start D-Bus service.
 *
 * dispatch_table      - NULL-terminated array of ZylDbusMethodEntry
 * user_data           - forwarded to every handler
 *
 * Returns the bus-owner ID (for g_bus_unown_name on shutdown).
 */
guint zyl_dbus_service_start(const ZylDbusMethodEntry *dispatch_table,
                             gpointer                  user_data);

#endif /* ZYL_WAM_DBUS_SERVICE_H */
