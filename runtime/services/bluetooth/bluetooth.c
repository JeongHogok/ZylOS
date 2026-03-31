#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: BlueZ D-Bus 클라이언트 — 스캔, 페어링, 연결 관리
 * 수행범위: org.bluez Adapter1/Device1 D-Bus 인터페이스 통합
 * 의존방향: bluetooth.h, gio/gio.h
 * SOLID: SRP — Bluetooth 디바이스 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "bluetooth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib-unix.h>

#define BLUEZ_BUS    "org.bluez"
#define ADAPTER_PATH "/org/bluez/hci0"
#define ADAPTER_IFACE "org.bluez.Adapter1"
#define DEVICE_IFACE  "org.bluez.Device1"

struct ZylBluetoothService {
    GDBusConnection *system_bus;
    GDBusConnection *session_bus;
    guint dbus_owner_id;
    bool enabled;
};

/* ─── BlueZ D-Bus helpers ─── */

static GVariant *bluez_call_timeout(GDBusConnection *conn, const char *path,
                                     const char *iface, const char *method,
                                     GVariant *params, int timeout_ms) {
    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        conn, BLUEZ_BUS, path, iface, method, params,
        NULL, G_DBUS_CALL_FLAGS_NONE, timeout_ms, NULL, &err);
    if (err) {
        fprintf(stderr, "[BT] %s.%s failed: %s\n", iface, method,
                err->message);
        g_error_free(err);
    }
    return result;
}

static GVariant *bluez_call(GDBusConnection *conn, const char *path,
                             const char *iface, const char *method,
                             GVariant *params) {
    return bluez_call_timeout(conn, path, iface, method, params, 5000);
}

static GVariant *bluez_get_prop(GDBusConnection *conn, const char *path,
                                 const char *iface, const char *prop) {
    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        conn, BLUEZ_BUS, path, "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", iface, prop),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &err);
    if (err) { g_error_free(err); return NULL; }
    GVariant *val = NULL;
    g_variant_get(result, "(v)", &val);
    g_variant_unref(result);
    return val;
}

/* ─── Service D-Bus interface ─── */

