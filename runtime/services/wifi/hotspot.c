#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: WiFi 핫스팟 — AP 모드 활성화/비활성화
 * 수행범위: nmcli hotspot 생성, SSID/비밀번호 설정, 클라이언트 목록
 * 의존방향: stdio, spawn
 * SOLID: SRP — 핫스팟 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <spawn.h>

static int safe_exec(const char *const argv[]) {
    pid_t pid;
    char *env[] = { "PATH=/usr/bin:/bin:/sbin", NULL };
    int rc = posix_spawn(&pid, argv[0], NULL, NULL,
                         (char *const *)argv, env);
    if (rc != 0) return -1;
    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/**
 * WiFi 핫스팟 활성화.
 * @param ssid     SSID (NULL이면 "ZylOS-Hotspot")
 * @param password WPA2 비밀번호 (NULL이면 랜덤 생성)
 * @param band     "bg" (2.4GHz) 또는 "a" (5GHz)
 */
int zyl_hotspot_start(const char *ssid, const char *password,
                       const char *band) {
    const char *s = ssid ? ssid : "ZylOS-Hotspot";
    const char *b = band ? band : "bg";

    if (password && strlen(password) > 0) {
        const char *argv[] = {
            "/usr/bin/nmcli", "device", "wifi", "hotspot",
            "ssid", s, "password", password, "band", b, NULL
        };
        return safe_exec(argv);
    } else {
        const char *argv[] = {
            "/usr/bin/nmcli", "device", "wifi", "hotspot",
            "ssid", s, "band", b, NULL
        };
        return safe_exec(argv);
    }
}

/**
 * WiFi 핫스팟 비활성화.
 */
int zyl_hotspot_stop(void) {
    const char *argv[] = {
        "/usr/bin/nmcli", "connection", "down", "Hotspot", NULL
    };
    return safe_exec(argv);
}

/**
 * 핫스팟 활성 여부 확인.
 */
int zyl_hotspot_is_active(void) {
    FILE *fp = popen(
        "nmcli -t -f NAME,TYPE connection show --active 2>/dev/null | "
        "grep Hotspot", "r");
    if (!fp) return 0;
    char buf[128];
    int active = (fgets(buf, sizeof(buf), fp) != NULL);
    pclose(fp);
    return active;
}

/**
 * 핫스팟에 연결된 클라이언트 목록 조회.
 *
 * Returns the number of connected clients written to `out_macs`.
 * Each entry is a NUL-terminated MAC address string.
 * Caller must free each string and the array.
 *
 * Uses iw (nl80211) to list associated stations on the AP interface.
 * Falls back to parsing /proc/net/arp if iw is unavailable.
 *
 * @param out_macs  output array of strdup'd MAC address strings
 * @param max       maximum number of entries to write
 * @return          number of entries written, or -1 on error
 */
int zyl_hotspot_get_clients(char **out_macs, int max) {
    if (!out_macs || max <= 0) return -1;

    int n = 0;

    /* Try: iw dev ap0 station dump (or wlan0 in AP mode) */
    FILE *fp = popen(
        "iw dev 2>/dev/null | awk '/Interface/{iface=$2} "
        "/type AP/{print iface}' | head -1 | "
        "xargs -I{} iw dev {} station dump 2>/dev/null | "
        "awk '/^Station/{print $2}'", "r");
    if (fp) {
        char mac[32];
        while (fgets(mac, sizeof(mac), fp) && n < max) {
            size_t mlen = strlen(mac);
            if (mlen > 0 && mac[mlen-1] == '\n') mac[mlen-1] = '\0';
            if (mac[0]) {
                out_macs[n++] = strdup(mac);
            }
        }
        pclose(fp);
    }

    if (n == 0) {
        /*
         * Fallback: parse /proc/net/arp for entries on the hotspot
         * interface (typically ap0 or wlan0 when in AP mode).
         * ARP table entries for 192.168.x.x range are likely hotspot clients.
         */
        FILE *arp = fopen("/proc/net/arp", "r");
        if (arp) {
            char line[256];
            fgets(line, sizeof(line), arp); /* skip header */
            while (fgets(line, sizeof(line), arp) && n < max) {
                char ip[32], hw_type[8], flags[8], mac[32], mask[8], dev[32];
                if (sscanf(line, "%31s %7s %7s %31s %7s %31s",
                           ip, hw_type, flags, mac, mask, dev) == 6) {
                    /* Only include non-zero MAC entries */
                    if (strcmp(mac, "00:00:00:00:00:00") != 0 &&
                        strncmp(ip, "192.168.", 8) == 0) {
                        out_macs[n++] = strdup(mac);
                    }
                }
            }
            fclose(arp);
        }
    }

    return n;
}
