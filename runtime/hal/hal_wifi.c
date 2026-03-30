/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Implementation
 *
 * 역할: WiFi HAL Linux 구현체 — wpa_supplicant + nl80211 via D-Bus
 * 수행범위: WiFi 스캔, 연결, 해제, 신호 강도 조회.
 *           드라이버 직접 ioctl 미사용 — wpa_supplicant D-Bus 경유.
 * 의존방향: hal.h (Domain), gio/gio.h (Platform)
 * SOLID: DIP — hal.h 인터페이스 구현, OCP — 다른 HAL과 독립 교체 가능
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#define WPA_BUS_NAME    "fi.w1.wpa_supplicant1"
#define WPA_BUS_PATH    "/fi/w1/wpa_supplicant1"
#define WPA_IFACE       "fi.w1.wpa_supplicant1"
#define WPA_IFACE_IF    "fi.w1.wpa_supplicant1.Interface"
#define WPA_IFACE_BSS   "fi.w1.wpa_supplicant1.BSS"

#define NM_BUS_NAME     "org.freedesktop.NetworkManager"
#define NM_BUS_PATH     "/org/freedesktop/NetworkManager"
#define NM_IFACE        "org.freedesktop.NetworkManager"
#define NM_IFACE_DEV    "org.freedesktop.NetworkManager.Device"
#define NM_IFACE_WIFI   "org.freedesktop.NetworkManager.Device.Wireless"

static GDBusConnection *g_conn = NULL;
static char g_iface_path[256] = {0};  /* wpa_supplicant interface object path */

/* ─── Helper: D-Bus property get ─── */
static GVariant *dbus_get_prop(const char *bus, const char *path,
                                const char *iface, const char *prop) {
    if (!g_conn) return NULL;
    GVariant *result = g_dbus_connection_call_sync(g_conn,
        bus, path, "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", iface, prop),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);
    if (!result) return NULL;
    GVariant *val = NULL;
    g_variant_get(result, "(v)", &val);
    g_variant_unref(result);
    return val;
}

/* ─── Helper: call D-Bus method, return result (caller frees) ─── */
static GVariant *dbus_call(const char *bus, const char *path,
                            const char *iface, const char *method,
                            GVariant *params, const GVariantType *reply_type) {
    if (!g_conn) return NULL;
    return g_dbus_connection_call_sync(g_conn, bus, path, iface, method,
        params, reply_type, G_DBUS_CALL_FLAGS_NONE, 10000, NULL, NULL);
}

/* ─── Init: connect to system bus, find wpa_supplicant interface ─── */
static int wifi_init(void) {
    GError *err = NULL;
    g_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!g_conn) {
        if (err) g_error_free(err);
        return -1;
    }

    /* Get first wpa_supplicant interface (e.g. /fi/w1/wpa_supplicant1/Interfaces/0) */
    GVariant *result = dbus_get_prop(WPA_BUS_NAME, WPA_BUS_PATH, WPA_IFACE, "Interfaces");
    if (result) {
        GVariantIter *iter = NULL;
        g_variant_get(result, "ao", &iter);
        const gchar *path = NULL;
        if (g_variant_iter_next(iter, "&o", &path) && path) {
            snprintf(g_iface_path, sizeof(g_iface_path), "%s", path);
        }
        g_variant_iter_free(iter);
        g_variant_unref(result);
    }

    return (g_iface_path[0] != '\0') ? 0 : -1;
}

static void wifi_shutdown(void) {
    if (g_conn) {
        g_object_unref(g_conn);
        g_conn = NULL;
    }
    g_iface_path[0] = '\0';
}

static int wifi_set_enabled(bool enabled) {
    /* Enable/disable via NetworkManager — toggle WiFi global switch */
    GVariant *result = dbus_call(NM_BUS_NAME, NM_BUS_PATH,
        "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", NM_IFACE, "WirelessEnabled",
                       g_variant_new_boolean(enabled)),
        NULL);
    if (result) { g_variant_unref(result); return 0; }
    return -1;
}

