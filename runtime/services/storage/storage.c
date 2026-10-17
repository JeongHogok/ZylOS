/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 저장공간 서비스 (org.zylos.Storage) — 설정·파일 앱의 용량 표시, 복구 화면의
 *       기기 초기화
 * 수행범위: 사용자 데이터 파티션의 전체·사용·남은 바이트와 사용률(Storage HAL).
 *           사람이 읽는 단위로 바꾸는 것은 앱(zylI18n.formatBytes) 몫.
 *           EraseUserData: 복구 부팅(runtime/lib/recovery.h)에서만
 *           zyl-erase-data.service 를 시작한다 — 올라 있는 /data 는 지우지 않는다
 * 의존방향: hal.h (Storage HAL), recovery.h, gio/gio.h (D-Bus, systemd)
 * SOLID: SRP — 사용자 데이터 파티션의 용량과 지우기만
 * ────────────────────────────────────────────────────────── */

#include "hal.h"
#include "recovery.h"

#include <gio/gio.h>
#include <glib-unix.h>

static const char *storage_xml =
    "<node>"
    "  <interface name='org.zylos.Storage'>"
    "    <method name='GetUsage'>"
    "      <arg type='a{sv}' name='usage' direction='out'/>"
    "    </method>"
    "    <method name='EraseUserData'>"
    "      <arg type='b' name='started' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void on_erase_started(GObject *src, GAsyncResult *res, gpointer inv) {
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", r != NULL));
    if (r) g_variant_unref(r);
}

/* The recovery screen's factory reset: the unit erases the partition and
   restarts. Only on a recovery boot — never a mounted /data */
static void erase_user_data(GDBusConnection *c, GDBusMethodInvocation *inv) {
    if (!zyl_recovery_mode()) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
        return;
    }
    g_dbus_connection_call(c, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit",
        g_variant_new("(ss)", "zyl-erase-data.service", "replace"), G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, on_erase_started, inv);
}

/* Same fields as the emulator's storage service: { total, used, available, percent } */
static void handle(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)sender; (void)path; (void)iface; (void)params; (void)data;
    if (g_strcmp0(method, "EraseUserData") == 0) {
        erase_user_data(c, inv);
        return;
    }
    if (g_strcmp0(method, "GetUsage") != 0) return;
    ZylStorageState st = {0};
    bool ok = zyl_hal_storage()->get(&st);
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "total", g_variant_new_uint64(st.total_bytes));
    g_variant_builder_add(&d, "{sv}", "used", g_variant_new_uint64(st.used_bytes));
    g_variant_builder_add(&d, "{sv}", "available", g_variant_new_uint64(st.available_bytes));
    g_variant_builder_add(&d, "{sv}", "percent", g_variant_new_int32(
        ok && st.total_bytes ? (int)(st.used_bytes * 100 / st.total_bytes) : 0));
    g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(storage_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/Storage", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

#ifndef ZYL_TEST
static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.Storage", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
