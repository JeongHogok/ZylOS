/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 블루투스 서비스 (org.zylos.BluetoothService) — D-Bus
 * 수행범위: 이 기기에 어댑터가 있는지 보고(없으면 앱이 비활성), 전원·상태,
 *           검색, 기기 목록, 페어링·연결·해제·삭제
 * 의존방향: bluetooth.h, hal.h (Bluetooth HAL), gio/gio.h (D-Bus)
 * SOLID: SRP — D-Bus 노출만. DIP — 장치는 ZylBtHal 로만
 * ────────────────────────────────────────────────────────── */

#include "bluetooth.h"
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib-unix.h>

#define MAX_DEVICES 64

struct ZylBluetoothService {
    guint dbus_owner_id;
};

static const char *bt_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_BT_DBUS_NAME "'>"
    "    <method name='GetCapabilities'>"
    "      <arg type='a{sv}' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='GetState'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <method name='SetEnabled'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <method name='StartScan'>"
    "      <arg type='b' name='started' direction='out'/>"
    "    </method>"
    "    <method name='StopScan'/>"
    "    <method name='GetDevices'>"
    "      <arg type='aa{sv}' name='devices' direction='out'/>"
    "    </method>"
    "    <method name='Pair'>"
    "      <arg type='s' name='address' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Connect'>"
    "      <arg type='s' name='address' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Disconnect'>"
    "      <arg type='s' name='address' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Remove'>"
    "      <arg type='s' name='address' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static GVariant *one_bool(const char *key, bool v) {
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", key, g_variant_new_boolean(v));
    return g_variant_new("(a{sv})", &d);
}

/* Same fields as the emulator: { name, address, deviceType, paired, connected, rssi } */
static GVariant *devices_variant(void) {
    ZylBtDevice devs[MAX_DEVICES];
    int n = zyl_hal_bt()->list_devices(devs, MAX_DEVICES);
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    for (int i = 0; i < n; i++) {
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        /* Any device in range picks its name and the HAL keeps the first bytes:
           the cut can fall inside a character, and D-Bus strings must be UTF-8 */
        g_variant_builder_add(&d, "{sv}", "name", g_variant_new_take_string(g_utf8_make_valid(devs[i].name, -1)));
        g_variant_builder_add(&d, "{sv}", "address", g_variant_new_string(devs[i].address));
        g_variant_builder_add(&d, "{sv}", "deviceType", g_variant_new_string(devs[i].type));
        g_variant_builder_add(&d, "{sv}", "paired", g_variant_new_boolean(devs[i].paired));
        g_variant_builder_add(&d, "{sv}", "connected", g_variant_new_boolean(devs[i].connected));
        g_variant_builder_add(&d, "{sv}", "rssi", g_variant_new_int32(devs[i].rssi));
        g_variant_builder_add_value(&list, g_variant_builder_end(&d));
    }
    return g_variant_new("(aa{sv})", &list);
}

static void handle_bt_method(GDBusConnection *conn, const gchar *sender,
                              const gchar *path, const gchar *iface,
                              const gchar *method, GVariant *params,
                              GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    const ZylBtHal *bt = zyl_hal_bt();
    const gchar *addr = NULL;
    if (g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) g_variant_get(params, "(&s)", &addr);

    if (g_strcmp0(method, "GetCapabilities") == 0) {
        g_dbus_method_invocation_return_value(inv, one_bool("present", bt->present()));
    } else if (g_strcmp0(method, "GetState") == 0) {
        g_dbus_method_invocation_return_value(inv, one_bool("enabled", bt->get_enabled()));
    } else if (g_strcmp0(method, "SetEnabled") == 0) {
        gboolean en = FALSE;
        g_variant_get(params, "(b)", &en);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", bt->set_enabled(en)));
    } else if (g_strcmp0(method, "StartScan") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", bt->start_scan()));
    } else if (g_strcmp0(method, "StopScan") == 0) {
        bt->stop_scan();
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetDevices") == 0) {
        g_dbus_method_invocation_return_value(inv, devices_variant());
    } else if (g_strcmp0(method, "Pair") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", bt->pair(addr)));
    } else if (g_strcmp0(method, "Connect") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", bt->connect(addr)));
    } else if (g_strcmp0(method, "Disconnect") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", bt->disconnect(addr)));
    } else if (g_strcmp0(method, "Remove") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", bt->remove(addr)));
    }
}

static const GDBusInterfaceVTable bt_vtable = {
    .method_call = handle_bt_method
};

static void on_bt_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(bt_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0])
        g_dbus_connection_register_object(conn, ZYL_BT_DBUS_PATH,
            info->interfaces[0], &bt_vtable, NULL, NULL, NULL);
    if (info) g_dbus_node_info_unref(info);
    g_message("[BT] D-Bus registered: %s", ZYL_BT_DBUS_NAME);
}

ZylBluetoothService *zyl_bt_create(void) {
    ZylBluetoothService *svc = g_new0(ZylBluetoothService, 1);
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
        ZYL_BT_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bt_bus, NULL, NULL, svc, NULL);
    return svc;
}

void zyl_bt_destroy(ZylBluetoothService *svc) {
    if (!svc) return;
    g_bus_unown_name(svc->dbus_owner_id);
    g_free(svc);
}

#ifndef ZYL_TEST
static GMainLoop *g_bt_loop = NULL;

static gboolean on_signal_bt(gpointer data) {
    (void)data;
    if (g_bt_loop) g_main_loop_quit(g_bt_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ZylBluetoothService *svc = zyl_bt_create();
    g_message("[BT] Zyl OS Bluetooth Service started");
    g_bt_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_bt, NULL);
    g_unix_signal_add(SIGINT,  on_signal_bt, NULL);
    g_main_loop_run(g_bt_loop);
    g_main_loop_unref(g_bt_loop);
    zyl_bt_destroy(svc);
    return 0;
}
#endif /* ZYL_TEST */
