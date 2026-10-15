/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: Wi-Fi 서비스 (org.zylos.WifiService) — 무선 연결·핫스팟 정책과 D-Bus
 * 수행범위: 이 기기에 있는 기능(Wi-Fi 장치, 핫스팟) 보고 — 없으면 앱이 비활성,
 *           무선 켜고 끄기·상태, 스캔·목록·연결·해제, 연결 뒤 포털 확인
 *           (CaptivePortalDetected), 핫스팟 켜고 끄기(이름 1–32바이트, 암호
 *           8–63자 — 비우면 만들어 줌)
 * 의존방향: wifi.h, hal.h (Wi-Fi HAL), captive_portal.c, gio/gio.h (D-Bus)
 * SOLID: SRP — 무선 정책만. DIP — 장치는 ZylWifiHal 로만
 * ────────────────────────────────────────────────────────── */

#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <gio/gio.h>
#include <glib-unix.h>

/* captive_portal.c */
typedef enum {
    ZYL_NETWORK_CONNECTED_STATUS = 0,
    ZYL_NETWORK_CAPTIVE_PORTAL_STATUS = 1,
    ZYL_NETWORK_NO_INTERNET_STATUS = 2,
} ZylNetworkConnStatus;
typedef struct {
    ZylNetworkConnStatus status;
    char portal_url[512];
} ZylCaptiveResult;
extern ZylCaptiveResult zyl_captive_portal_check(void);

#define MAX_NETWORKS 64

struct ZylWifiService {
    GDBusConnection *session_bus;
    guint dbus_owner_id;
};

/* ─── Hotspot policy ─── */
static void random_text(char *out, size_t n, const char *alphabet) {
    unsigned char r[64];
    size_t k = strlen(alphabet);
    if (n > sizeof(r)) n = sizeof(r);
    if (getrandom(r, n, 0) != (ssize_t)n) memset(r, 0, n);
    for (size_t i = 0; i < n; i++) out[i] = alphabet[r[i] % k];
    out[n] = '\0';
}

const char *zyl_wifi_hotspot_credentials(const char *ssid, const char *passphrase,
                                         char *ssid_out, size_t ssid_len,
                                         char *pass_out, size_t pass_len) {
    if (ssid && *ssid) {
        if (strlen(ssid) > 32) return "ssid";           /* 802.11: at most 32 bytes */
        g_strlcpy(ssid_out, ssid, ssid_len);
    } else {
        char tail[5];
        random_text(tail, 4, "0123456789ABCDEF");
        snprintf(ssid_out, ssid_len, "ZylOS-%s", tail);
    }
    if (passphrase && *passphrase) {
        size_t n = strlen(passphrase);
        if (n < 8 || n > 63) return "passphrase";       /* WPA2-PSK passphrase */
        for (size_t i = 0; i < n; i++)
            if (passphrase[i] < 0x20 || passphrase[i] > 0x7e) return "passphrase";
        g_strlcpy(pass_out, passphrase, pass_len);
    } else {
        /* No easily confused characters: it is read off one screen and typed on another */
        random_text(pass_out, pass_len - 1 < 12 ? pass_len - 1 : 12,
                    "abcdefghjkmnpqrstuvwxyz23456789");
    }
    return NULL;
}

/* ─── D-Bus ─── */

static const char *wifi_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_WIFI_DBUS_NAME "'>"
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
    "    <method name='Scan'/>"
    "    <method name='GetNetworks'>"
    "      <arg type='aa{sv}' name='networks' direction='out'/>"
    "    </method>"
    "    <method name='Connect'>"
    "      <arg type='s' name='ssid' direction='in'/>"
    "      <arg type='s' name='passphrase' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Disconnect'/>"
    "    <method name='GetHotspot'>"
    "      <arg type='a{sv}' name='hotspot' direction='out'/>"
    "    </method>"
    "    <method name='SetHotspot'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "      <arg type='s' name='ssid' direction='in'/>"
    "      <arg type='s' name='passphrase' direction='in'/>"
    "      <arg type='a{sv}' name='hotspot' direction='out'/>"
    "    </method>"
    "    <signal name='CaptivePortalDetected'>"
    "      <arg type='s' name='portal_url'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* Same shape as the emulator's wifi service */
static GVariant *hotspot_variant(bool ok, const char *reason) {
    ZylHotspotState st;
    zyl_hal_wifi()->hotspot_get(&st);
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "ok", g_variant_new_boolean(ok));
    g_variant_builder_add(&d, "{sv}", "reason", g_variant_new_string(reason));
    g_variant_builder_add(&d, "{sv}", "active", g_variant_new_boolean(st.active));
    g_variant_builder_add(&d, "{sv}", "ssid", g_variant_new_string(st.ssid));
    g_variant_builder_add(&d, "{sv}", "passphrase", g_variant_new_string(st.passphrase));
    return g_variant_new("(a{sv})", &d);
}

static GVariant *set_hotspot(bool on, const char *ssid, const char *passphrase) {
    const ZylWifiHal *wifi = zyl_hal_wifi();
    if (!on) return hotspot_variant(wifi->hotspot_stop(), "");
    if (!wifi->hotspot_supported()) return hotspot_variant(false, "unsupported");
    char name[64], pass[64];
    const char *bad = zyl_wifi_hotspot_credentials(ssid, passphrase, name, sizeof(name), pass, sizeof(pass));
    if (bad) return hotspot_variant(false, bad);
    bool ok = wifi->hotspot_start(name, pass);
    return hotspot_variant(ok, ok ? "" : "failed");
}

