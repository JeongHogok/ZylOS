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

/* A second interface on the same bus name: the OS-owned web content view the
   browser drives (navigate/back/...) but never touches the content's DOM. */
#define WAM_WEBCONTENT_IFACE "org.zylos.WebContent"
#define WAM_WEBCONTENT_PATH  "/org/zylos/WebContent"

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
 * Start D-Bus service. Both tables share one bus name and one connection;
 * each is dispatched only for its own object path/interface.
 *
 * app_table         - WebAppManager methods (NULL-terminated), path WAM_DBUS_PATH
 * webcontent_table  - WebContent methods (NULL-terminated), path WAM_WEBCONTENT_PATH
 * user_data         - forwarded to every handler
 *
 * Returns the bus-owner ID (for g_bus_unown_name on shutdown).
 */
guint zyl_dbus_service_start(const ZylDbusMethodEntry *app_table,
                             const ZylDbusMethodEntry *webcontent_table,
                             gpointer                  user_data);

/* Emit a signal on the WebContent interface (WAM_WEBCONTENT_PATH). params is
   consumed (floating refs are sunk). No-op before the bus is acquired. */
void zyl_dbus_webcontent_emit(const char *signal_name, GVariant *params);

#endif /* ZYL_WAM_DBUS_SERVICE_H */
