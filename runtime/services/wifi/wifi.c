#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: WiFi 서비스 — NetworkManager D-Bus 통합
 * 수행범위: nmcli 래퍼를 통한 스캔, 연결, 상태 조회
 * 의존방향: wifi.h, gio/gio.h
 * SOLID: SRP — WiFi 네트워크 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <spawn.h>
#include <gio/gio.h>
#include <glib-unix.h>

/* Include captive portal detection (forward declaration only — module in captive_portal.c) */
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

#define NM_BUS  "org.freedesktop.NetworkManager"
#define NM_PATH "/org/freedesktop/NetworkManager"

/* Safe command execution without system() — no shell interpretation */
static int safe_exec(const char *const argv[]) {
    pid_t pid;
    char *safe_env[] = { "PATH=/usr/bin:/bin", "HOME=/tmp", NULL };
    int rc = posix_spawn(&pid, argv[0], NULL, NULL,
                         (char *const *)argv, safe_env);
    if (rc != 0) return -1;
    int status;
    if (waitpid(pid, &status, 0) == -1) return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

struct ZylWifiService {
    GDBusConnection *system_bus;
    GDBusConnection *session_bus;
    guint dbus_owner_id;
    bool enabled;
};

/* ─── nmcli wrapper (portable, no direct NM D-Bus complexity) ─── */

static int nmcli_scan(void) {
    const char *argv[] = { "/usr/bin/nmcli", "device", "wifi", "rescan", NULL };
    return safe_exec(argv);
}

static int nmcli_connect(const char *ssid, const char *pass) {
    if (pass && strlen(pass) > 0) {
        const char *argv[] = { "/usr/bin/nmcli", "device", "wifi", "connect",
                               ssid, "password", pass, NULL };
        return safe_exec(argv);
    } else {
        const char *argv[] = { "/usr/bin/nmcli", "device", "wifi", "connect",
                               ssid, NULL };
        return safe_exec(argv);
    }
}

static int nmcli_disconnect(void) {
    const char *argv[] = { "/usr/bin/nmcli", "device", "disconnect",
                           "wlan0", NULL };
    return safe_exec(argv);
}

/* ─── D-Bus interface ─── */

static const char *wifi_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_WIFI_DBUS_NAME "'>"
    "    <method name='Scan'/>"
    "    <method name='Connect'>"
    "      <arg type='s' name='ssid' direction='in'/>"
    "      <arg type='s' name='passphrase' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Disconnect'/>"
    "    <method name='IsEnabled'>"
    "      <arg type='b' name='enabled' direction='out'/>"
    "    </method>"
    "    <method name='SetEnabled'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "    </method>"
    "    <signal name='CaptivePortalDetected'>"
    "      <arg type='s' name='portal_url'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static void handle_wifi_method(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *method, GVariant *params,
                                GDBusMethodInvocation *inv, gpointer data) {
    ZylWifiService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "Scan") == 0) {
        zyl_wifi_scan(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Connect") == 0) {
        const gchar *ssid, *pass;
        g_variant_get(params, "(&s&s)", &ssid, &pass);
        int ret = zyl_wifi_connect(svc, ssid, pass);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "Disconnect") == 0) {
        zyl_wifi_disconnect(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "IsEnabled") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", zyl_wifi_is_enabled(svc)));
    } else if (g_strcmp0(method, "SetEnabled") == 0) {
        gboolean en;
        g_variant_get(params, "(b)", &en);
        zyl_wifi_set_enabled(svc, en);
        g_dbus_method_invocation_return_value(inv, NULL);
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
    ZylWifiService *svc = calloc(1, sizeof(ZylWifiService));
    if (!svc) return NULL;
    svc->enabled = true;

    GError *err = NULL;
    svc->system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (err) { g_error_free(err); }

    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_WIFI_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_wifi_bus, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_wifi_destroy(ZylWifiService *svc) {
    if (!svc) return;
    g_bus_unown_name(svc->dbus_owner_id);
    if (svc->system_bus) g_object_unref(svc->system_bus);
    free(svc);
}

int zyl_wifi_scan(ZylWifiService *svc) {
    if (!svc || !svc->enabled) return -1;
    return nmcli_scan();
}

int zyl_wifi_get_networks(ZylWifiService *svc, ZylWifiNetwork **out,
                           int *count) {
    if (!svc || !out || !count) return -1;
    *out = NULL; *count = 0;

    FILE *fp = popen(
        "nmcli -t -f SSID,SIGNAL,SECURITY,ACTIVE,BSSID dev wifi list "
        "2>/dev/null", "r");
    if (!fp) return -1;

    int cap = 16, n = 0;
    ZylWifiNetwork *nets = calloc(cap, sizeof(ZylWifiNetwork));
    if (!nets) { pclose(fp); return -1; }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        /*
         * Parse nmcli -t output: SSID:SIGNAL:SECURITY:ACTIVE:BSSID
         * SSID may contain ':' — so we parse from the RIGHT for known fields.
         * Fields in reverse order from end: BSSID (17 chars + colons),
         * ACTIVE (yes/no), SECURITY, SIGNAL (integer).
         * Everything before the 4th-from-right ':' separator is SSID.
         *
         * Strategy: find the last 4 unescaped ':' separators.
         * nmcli uses backslash-escaping for ':' in field values.
         */
        const char *p = line + strlen(line);
        const char *seps[4] = {NULL, NULL, NULL, NULL};
        int found = 0;
        while (p > line && found < 4) {
            p--;
            if (*p == ':') {
                /* Check it's not escaped (nmcli escapes with \) */
                if (p == line || *(p-1) != '\\') {
                    seps[3 - found] = p;
                    found++;
                }
            }
        }
        if (found < 4) continue;

        /* Extract fields from right */
        const char *bssid_start    = seps[3] + 1;
        const char *active_start   = seps[2] + 1;
        const char *security_start = seps[1] + 1;
        const char *signal_start   = seps[0] + 1;
        size_t      ssid_len       = (size_t)(seps[0] - line);
        size_t      security_len   = (size_t)(seps[2] - security_start);
        size_t      active_len     = (size_t)(seps[3] - active_start);

        /* Signal: nmcli outputs 0-100; convert to dBm approximation (-100..-30) */
        int signal_pct = atoi(signal_start);
        int signal_dbm = (signal_pct <= 0) ? -100 :
                         (signal_pct >= 100) ? -30 :
                         (-100 + signal_pct * 70 / 100);

        /* active/inactive */
        bool connected = (active_len == 3 && strncmp(active_start, "yes", 3) == 0);

        if (n >= cap) {
            cap *= 2;
            ZylWifiNetwork *tmp = realloc(nets, cap * sizeof(ZylWifiNetwork));
            if (!tmp) break;
            nets = tmp;
        }

        char ssid_buf[256] = {0};
        if (ssid_len > 0 && ssid_len < sizeof(ssid_buf)) {
            memcpy(ssid_buf, line, ssid_len);
        }
        /* Unescape nmcli backslash-colon sequences in SSID */
        for (char *sp = ssid_buf; *sp; sp++) {
            if (*sp == '\\' && *(sp+1) == ':') {
                memmove(sp, sp+1, strlen(sp+1)+1);
            }
        }

        char sec_buf[128] = {0};
        if (security_len > 0 && security_len < sizeof(sec_buf)) {
            memcpy(sec_buf, security_start, security_len);
            sec_buf[security_len] = '\0';
        }

        nets[n].ssid = strdup(ssid_buf);
        nets[n].signal = signal_dbm;
        nets[n].security = strdup(sec_buf[0] ? sec_buf : "Open");
        nets[n].connected = connected;
        nets[n].bssid = strdup(bssid_start);
        n++;
    }
    pclose(fp);

    *out = nets;
    *count = n;
    return 0;
}

