/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Adapter
 *
 * 역할: D-Bus 메서드 디스패칭 — 테이블 기반 메서드 라우팅
 * 수행범위: 인트로스펙션 XML 정의, 버스 이름 소유, 메서드 호출 라우팅
 * 의존방향: dbus_service.h
 * SOLID: OCP — 테이블 기반 디스패치로 새 메서드 추가 시 기존 코드 수정 불필요
 * ────────────────────────────────────────────────────────── */

#include "dbus_service.h"

#include <string.h>

/* ─── Introspection XML ─── */
static const char *introspection_xml =
    "<node>"
    "  <interface name='" WAM_DBUS_NAME "'>"
    "    <method name='Launch'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Close'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "    </method>"
    "    <method name='Suspend'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "    </method>"
    "    <method name='Resume'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "    </method>"
    "    <method name='ListApps'>"
    "      <arg type='as' name='app_ids' direction='out'/>"
    "    </method>"
    "    <method name='ListRunning'>"
    "      <arg type='as' name='app_ids' direction='out'/>"
    "      <arg type='i' name='count' direction='out'/>"
    "    </method>"
    "    <signal name='AppLaunched'>"
    "      <arg type='s' name='app_id'/>"
    "    </signal>"
    "    <signal name='AppClosed'>"
    "      <arg type='s' name='app_id'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── Module-level state ─── */
typedef struct {
    const ZylDbusMethodEntry *table;
    gpointer                  user_data;
    GDBusConnection          *connection;
} DbusCtx;

static DbusCtx g_ctx;

/* ─── Dispatch via table lookup ─── */
static void handle_method_call(GDBusConnection       *connection,
                                const gchar           *sender,
                                const gchar           *object_path,
                                const gchar           *interface_name,
                                const gchar           *method_name,
                                GVariant              *parameters,
                                GDBusMethodInvocation *invocation,
                                gpointer               user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name;

    DbusCtx *ctx = user_data;

    for (const ZylDbusMethodEntry *e = ctx->table; e->name != NULL; e++) {
        if (g_strcmp0(method_name, e->name) == 0) {
            e->func(parameters, invocation, ctx->user_data);
            return;
        }
    }

    g_dbus_method_invocation_return_error(invocation,
        G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
        "Unknown method: %s", method_name);
}

static const GDBusInterfaceVTable vtable = {
    .method_call = handle_method_call,
};

/* ─── Bus acquired callback ─── */
static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data) {
    DbusCtx *ctx = user_data;
    ctx->connection = connection;

    GDBusNodeInfo *node_info =
        g_dbus_node_info_new_for_xml(introspection_xml, NULL);

    g_dbus_connection_register_object(connection,
        WAM_DBUS_PATH,
        node_info->interfaces[0],
        &vtable,
        ctx, NULL, NULL);

    g_dbus_node_info_unref(node_info);
    g_message("D-Bus registered: %s", WAM_DBUS_NAME);
}

static void on_name_acquired(GDBusConnection *conn, const gchar *name,
                              gpointer data) {
    (void)conn; (void)data;
    g_message("D-Bus name acquired: %s", name);
}

static void on_name_lost(GDBusConnection *conn, const gchar *name,
                          gpointer data) {
    (void)conn; (void)data;
    g_warning("D-Bus name lost: %s", name);
}

/* ─── Public: start the service ─── */
guint zyl_dbus_service_start(const ZylDbusMethodEntry *dispatch_table,
                             gpointer                  user_data) {
    g_ctx.table     = dispatch_table;
    g_ctx.user_data = user_data;

    return g_bus_own_name(G_BUS_TYPE_SESSION,
        WAM_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, on_name_acquired, on_name_lost,
        &g_ctx, NULL);
}