static const char *bt_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_BT_DBUS_NAME "'>"
    "    <method name='StartScan'>"
    "      <arg type='i' name='timeout_sec' direction='in'/>"
    "    </method>"
    "    <method name='StopScan'/>"
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
    "    </method>"
    "    <method name='IsEnabled'>"
    "      <arg type='b' name='enabled' direction='out'/>"
    "    </method>"
    "    <method name='SetEnabled'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void handle_bt_method(GDBusConnection *conn, const gchar *sender,
                              const gchar *path, const gchar *iface,
                              const gchar *method, GVariant *params,
                              GDBusMethodInvocation *inv, gpointer data) {
    ZylBluetoothService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "StartScan") == 0) {
        gint32 timeout;
        g_variant_get(params, "(i)", &timeout);
        zyl_bt_start_scan(svc, timeout);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "StopScan") == 0) {
        zyl_bt_stop_scan(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Pair") == 0) {
        const gchar *addr;
        g_variant_get(params, "(&s)", &addr);
        int ret = zyl_bt_pair(svc, addr);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "Connect") == 0) {
        const gchar *addr;
        g_variant_get(params, "(&s)", &addr);
        int ret = zyl_bt_connect(svc, addr);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "Disconnect") == 0) {
        const gchar *addr;
        g_variant_get(params, "(&s)", &addr);
        zyl_bt_disconnect(svc, addr);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "IsEnabled") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", zyl_bt_is_enabled(svc)));
    } else if (g_strcmp0(method, "SetEnabled") == 0) {
        gboolean en;
        g_variant_get(params, "(b)", &en);
        zyl_bt_set_enabled(svc, en);
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

static const GDBusInterfaceVTable bt_vtable = {
    .method_call = handle_bt_method
};

static void on_bt_bus(GDBusConnection *conn, const gchar *name,
                       gpointer data) {
    ZylBluetoothService *svc = data;
    (void)name;
    svc->session_bus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        bt_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_BT_DBUS_PATH,
            info->interfaces[0], &bt_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[BT] D-Bus registered: %s", ZYL_BT_DBUS_NAME);
}

/* ─── Public API ─── */

ZylBluetoothService *zyl_bt_create(void) {
    ZylBluetoothService *svc = calloc(1, sizeof(ZylBluetoothService));
    if (!svc) return NULL;

    GError *err = NULL;
    svc->system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (err) {
        fprintf(stderr, "[BT] System bus failed: %s\n", err->message);
        g_error_free(err);
    }

    svc->enabled = true;
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_BT_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bt_bus, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_bt_destroy(ZylBluetoothService *svc) {
    if (!svc) return;
    g_bus_unown_name(svc->dbus_owner_id);
    if (svc->system_bus) g_object_unref(svc->system_bus);
    free(svc);
}

int zyl_bt_start_scan(ZylBluetoothService *svc, int timeout_sec) {
    if (!svc || !svc->system_bus) return -1;
    (void)timeout_sec;
    GVariant *r = bluez_call(svc->system_bus, ADAPTER_PATH,
        ADAPTER_IFACE, "StartDiscovery", NULL);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

void zyl_bt_stop_scan(ZylBluetoothService *svc) {
    if (!svc || !svc->system_bus) return;
    GVariant *r = bluez_call(svc->system_bus, ADAPTER_PATH,
        ADAPTER_IFACE, "StopDiscovery", NULL);
    if (r) g_variant_unref(r);
}

static char *addr_to_path(const char *addr) {
    /* Convert "AA:BB:CC:DD:EE:FF" → "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF" */
    char *path = malloc(64);
    if (!path) return NULL;
    snprintf(path, 64, "%s/dev_%s", ADAPTER_PATH, addr);
    for (char *p = path + strlen(ADAPTER_PATH) + 5; *p; p++) {
        if (*p == ':') *p = '_';
    }
    return path;
}

int zyl_bt_pair(ZylBluetoothService *svc, const char *address) {
    if (!svc || !svc->system_bus || !address) return -1;
    char *path = addr_to_path(address);
    if (!path) return -1;
    /* Pairing can take up to 30 seconds for user confirmation */
    GVariant *r = bluez_call_timeout(svc->system_bus, path,
        DEVICE_IFACE, "Pair", NULL, 30000);
    free(path);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

int zyl_bt_connect(ZylBluetoothService *svc, const char *address) {
    if (!svc || !svc->system_bus || !address) return -1;
    char *path = addr_to_path(address);
    if (!path) return -1;
    GVariant *r = bluez_call(svc->system_bus, path,
        DEVICE_IFACE, "Connect", NULL);
    free(path);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

int zyl_bt_disconnect(ZylBluetoothService *svc, const char *address) {
    if (!svc || !svc->system_bus || !address) return -1;
    char *path = addr_to_path(address);
    if (!path) return -1;
    GVariant *r = bluez_call(svc->system_bus, path,
        DEVICE_IFACE, "Disconnect", NULL);
    free(path);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

int zyl_bt_remove(ZylBluetoothService *svc, const char *address) {
    if (!svc || !svc->system_bus || !address) return -1;
    char *path = addr_to_path(address);
    if (!path) return -1;
    GVariant *r = bluez_call(svc->system_bus, ADAPTER_PATH,
        ADAPTER_IFACE, "RemoveDevice",
        g_variant_new("(o)", path));
    free(path);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

bool zyl_bt_is_enabled(ZylBluetoothService *svc) {
    if (!svc || !svc->system_bus) return false;
    GVariant *val = bluez_get_prop(svc->system_bus, ADAPTER_PATH,
        ADAPTER_IFACE, "Powered");
    if (!val) return false;
    bool powered = g_variant_get_boolean(val);
    g_variant_unref(val);
    return powered;
}

void zyl_bt_set_enabled(ZylBluetoothService *svc, bool enabled) {
    if (!svc || !svc->system_bus) return;
    GError *err = NULL;
    g_dbus_connection_call_sync(svc->system_bus, BLUEZ_BUS, ADAPTER_PATH,
        "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", ADAPTER_IFACE, "Powered",
                      g_variant_new_boolean(enabled)),
        NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &err);
    if (err) {
        fprintf(stderr, "[BT] SetEnabled failed: %s\n", err->message);
        g_error_free(err);
    }
    svc->enabled = enabled;
}

int zyl_bt_get_devices(ZylBluetoothService *svc, ZylBtDevice **out,
                        int *count) {
    /* Uses org.freedesktop.DBus.ObjectManager to enumerate BlueZ devices */
    if (!svc || !svc->system_bus || !out || !count) return -1;
    *out = NULL; *count = 0;

    GError *err = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        svc->system_bus, BLUEZ_BUS, "/",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
        NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);
    if (err) { g_error_free(err); return -1; }
    if (!result) return -1;

    GVariantIter *obj_iter;
    g_variant_get(result, "(a{oa{sa{sv}}})", &obj_iter);

    int cap = 16, n = 0;
    ZylBtDevice *devs = calloc(cap, sizeof(ZylBtDevice));
    if (!devs) { g_variant_iter_free(obj_iter); g_variant_unref(result); return -1; }

    const gchar *obj_path;
    GVariantIter *iface_iter;
    while (g_variant_iter_loop(obj_iter, "{oa{sa{sv}}}", &obj_path,
                               &iface_iter)) {
        const gchar *iface_name;
        GVariantIter *prop_iter;
        while (g_variant_iter_loop(iface_iter, "{sa{sv}}", &iface_name,
                                   &prop_iter)) {
            if (g_strcmp0(iface_name, DEVICE_IFACE) != 0) continue;
            if (n >= cap) {
                cap *= 2;
                ZylBtDevice *tmp = realloc(devs, cap * sizeof(ZylBtDevice));
                if (!tmp) break;
                devs = tmp;
            }
            ZylBtDevice *d = &devs[n];
            memset(d, 0, sizeof(*d));
            const gchar *pname;
            GVariant *pval;
            while (g_variant_iter_loop(prop_iter, "{sv}", &pname, &pval)) {
                if (g_strcmp0(pname, "Address") == 0)
                    d->address = g_variant_dup_string(pval, NULL);
                else if (g_strcmp0(pname, "Name") == 0)
                    d->name = g_variant_dup_string(pval, NULL);
                else if (g_strcmp0(pname, "Paired") == 0)
                    d->paired = g_variant_get_boolean(pval);
                else if (g_strcmp0(pname, "Connected") == 0)
                    d->connected = g_variant_get_boolean(pval);
                else if (g_strcmp0(pname, "RSSI") == 0)
                    d->rssi = g_variant_get_int16(pval);
                else if (g_strcmp0(pname, "Icon") == 0)
                    /* BlueZ Icon property: "audio-headphones", "phone", etc. */
                    d->device_type = g_variant_dup_string(pval, NULL);
            }
            n++;
        }
    }
    g_variant_iter_free(obj_iter);
    g_variant_unref(result);

    *out = devs;
    *count = n;
    return 0;
}

void zyl_bt_device_free(ZylBtDevice *devices, int count) {
    if (!devices) return;
    for (int i = 0; i < count; i++) {
        free(devices[i].address);
        free(devices[i].name);
        free(devices[i].device_type);
    }
    free(devices);
}

/* ─── 데몬 진입점 ─── */

static GMainLoop *g_bt_loop = NULL;

static gboolean on_signal_bt(gpointer data) {
    (void)data;
    g_message("[BT] Signal received, shutting down");
    if (g_bt_loop) g_main_loop_quit(g_bt_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ZylBluetoothService *svc = zyl_bt_create();
    if (!svc) { g_critical("[BT] Failed to create service"); return 1; }
    g_message("[BT] Zyl OS Bluetooth Service started (BlueZ)");
    g_bt_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_bt, NULL);
    g_unix_signal_add(SIGINT,  on_signal_bt, NULL);
    g_main_loop_run(g_bt_loop);
    g_main_loop_unref(g_bt_loop);
    zyl_bt_destroy(svc);
    return 0;
}
