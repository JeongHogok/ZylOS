/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Wi-Fi HAL Linux 구현 — NetworkManager (nmcli)
 * 수행범위: Wi-Fi 장치 유무, 무선 켜고 끄기, 스캔, 네트워크 목록(nmcli -t 의
 *           \: \\ 이스케이프 복원, 숨은 네트워크 제외, 한 네트워크의 AP 여럿은
 *           한 줄 — 연결 중·가장 강한 신호), 연결·해제, 핫스팟(장치의 AP 모드
 *           지원 여부, IPv4·IPv6 공유로 시작, 중지, 현재 이름·암호)
 * 의존방향: hal.h (Domain), glib (g_spawn — 셸 없이)
 * SOLID: SRP — 무선 장치 접근만. 연결 후 포털 확인·입력 검증은 wifi 서비스 몫
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define HOTSPOT_CONNECTION "Hotspot"

/* nmcli on PATH; stdout kept when out != NULL */
static bool nm(const char *const argv[], char **out) {
    int status = -1;
    GSpawnFlags flags = G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL |
                        (out ? 0 : G_SPAWN_STDOUT_TO_DEV_NULL);
    return g_spawn_sync(NULL, (char **)argv, NULL, flags, NULL, NULL, out, NULL, &status, NULL) &&
           g_spawn_check_wait_status(status, NULL);
}

/* The first Wi-Fi interface NetworkManager knows */
static bool wifi_device(char *dev, size_t len) {
    const char *argv[] = { "nmcli", "-t", "-f", "DEVICE,TYPE", "device", NULL };
    char *out = NULL;
    bool found = false;
    if (nm(argv, &out)) {
        char **lines = g_strsplit(out, "\n", -1);
        for (int i = 0; lines[i] && !found; i++) {
            char *sep = strrchr(lines[i], ':');
            if (sep && strcmp(sep + 1, "wifi") == 0) {
                *sep = '\0';
                g_strlcpy(dev, lines[i], len);
                found = true;
            }
        }
        g_strfreev(lines);
    }
    g_free(out);
    return found;
}

static bool wifi_present(void) {
    char dev[64];
    return wifi_device(dev, sizeof(dev));
}

static bool wifi_get_enabled(void) {
    const char *argv[] = { "nmcli", "-t", "radio", "wifi", NULL };
    char *out = NULL;
    bool on = nm(argv, &out) && g_str_has_prefix(out, "enabled");
    g_free(out);
    return on;
}

static bool wifi_set_enabled(bool on) {
    const char *argv[] = { "nmcli", "radio", "wifi", on ? "on" : "off", NULL };
    return nm(argv, NULL);
}

static bool wifi_scan(void) {
    const char *argv[] = { "nmcli", "device", "wifi", "rescan", NULL };
    return nm(argv, NULL);
}

/* nmcli -t escapes ':' and '\' inside a value */
static void unescape(char *s) {
    for (char *p = s; *p; p++)
        if (*p == '\\' && (p[1] == ':' || p[1] == '\\')) memmove(p, p + 1, strlen(p + 1) + 1);
}

/* Splits one terse line at its unescaped ':' (in place); returns the field count */
static int split_terse(char *line, char **fields, int max) {
    int n = 0;
    fields[n++] = line;
    for (char *p = line; *p && n < max; p++) {
        if (*p == '\\' && p[1]) { p++; continue; }
        if (*p == ':') { *p = '\0'; fields[n++] = p + 1; }
    }
    return n;
}

static int wifi_list_networks(ZylWifiNetwork *out, int max) {
    const char *argv[] = { "nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY,ACTIVE,BSSID",
                           "device", "wifi", "list", NULL };
    char *text = NULL;
    if (!nm(argv, &text)) { g_free(text); return -1; }
    int n = 0;
    char **lines = g_strsplit(text, "\n", -1);
    for (int i = 0; lines[i] && n < max; i++) {
        char *f[5];
        if (split_terse(lines[i], f, 5) != 5) continue;
        for (int k = 0; k < 5; k++) unescape(f[k]);
        /* Hidden networks have no name to show */
        if (!f[0][0]) continue;
        int pct = CLAMP(atoi(f[1]), 0, 100);
        int dbm = -100 + pct * 70 / 100;              /* nmcli reports 0-100 */
        bool connected = strcmp(f[3], "yes") == 0;
        /* Each access point of a network is its own line; to the user they are one network */
        int same = -1;
        for (int k = 0; k < n && same < 0; k++) if (strcmp(out[k].ssid, f[0]) == 0) same = k;
        if (same >= 0) {
            if (connected) out[same].connected = true;
            if (dbm > out[same].signal_dbm) out[same].signal_dbm = dbm;
            continue;
        }
        ZylWifiNetwork *w = &out[n++];
        memset(w, 0, sizeof(*w));
        g_strlcpy(w->ssid, f[0], sizeof(w->ssid));
        g_strlcpy(w->security, f[2][0] ? f[2] : "Open", sizeof(w->security));
        g_strlcpy(w->bssid, f[4], sizeof(w->bssid));
        w->signal_dbm = dbm;
        w->connected = connected;
    }
    g_strfreev(lines);
    g_free(text);
    return n;
}