int zyl_wifi_connect(ZylWifiService *svc, const char *ssid,
                      const char *passphrase) {
    if (!svc || !ssid || !svc->enabled) return -1;
    g_message("[WiFi] Connecting to: %s", ssid);
    int rc = nmcli_connect(ssid, passphrase);
    if (rc == 0) {
        /* Post-connect: check for captive portal */
        ZylCaptiveResult captive = zyl_captive_portal_check();
        if (captive.status == ZYL_NETWORK_CAPTIVE_PORTAL_STATUS) {
            g_message("[WiFi] Captive portal detected: %s", captive.portal_url);
            /* Emit D-Bus signal so apps/UI can show the portal browser */
            if (svc->session_bus) {
                g_dbus_connection_emit_signal(svc->session_bus, NULL,
                    ZYL_WIFI_DBUS_PATH, ZYL_WIFI_DBUS_NAME,
                    "CaptivePortalDetected",
                    g_variant_new("(s)", captive.portal_url),
                    NULL);
            }
        } else if (captive.status == ZYL_NETWORK_NO_INTERNET_STATUS) {
            g_message("[WiFi] Connected but no internet access");
        }
    }
    return rc;
}

int zyl_wifi_disconnect(ZylWifiService *svc) {
    if (!svc) return -1;
    return nmcli_disconnect();
}

bool zyl_wifi_is_enabled(ZylWifiService *svc) {
    if (!svc) return false;
    return svc->enabled;
}

void zyl_wifi_set_enabled(ZylWifiService *svc, bool enabled) {
    if (!svc) return;
    svc->enabled = enabled;
    const char *argv[] = { "/usr/bin/nmcli", "radio", "wifi",
                           enabled ? "on" : "off", NULL };
    safe_exec(argv);
    g_message("[WiFi] %s", enabled ? "Enabled" : "Disabled");
}

void zyl_wifi_network_free(ZylWifiNetwork *networks, int count) {
    if (!networks) return;
    for (int i = 0; i < count; i++) {
        free(networks[i].ssid);
        free(networks[i].security);
        free(networks[i].bssid);
    }
    free(networks);
}

/* ─── 데몬 진입점 ─── */

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
    if (!svc) { g_critical("[WiFi] Failed to create service"); return 1; }
    g_message("[WiFi] Zyl OS WiFi Service started (NetworkManager)");
    g_wifi_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_wifi, NULL);
    g_unix_signal_add(SIGINT,  on_signal_wifi, NULL);
    g_main_loop_run(g_wifi_loop);
    g_main_loop_unref(g_wifi_loop);
    zyl_wifi_destroy(svc);
    return 0;
}
