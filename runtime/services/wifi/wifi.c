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

        /* Parse: SSID:SIGNAL:SECURITY:ACTIVE:BSSID */
        char *fields[5] = {NULL};
        int fi = 0;
        char *tok = line;
        for (char *p = line; *p && fi < 5; p++) {
            if (*p == ':') {
                *p = '\0';
                fields[fi++] = tok;
                tok = p + 1;
            }
        }
        if (fi < 4) continue;
        fields[fi] = tok;

        if (n >= cap) {
            cap *= 2;
            ZylWifiNetwork *tmp = realloc(nets, cap * sizeof(ZylWifiNetwork));
            if (!tmp) break;
            nets = tmp;
        }

        nets[n].ssid = strdup(fields[0] ? fields[0] : "");
        nets[n].signal = fields[1] ? -(100 - atoi(fields[1])) : -80;
        nets[n].security = strdup(fields[2] ? fields[2] : "Open");
        nets[n].connected = (fields[3] && strcmp(fields[3], "yes") == 0);
        nets[n].bssid = strdup(fields[4] ? fields[4] : "");
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
    return nmcli_connect(ssid, passphrase);
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
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ZylWifiService *svc = zyl_wifi_create();
    if (!svc) { g_critical("[WiFi] Failed to create service"); return 1; }
    g_message("[WiFi] Zyl OS WiFi Service started (NetworkManager)");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    zyl_wifi_destroy(svc);
    return 0;
}