/* shortcut: the passphrase is an nmcli argument for the moment it runs — move
   to the NetworkManager D-Bus API (AddAndActivateConnection) when processes
   other than the OS can list /proc */
static bool wifi_connect(const char *ssid, const char *passphrase) {
    if (passphrase && *passphrase) {
        const char *argv[] = { "nmcli", "device", "wifi", "connect", ssid, "password", passphrase, NULL };
        return nm(argv, NULL);
    }
    const char *argv[] = { "nmcli", "device", "wifi", "connect", ssid, NULL };
    return nm(argv, NULL);
}

static bool wifi_disconnect(void) {
    char dev[64];
    if (!wifi_device(dev, sizeof(dev))) return false;
    const char *argv[] = { "nmcli", "device", "disconnect", dev, NULL };
    return nm(argv, NULL);
}

/* ─── Hotspot ─── */
static bool wifi_hotspot_supported(void) {
    char dev[64];
    if (!wifi_device(dev, sizeof(dev))) return false;
    const char *argv[] = { "nmcli", "-t", "-f", "WIFI-PROPERTIES.AP", "device", "show", dev, NULL };
    char *out = NULL;
    bool ap = nm(argv, &out) && strstr(out, "WIFI-PROPERTIES.AP:yes");
    g_free(out);
    return ap;
}

/* A fresh profile each time: what the user typed last is what runs, and
   ipv6.method shared hands out IPv6 too (nmcli's own hotspot shares IPv4 only) */
static bool wifi_hotspot_start(const char *ssid, const char *passphrase) {
    char dev[64];
    if (!wifi_device(dev, sizeof(dev))) return false;
    const char *del[] = { "nmcli", "connection", "delete", HOTSPOT_CONNECTION, NULL };
    nm(del, NULL);                                    /* none yet is fine */
    const char *add[] = { "nmcli", "connection", "add", "type", "wifi", "ifname", dev,
                          "con-name", HOTSPOT_CONNECTION, "autoconnect", "no", "ssid", ssid,
                          "802-11-wireless.mode", "ap",
                          "ipv4.method", "shared", "ipv6.method", "shared",
                          "wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.psk", passphrase, NULL };
    const char *up[] = { "nmcli", "connection", "up", HOTSPOT_CONNECTION, NULL };
    return nm(add, NULL) && nm(up, NULL);
}

static bool wifi_hotspot_stop(void) {
    const char *argv[] = { "nmcli", "connection", "down", HOTSPOT_CONNECTION, NULL };
    return nm(argv, NULL);
}

static bool get_value(const char *field, bool secret, char *out, size_t len) {
    const char *plain[] = { "nmcli", "-g", field, "connection", "show", HOTSPOT_CONNECTION, NULL };
    const char *secrets[] = { "nmcli", "-s", "-g", field, "connection", "show", HOTSPOT_CONNECTION, NULL };
    char *text = NULL;
    bool ok = nm(secret ? secrets : plain, &text);
    if (ok) g_strlcpy(out, g_strstrip(text), len);
    g_free(text);
    return ok;
}

static bool wifi_hotspot_get(ZylHotspotState *st) {
    memset(st, 0, sizeof(*st));
    const char *argv[] = { "nmcli", "-t", "-f", "NAME", "connection", "show", "--active", NULL };
    char *out = NULL;
    if (nm(argv, &out)) {
        char **lines = g_strsplit(out, "\n", -1);
        for (int i = 0; lines[i]; i++) if (strcmp(lines[i], HOTSPOT_CONNECTION) == 0) st->active = true;
        g_strfreev(lines);
    }
    g_free(out);
    /* No profile yet: nothing to show, which is not an error */
    if (get_value("802-11-wireless.ssid", false, st->ssid, sizeof(st->ssid)))
        get_value("802-11-wireless-security.psk", true, st->passphrase, sizeof(st->passphrase));
    return true;
}

static const ZylWifiHal wifi_hal = {
    .present           = wifi_present,
    .get_enabled       = wifi_get_enabled,
    .set_enabled       = wifi_set_enabled,
    .scan              = wifi_scan,
    .list_networks     = wifi_list_networks,
    .connect           = wifi_connect,
    .disconnect        = wifi_disconnect,
    .hotspot_supported = wifi_hotspot_supported,
    .hotspot_start     = wifi_hotspot_start,
    .hotspot_stop      = wifi_hotspot_stop,
    .hotspot_get       = wifi_hotspot_get,
};

const ZylWifiHal *zyl_hal_wifi(void) { return &wifi_hal; }
