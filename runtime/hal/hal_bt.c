/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Implementation
 *
 * 역할: Bluetooth HAL Linux 구현체 — BlueZ D-Bus API
 * 수행범위: BT 켜기/끄기, 스캔, 페어링, 연결/해제, paired 목록
 * 의존방향: hal.h (Domain), gio/gio.h (Platform)
 * SOLID: DIP — hal.h 인터페이스 구현, SRP — BT HAL만 담당
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#define BLUEZ_BUS           "org.bluez"
#define BLUEZ_ADAPTER_IFACE "org.bluez.Adapter1"
#define BLUEZ_DEVICE_IFACE  "org.bluez.Device1"
#define DBUS_OM_IFACE       "org.freedesktop.DBus.ObjectManager"
#define DBUS_PROP_IFACE     "org.freedesktop.DBus.Properties"

static GDBusConnection *g_conn = NULL;
static char g_adapter_path[128] = {0};

/* ─── Helper ─── */
static GVariant *dbus_get_prop(const char *path, const char *iface, const char *prop) {
    if (!g_conn) return NULL;
    GVariant *result = g_dbus_connection_call_sync(g_conn, BLUEZ_BUS, path,
        DBUS_PROP_IFACE, "Get", g_variant_new("(ss)", iface, prop),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);
    if (!result) return NULL;
    GVariant *val = NULL;
    g_variant_get(result, "(v)", &val);
    g_variant_unref(result);
    return val;
}

static GVariant *dbus_call(const char *path, const char *iface,
                            const char *method, GVariant *params) {
    if (!g_conn) return NULL;
    return g_dbus_connection_call_sync(g_conn, BLUEZ_BUS, path, iface, method,
        params, NULL, G_DBUS_CALL_FLAGS_NONE, 15000, NULL, NULL);
}

/* ─── Find default adapter (hci0) ─── */
static int find_adapter(void) {
    GVariant *result = g_dbus_connection_call_sync(g_conn, BLUEZ_BUS, "/",
        DBUS_OM_IFACE, "GetManagedObjects", NULL,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE,
        5000, NULL, NULL);
    if (!result) return -1;

    GVariantIter *obj_iter = NULL;
    g_variant_get(result, "(a{oa{sa{sv}}})", &obj_iter);

    const gchar *obj_path = NULL;
    GVariant *ifaces = NULL;
    while (g_variant_iter_next(obj_iter, "{&o@a{sa{sv}}}", &obj_path, &ifaces)) {
        if (g_variant_lookup(ifaces, BLUEZ_ADAPTER_IFACE, "a{sv}", NULL)) {
            snprintf(g_adapter_path, sizeof(g_adapter_path), "%s", obj_path);
            g_variant_unref(ifaces);
            break;
        }
        g_variant_unref(ifaces);
    }
    g_variant_iter_free(obj_iter);
    g_variant_unref(result);
    return (g_adapter_path[0] != '\0') ? 0 : -1;
}

