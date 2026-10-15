/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Bluetooth HAL Linux 구현 — BlueZ
 * 수행범위: 어댑터 유무(ObjectManager 로 첫 Adapter1 — hci0 고정 아님), 전원,
 *           검색 시작·중지, 기기 목록(이름 없으면 주소), 페어링·연결·해제·삭제
 * 의존방향: hal.h (Domain), gio (system bus)
 * SOLID: SRP — 블루투스 장치 접근만. 목록 표시 규칙은 서비스·앱 몫
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <string.h>
#include <gio/gio.h>

#define BLUEZ          "org.bluez"
#define ADAPTER_IFACE  "org.bluez.Adapter1"
#define DEVICE_IFACE   "org.bluez.Device1"
#define PROPS_IFACE    "org.freedesktop.DBus.Properties"

static GDBusConnection *bus(void) {
    static GDBusConnection *conn;
    if (!conn) conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    return conn;
}

static GVariant *managed_objects(void) {
    if (!bus()) return NULL;
    return g_dbus_connection_call_sync(bus(), BLUEZ, "/", "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
}

/* The first adapter BlueZ knows; false when there is none */
static bool adapter_path(char *out, size_t len) {
    GVariant *r = managed_objects();
    if (!r) return false;
    bool found = false;
    GVariantIter *objs;
    const gchar *path;
    GVariant *ifaces;
    g_variant_get(r, "(a{oa{sa{sv}}})", &objs);
    while (!found && g_variant_iter_next(objs, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant *adapter = g_variant_lookup_value(ifaces, ADAPTER_IFACE, NULL);
        if (adapter) {
            g_strlcpy(out, path, len);
            found = true;
            g_variant_unref(adapter);
        }
        g_variant_unref(ifaces);
    }
    g_variant_iter_free(objs);
    g_variant_unref(r);
    return found;
}

static bool call(const char *path, const char *iface, const char *method, GVariant *args, int timeout_ms) {
    GError *err = NULL;
    GVariant *r = bus() ? g_dbus_connection_call_sync(bus(), BLUEZ, path, iface, method, args, NULL,
                                                       G_DBUS_CALL_FLAGS_NONE, timeout_ms, NULL, &err)
                        : NULL;
    if (err) {
        g_message("[BT HAL] %s.%s: %s", iface, method, err->message);
        g_error_free(err);
    }
    if (!r) return false;
    g_variant_unref(r);
    return true;
}

static bool bt_present(void) {
    char a[128];
    return adapter_path(a, sizeof(a));
}

static bool bt_get_enabled(void) {
    char a[128];
    if (!adapter_path(a, sizeof(a))) return false;
    GVariant *r = g_dbus_connection_call_sync(bus(), BLUEZ, a, PROPS_IFACE, "Get",
        g_variant_new("(ss)", ADAPTER_IFACE, "Powered"), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);
    if (!r) return false;
    GVariant *v = NULL;
    g_variant_get(r, "(v)", &v);
    bool on = g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(v);
    g_variant_unref(v);
    g_variant_unref(r);
    return on;
}

static bool bt_set_enabled(bool on) {
    char a[128];
    return adapter_path(a, sizeof(a)) &&
           call(a, PROPS_IFACE, "Set", g_variant_new("(ssv)", ADAPTER_IFACE, "Powered",
                                                     g_variant_new_boolean(on)), 5000);
}

static bool bt_start_scan(void) {
    char a[128];
    return adapter_path(a, sizeof(a)) && call(a, ADAPTER_IFACE, "StartDiscovery", NULL, 5000);
}

static bool bt_stop_scan(void) {
    char a[128];
    return adapter_path(a, sizeof(a)) && call(a, ADAPTER_IFACE, "StopDiscovery", NULL, 5000);
}

static int bt_list_devices(ZylBtDevice *out, int max) {
    GVariant *r = managed_objects();
    if (!r) return -1;
    int n = 0;
    GVariantIter *objs;
    const gchar *path;
    GVariant *ifaces;
    g_variant_get(r, "(a{oa{sa{sv}}})", &objs);
    while (n < max && g_variant_iter_next(objs, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant *props = g_variant_lookup_value(ifaces, DEVICE_IFACE, G_VARIANT_TYPE("a{sv}"));
        if (props) {
            ZylBtDevice *d = &out[n];
            memset(d, 0, sizeof(*d));
            const gchar *s = NULL;
            gboolean b = FALSE;
            gint16 rssi = 0;
            if (g_variant_lookup(props, "Address", "&s", &s)) g_strlcpy(d->address, s, sizeof(d->address));
            if (g_variant_lookup(props, "Name", "&s", &s)) g_strlcpy(d->name, s, sizeof(d->name));
            else g_strlcpy(d->name, d->address, sizeof(d->name));
            if (g_variant_lookup(props, "Icon", "&s", &s)) g_strlcpy(d->type, s, sizeof(d->type));
            if (g_variant_lookup(props, "Paired", "b", &b)) d->paired = b;
            if (g_variant_lookup(props, "Connected", "b", &b)) d->connected = b;
            if (g_variant_lookup(props, "RSSI", "n", &rssi)) d->rssi = rssi;
            if (d->address[0]) n++;
            g_variant_unref(props);
        }
        g_variant_unref(ifaces);
    }
    g_variant_iter_free(objs);
    g_variant_unref(r);
    return n;
}

/* "AA:BB:CC:DD:EE:FF" → "<adapter>/dev_AA_BB_CC_DD_EE_FF" */
static bool device_path(const char *address, char *out, size_t len) {
    char a[128];
    if (!address || strlen(address) != 17 || !adapter_path(a, sizeof(a))) return false;
    snprintf(out, len, "%s/dev_%s", a, address);
    for (char *p = out + strlen(a) + 5; *p; p++) if (*p == ':') *p = '_';
    return true;
}

static bool bt_pair(const char *address) {
    char p[192];
    /* The other device may ask its user to confirm */
    return device_path(address, p, sizeof(p)) && call(p, DEVICE_IFACE, "Pair", NULL, 30000);
}

static bool bt_connect(const char *address) {
    char p[192];
    return device_path(address, p, sizeof(p)) && call(p, DEVICE_IFACE, "Connect", NULL, 10000);
}

static bool bt_disconnect(const char *address) {
    char p[192];
    return device_path(address, p, sizeof(p)) && call(p, DEVICE_IFACE, "Disconnect", NULL, 5000);
}

static bool bt_remove(const char *address) {
    char a[128], p[192];
    return device_path(address, p, sizeof(p)) && adapter_path(a, sizeof(a)) &&
           call(a, ADAPTER_IFACE, "RemoveDevice", g_variant_new("(o)", p), 5000);
}

static const ZylBtHal bt_hal = {
    .present      = bt_present,
    .get_enabled  = bt_get_enabled,
    .set_enabled  = bt_set_enabled,
    .start_scan   = bt_start_scan,
    .stop_scan    = bt_stop_scan,
    .list_devices = bt_list_devices,
    .pair         = bt_pair,
    .connect      = bt_connect,
    .disconnect   = bt_disconnect,
    .remove       = bt_remove,
};

const ZylBtHal *zyl_hal_bt(void) { return &bt_hal; }