static int wifi_get_state(ZylWifiState *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    /* Check if wireless is enabled via NM */
    GVariant *we = dbus_get_prop(NM_BUS_NAME, NM_BUS_PATH, NM_IFACE, "WirelessEnabled");
    if (we) {
        out->enabled = g_variant_get_boolean(we);
        g_variant_unref(we);
    }

    /* Get current SSID from wpa_supplicant */
    if (g_iface_path[0]) {
        GVariant *nw = dbus_get_prop(WPA_BUS_NAME, g_iface_path, WPA_IFACE_IF, "CurrentNetwork");
        if (nw) {
            const gchar *nw_path = g_variant_get_string(nw, NULL);
            if (nw_path && strcmp(nw_path, "/") != 0) {
                out->connected = true;
                /* Read SSID from network properties */
                GVariant *props = dbus_get_prop(WPA_BUS_NAME, nw_path,
                    "fi.w1.wpa_supplicant1.Network", "Properties");
                if (props) {
                    GVariant *ssid_v = g_variant_lookup_value(props, "ssid", G_VARIANT_TYPE_STRING);
                    if (ssid_v) {
                        snprintf(out->current_ssid, sizeof(out->current_ssid),
                                 "%s", g_variant_get_string(ssid_v, NULL));
                        g_variant_unref(ssid_v);
                    }
                    g_variant_unref(props);
                }
            }
            g_variant_unref(nw);
        }
    }

    return 0;
}

static int wifi_scan(ZylWifiNetwork **out_list, int *out_count) {
    if (!out_list || !out_count) return -1;
    *out_list = NULL;
    *out_count = 0;

    if (!g_iface_path[0]) return -1;

    /* Trigger scan */
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    GVariant *scan_args = g_variant_new("(a{sv})", &builder);
    GVariant *sr = dbus_call(WPA_BUS_NAME, g_iface_path, WPA_IFACE_IF,
                              "Scan", scan_args, NULL);
    if (sr) g_variant_unref(sr);

    /* Small delay for scan to populate — real impl would use SignalChanged */
    g_usleep(2000000); /* 2 seconds */

    /* Read BSSs */
    GVariant *bss_list = dbus_get_prop(WPA_BUS_NAME, g_iface_path, WPA_IFACE_IF, "BSSs");
    if (!bss_list) return 0;

    gsize n = g_variant_n_children(bss_list);
    if (n == 0) { g_variant_unref(bss_list); return 0; }

    ZylWifiNetwork *nets = calloc(n, sizeof(ZylWifiNetwork));
    if (!nets) { g_variant_unref(bss_list); return -1; }

    int count = 0;
    for (gsize i = 0; i < n; i++) {
        GVariant *child = g_variant_get_child_value(bss_list, i);
        const gchar *bss_path = g_variant_get_string(child, NULL);

        ZylWifiNetwork *net = &nets[count];

        /* SSID (byte array) */
        GVariant *ssid_v = dbus_get_prop(WPA_BUS_NAME, bss_path, WPA_IFACE_BSS, "SSID");
        if (ssid_v) {
            gsize ssid_len = 0;
            const guchar *ssid_data = g_variant_get_fixed_array(ssid_v, &ssid_len, 1);
            if (ssid_data && ssid_len > 0 && ssid_len < sizeof(net->ssid)) {
                memcpy(net->ssid, ssid_data, ssid_len);
                net->ssid[ssid_len] = '\0';
            }
            g_variant_unref(ssid_v);
        }

        /* BSSID (byte array → MAC string) */
        GVariant *bssid_v = dbus_get_prop(WPA_BUS_NAME, bss_path, WPA_IFACE_BSS, "BSSID");
        if (bssid_v) {
            gsize bssid_len = 0;
            const guchar *bssid_data = g_variant_get_fixed_array(bssid_v, &bssid_len, 1);
            if (bssid_data && bssid_len == 6) {
                snprintf(net->bssid, sizeof(net->bssid),
                         "%02x:%02x:%02x:%02x:%02x:%02x",
                         bssid_data[0], bssid_data[1], bssid_data[2],
                         bssid_data[3], bssid_data[4], bssid_data[5]);
            }
            g_variant_unref(bssid_v);
        }

        /* Signal (dBm) */
        GVariant *sig_v = dbus_get_prop(WPA_BUS_NAME, bss_path, WPA_IFACE_BSS, "Signal");
        if (sig_v) {
            net->signal_dbm = (int16_t)g_variant_get_int16(sig_v);
            /* dBm → percent: -30 dBm = 100%, -90 dBm = 0% */
            int pct = (net->signal_dbm + 90) * 100 / 60;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            net->signal_percent = pct;
            g_variant_unref(sig_v);
        }

        /* Frequency */
        GVariant *freq_v = dbus_get_prop(WPA_BUS_NAME, bss_path, WPA_IFACE_BSS, "Frequency");
        if (freq_v) {
            net->frequency_mhz = (int)g_variant_get_uint16(freq_v);
            g_variant_unref(freq_v);
        }

        /* Skip hidden SSIDs */
        if (net->ssid[0] != '\0') count++;

        g_variant_unref(child);
    }

    g_variant_unref(bss_list);
    *out_list = nets;
    *out_count = count;
    return 0;
}