static void after_connect(ZylWifiService *svc) {
    ZylCaptiveResult captive = zyl_captive_portal_check();
    if (captive.status == ZYL_NETWORK_CAPTIVE_PORTAL_STATUS && svc->session_bus) {
        /* Apps and the UI show the portal's sign-in page */
        g_dbus_connection_emit_signal(svc->session_bus, NULL, ZYL_WIFI_DBUS_PATH,
            ZYL_WIFI_DBUS_NAME, "CaptivePortalDetected",
            g_variant_new("(s)", captive.portal_url), NULL);
    }
}

static void handle_wifi_method(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *method, GVariant *params,
                                GDBusMethodInvocation *inv, gpointer data) {
    ZylWifiService *svc = data;
    const ZylWifiHal *wifi = zyl_hal_wifi();
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "GetCapabilities") == 0) {
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "present", g_variant_new_boolean(wifi->present()));
        g_variant_builder_add(&d, "{sv}", "hotspot", g_variant_new_boolean(wifi->hotspot_supported()));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "GetState") == 0) {
        ZylWifiNetwork nets[MAX_NETWORKS];
        int n = wifi->list_networks(nets, MAX_NETWORKS);
        const char *ssid = "";
        for (int i = 0; i < n; i++) if (nets[i].connected) ssid = nets[i].ssid;
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "enabled", g_variant_new_boolean(wifi->get_enabled()));
        g_variant_builder_add(&d, "{sv}", "connected", g_variant_new_boolean(*ssid != '\0'));
        g_variant_builder_add(&d, "{sv}", "ssid", g_variant_new_string(ssid));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "SetEnabled") == 0) {
        gboolean en = FALSE;
        g_variant_get(params, "(b)", &en);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", wifi->set_enabled(en)));
    } else if (g_strcmp0(method, "Scan") == 0) {
        wifi->scan();
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetNetworks") == 0) {
        /* Same fields as the emulator: { ssid, signal (dBm), security, connected, bssid } */
        ZylWifiNetwork nets[MAX_NETWORKS];
        int n = wifi->list_networks(nets, MAX_NETWORKS);
        GVariantBuilder list;
        g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
        for (int i = 0; i < n; i++) {
            GVariantBuilder d;
            g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&d, "{sv}", "ssid", g_variant_new_string(nets[i].ssid));
            g_variant_builder_add(&d, "{sv}", "signal", g_variant_new_int32(nets[i].signal_dbm));
            g_variant_builder_add(&d, "{sv}", "security", g_variant_new_string(nets[i].security));
            g_variant_builder_add(&d, "{sv}", "connected", g_variant_new_boolean(nets[i].connected));
            g_variant_builder_add(&d, "{sv}", "bssid", g_variant_new_string(nets[i].bssid));
            g_variant_builder_add_value(&list, g_variant_builder_end(&d));
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(aa{sv})", &list));
    } else if (g_strcmp0(method, "Connect") == 0) {
        const gchar *ssid, *pass;
        g_variant_get(params, "(&s&s)", &ssid, &pass);
        bool ok = wifi->get_enabled() && wifi->connect(ssid, pass);
        if (ok) after_connect(svc);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ok));
    } else if (g_strcmp0(method, "Disconnect") == 0) {
        wifi->disconnect();
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetHotspot") == 0) {
        g_dbus_method_invocation_return_value(inv, hotspot_variant(true, ""));
    } else if (g_strcmp0(method, "SetHotspot") == 0) {
        gboolean on = FALSE;
        const gchar *ssid = NULL, *pass = NULL;
        g_variant_get(params, "(b&s&s)", &on, &ssid, &pass);
        g_dbus_method_invocation_return_value(inv, set_hotspot(on, ssid, pass));
    }
}

static const GDBusInterfaceVTable wifi_vtable = {
    .method_call = handle_wifi_method
};

static void on_wifi_bus(GDBusConnection *conn, const gchar *name,
                         gpointer data) {
    ZylWifiService *svc = data;
    (void)name;
    svc->session_bus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        wifi_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_WIFI_DBUS_PATH,
            info->interfaces[0], &wifi_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[WiFi] D-Bus registered: %s", ZYL_WIFI_DBUS_NAME);
}

/* ─── Public API ─── */

ZylWifiService *zyl_wifi_create(void) {
    ZylWifiService *svc = g_new0(ZylWifiService, 1);
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_WIFI_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_wifi_bus, NULL, NULL, svc, NULL);
    return svc;
}

void zyl_wifi_destroy(ZylWifiService *svc) {
    if (!svc) return;
    g_bus_unown_name(svc->dbus_owner_id);
    g_free(svc);
}

/* ─── 데몬 진입점 ─── */

#ifndef ZYL_TEST
static GMainLoop *g_wifi_loop = NULL;

static gboolean on_signal_wifi(gpointer data) {
    (void)data;
    g_message("[WiFi] Signal received, shutting down");
    if (g_wifi_loop) g_main_loop_quit(g_wifi_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ZylWifiService *svc = zyl_wifi_create();
    g_message("[WiFi] Zyl OS WiFi Service started");
    g_wifi_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_wifi, NULL);
    g_unix_signal_add(SIGINT,  on_signal_wifi, NULL);
    g_main_loop_run(g_wifi_loop);
    g_main_loop_unref(g_wifi_loop);
    zyl_wifi_destroy(svc);
    return 0;
}
#endif /* ZYL_TEST */