/* ─── Parse devices from ObjectManager ─── */
static int parse_devices(ZylBtDevice **out_list, int *out_count, bool paired_only) {
    GVariant *result = g_dbus_connection_call_sync(g_conn, BLUEZ_BUS, "/",
        DBUS_OM_IFACE, "GetManagedObjects", NULL,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE,
        5000, NULL, NULL);
    if (!result) { *out_list = NULL; *out_count = 0; return -1; }

    /* Count devices first */
    GVariantIter *obj_iter = NULL;
    g_variant_get(result, "(a{oa{sa{sv}}})", &obj_iter);

    int capacity = 32;
    ZylBtDevice *devs = calloc(capacity, sizeof(ZylBtDevice));
    if (!devs) { g_variant_iter_free(obj_iter); g_variant_unref(result); return -1; }

    int count = 0;
    const gchar *obj_path = NULL;
    GVariant *ifaces = NULL;
    while (g_variant_iter_next(obj_iter, "{&o@a{sa{sv}}}", &obj_path, &ifaces)) {
        GVariant *dev_props = g_variant_lookup_value(ifaces, BLUEZ_DEVICE_IFACE,
                                                      G_VARIANT_TYPE("a{sv}"));
        if (dev_props) {
            ZylBtDevice *d = &devs[count];

            GVariant *v;
            v = g_variant_lookup_value(dev_props, "Name", G_VARIANT_TYPE_STRING);
            if (v) { snprintf(d->name, sizeof(d->name), "%s", g_variant_get_string(v, NULL)); g_variant_unref(v); }

            v = g_variant_lookup_value(dev_props, "Address", G_VARIANT_TYPE_STRING);
            if (v) { snprintf(d->address, sizeof(d->address), "%s", g_variant_get_string(v, NULL)); g_variant_unref(v); }

            v = g_variant_lookup_value(dev_props, "Paired", G_VARIANT_TYPE_BOOLEAN);
            if (v) { d->paired = g_variant_get_boolean(v); g_variant_unref(v); }

            v = g_variant_lookup_value(dev_props, "Connected", G_VARIANT_TYPE_BOOLEAN);
            if (v) { d->connected = g_variant_get_boolean(v); g_variant_unref(v); }

            v = g_variant_lookup_value(dev_props, "Icon", G_VARIANT_TYPE_STRING);
            if (v) { snprintf(d->type, sizeof(d->type), "%s", g_variant_get_string(v, NULL)); g_variant_unref(v); }

            d->battery_percent = -1;
            v = g_variant_lookup_value(dev_props, "Percentage", G_VARIANT_TYPE_BYTE);
            if (v) { d->battery_percent = (int)g_variant_get_byte(v); g_variant_unref(v); }

            if (!paired_only || d->paired) {
                count++;
                if (count >= capacity) {
                    capacity *= 2;
                    ZylBtDevice *tmp = realloc(devs, capacity * sizeof(ZylBtDevice));
                    if (tmp) devs = tmp; else break;
                }
            }
            g_variant_unref(dev_props);
        }
        g_variant_unref(ifaces);
    }
    g_variant_iter_free(obj_iter);
    g_variant_unref(result);

    *out_list = devs;
    *out_count = count;
    return 0;
}

/* ─── HAL implementation ─── */
static int bt_init(void) {
    GError *err = NULL;
    g_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!g_conn) { if (err) g_error_free(err); return -1; }
    return find_adapter();
}

static void bt_shutdown(void) {
    if (g_conn) { g_object_unref(g_conn); g_conn = NULL; }
    g_adapter_path[0] = '\0';
}

static int bt_set_enabled(bool enabled) {
    if (!g_adapter_path[0]) return -1;
    GVariant *result = g_dbus_connection_call_sync(g_conn, BLUEZ_BUS, g_adapter_path,
        DBUS_PROP_IFACE, "Set",
        g_variant_new("(ssv)", BLUEZ_ADAPTER_IFACE, "Powered", g_variant_new_boolean(enabled)),
        NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);
    if (result) { g_variant_unref(result); return 0; }
    return -1;
}

static int bt_get_state(ZylBtState *out) {
    if (!out || !g_adapter_path[0]) return -1;
    memset(out, 0, sizeof(*out));

    GVariant *v = dbus_get_prop(g_adapter_path, BLUEZ_ADAPTER_IFACE, "Powered");
    if (v) { out->enabled = g_variant_get_boolean(v); g_variant_unref(v); }

    v = dbus_get_prop(g_adapter_path, BLUEZ_ADAPTER_IFACE, "Alias");
    if (v) { snprintf(out->device_name, sizeof(out->device_name), "%s",
                       g_variant_get_string(v, NULL)); g_variant_unref(v); }

    v = dbus_get_prop(g_adapter_path, BLUEZ_ADAPTER_IFACE, "Discoverable");
    if (v) { out->discoverable = g_variant_get_boolean(v); g_variant_unref(v); }

    return 0;
}