static int wifi_connect(const char *ssid, const char *password) {
    if (!g_iface_path[0] || !ssid) return -1;

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "ssid", g_variant_new_string(ssid));
    if (password && password[0]) {
        g_variant_builder_add(&builder, "{sv}", "psk", g_variant_new_string(password));
    }

    GVariant *result = dbus_call(WPA_BUS_NAME, g_iface_path, WPA_IFACE_IF,
                                  "AddNetwork",
                                  g_variant_new("(a{sv})", &builder),
                                  G_VARIANT_TYPE("(o)"));
    if (!result) return -1;

    const gchar *nw_path = NULL;
    g_variant_get(result, "(&o)", &nw_path);

    /* Select the network */
    if (nw_path) {
        GVariant *sel = dbus_call(WPA_BUS_NAME, g_iface_path, WPA_IFACE_IF,
                                   "SelectNetwork",
                                   g_variant_new("(o)", nw_path), NULL);
        if (sel) g_variant_unref(sel);
    }

    g_variant_unref(result);
    return 0;
}

static int wifi_disconnect(void) {
    if (!g_iface_path[0]) return -1;
    GVariant *result = dbus_call(WPA_BUS_NAME, g_iface_path, WPA_IFACE_IF,
                                  "Disconnect", NULL, NULL);
    if (result) { g_variant_unref(result); return 0; }
    return -1;
}

static int wifi_get_signal_strength(int *out_percent) {
    if (!out_percent || !g_iface_path[0]) return -1;

    GVariant *bss = dbus_get_prop(WPA_BUS_NAME, g_iface_path, WPA_IFACE_IF, "CurrentBSS");
    if (!bss) return -1;

    const gchar *bss_path = g_variant_get_string(bss, NULL);
    if (!bss_path || strcmp(bss_path, "/") == 0) {
        g_variant_unref(bss);
        return -1;
    }

    GVariant *sig_v = dbus_get_prop(WPA_BUS_NAME, bss_path, WPA_IFACE_BSS, "Signal");
    if (sig_v) {
        int dbm = (int16_t)g_variant_get_int16(sig_v);
        int pct = (dbm + 90) * 100 / 60;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        *out_percent = pct;
        g_variant_unref(sig_v);
    }

    g_variant_unref(bss);
    return 0;
}

/* ─── HAL 인스턴스 생성 ─── */
static ZylWifiHal wifi_hal_instance = {
    .init               = wifi_init,
    .shutdown            = wifi_shutdown,
    .set_enabled         = wifi_set_enabled,
    .get_state           = wifi_get_state,
    .scan                = wifi_scan,
    .connect             = wifi_connect,
    .disconnect          = wifi_disconnect,
    .get_signal_strength = wifi_get_signal_strength,
};

ZylWifiHal *zyl_hal_wifi_linux(void) {
    return &wifi_hal_instance;
}