static int bt_scan(ZylBtDevice **out_list, int *out_count) {
    if (!g_adapter_path[0] || !out_list || !out_count) return -1;

    /*
     * Async-friendly scan window:
     * - Start discovery
     * - Pump GLib main context in short intervals instead of a monolithic sleep
     *   so BlueZ signals and D-Bus dispatch are not starved.
     * - Stop discovery after ~5s and then enumerate managed objects.
     */
    GVariant *r;

    /* Best-effort discovery filter to reduce duplicates / speed convergence */
    GVariantBuilder filter;
    g_variant_builder_init(&filter, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&filter, "{sv}", "Transport", g_variant_new_string("auto"));
    r = dbus_call(g_adapter_path, BLUEZ_ADAPTER_IFACE, "SetDiscoveryFilter",
                  g_variant_new("(a{sv})", &filter));
    if (r) g_variant_unref(r);

    r = dbus_call(g_adapter_path, BLUEZ_ADAPTER_IFACE, "StartDiscovery", NULL);
    if (r) g_variant_unref(r);

    gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < deadline) {
        /* Dispatch pending D-Bus / GLib work instead of hard-blocking. */
        while (g_main_context_iteration(NULL, FALSE)) {
            /* drain */
        }
        g_usleep(100000); /* 100 ms slices */
    }

    r = dbus_call(g_adapter_path, BLUEZ_ADAPTER_IFACE, "StopDiscovery", NULL);
    if (r) g_variant_unref(r);

    return parse_devices(out_list, out_count, false);
}

static int bt_pair(const char *address) {
    if (!address) return -1;
    /* BlueZ device path: /org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX */
    char dev_path[256];
    char addr_under[18];
    snprintf(addr_under, sizeof(addr_under), "%s", address);
    for (int i = 0; addr_under[i]; i++) {
        if (addr_under[i] == ':') addr_under[i] = '_';
    }
    snprintf(dev_path, sizeof(dev_path), "%s/dev_%s", g_adapter_path, addr_under);

    GVariant *r = dbus_call(dev_path, BLUEZ_DEVICE_IFACE, "Pair", NULL);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

static int bt_unpair(const char *address) {
    if (!address || !g_adapter_path[0]) return -1;
    char addr_under[18];
    snprintf(addr_under, sizeof(addr_under), "%s", address);
    for (int i = 0; addr_under[i]; i++) {
        if (addr_under[i] == ':') addr_under[i] = '_';
    }
    char dev_path[256];
    snprintf(dev_path, sizeof(dev_path), "%s/dev_%s", g_adapter_path, addr_under);

    GVariant *r = dbus_call(g_adapter_path, BLUEZ_ADAPTER_IFACE, "RemoveDevice",
                             g_variant_new("(o)", dev_path));
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

static int bt_connect_device(const char *address) {
    if (!address) return -1;
    char addr_under[18];
    snprintf(addr_under, sizeof(addr_under), "%s", address);
    for (int i = 0; addr_under[i]; i++) { if (addr_under[i] == ':') addr_under[i] = '_'; }
    char dev_path[256];
    snprintf(dev_path, sizeof(dev_path), "%s/dev_%s", g_adapter_path, addr_under);

    GVariant *r = dbus_call(dev_path, BLUEZ_DEVICE_IFACE, "Connect", NULL);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

static int bt_disconnect_device(const char *address) {
    if (!address) return -1;
    char addr_under[18];
    snprintf(addr_under, sizeof(addr_under), "%s", address);
    for (int i = 0; addr_under[i]; i++) { if (addr_under[i] == ':') addr_under[i] = '_'; }
    char dev_path[256];
    snprintf(dev_path, sizeof(dev_path), "%s/dev_%s", g_adapter_path, addr_under);

    GVariant *r = dbus_call(dev_path, BLUEZ_DEVICE_IFACE, "Disconnect", NULL);
    if (r) { g_variant_unref(r); return 0; }
    return -1;
}

static int bt_get_paired_devices(ZylBtDevice **out_list, int *out_count) {
    if (!out_list || !out_count) return -1;
    return parse_devices(out_list, out_count, true);
}

/* ─── HAL 인스턴스 ─── */
static ZylBtHal bt_hal_instance = {
    .init              = bt_init,
    .shutdown          = bt_shutdown,
    .set_enabled       = bt_set_enabled,
    .get_state         = bt_get_state,
    .scan              = bt_scan,
    .pair              = bt_pair,
    .unpair            = bt_unpair,
    .connect_device    = bt_connect_device,
    .disconnect_device = bt_disconnect_device,
    .get_paired_devices = bt_get_paired_devices,
};

ZylBtHal *zyl_hal_bt_linux(void) {
    return &bt_hal_instance;
}
