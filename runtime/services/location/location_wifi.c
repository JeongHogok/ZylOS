/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Provider
 *
 * 역할: WiFi 위치 프로바이더 — wpa_supplicant BSS RSSI 기반 삼각측량
 * 수행범위: wpa_cli BSS 스캔 결과 파싱, RSSI 가중 평균 위치 추정
 * 의존방향: location_internal.h
 * SOLID: SRP — WiFi BSS 삼각측량 위치 추정만 담당
 * ────────────────────────────────────────────────────────── */

#include "location_internal.h"
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

/* 간단한 RSSI → 선형 가중치 변환 (dBm → weight) */
static double rssi_to_weight(int rssi_dbm) {
    /* rssi 범위: -30(강) ~ -90(약)
       weight = 1 / distance_estimate^2
       Friis: distance ∝ 10^((A - rssi) / (10*n)), A=-40, n=3 */
    double exp_val = (-40.0 - (double)rssi_dbm) / 30.0;
    double distance = pow(10.0, exp_val);
    if (distance < 0.1) distance = 0.1;
    return 1.0 / (distance * distance);
}

/*
 * wpa_supplicant BSS 목록을 읽어 RSSI 기반 가중 평균 위치 추정.
 * BSSs 의 lat/lon 이 모두 0이면 (DB 미등록) false 반환.
 */
bool wifi_triangulate(ZylLocationService *svc, ZylLocation *out) {
    /*
     * wpa_supplicant 소켓 접근: wpa_cli -i wlan0 bss list
     * 소켓 직접 제어 대신 popen으로 간략 구현.
     * 실제 구현에서는 wpa_ctrl API를 사용한다.
     */
    FILE *fp = popen("wpa_cli -i wlan0 scan_results 2>/dev/null", "r");
    if (!fp) {
        g_debug("[Location] wpa_cli not available, skipping WiFi triangulation");
        return false;
    }

    /* wpa_cli scan_results 출력 형식:
       bssid / frequency / signal level / flags / ssid
       00:11:22:33:44:55  2412  -65  [WPA2-PSK-CCMP]  MyNet */

    WifiBss bss_list[64];
    int bss_count = 0;

    char line[256];
    /* 첫 헤더 라인 스킵 */
    if (fgets(line, sizeof(line), fp) == NULL) {
        pclose(fp);
        return false;
    }

    while (fgets(line, sizeof(line), fp) && bss_count < 64) {
        WifiBss b;
        int freq = 0;
        char flags[128] = {0};
        char ssid[64]   = {0};
        /* bssid  freq  rssi  flags  ssid */
        if (sscanf(line, "%17s %d %d %127s %63s",
                   b.bssid, &freq, &b.rssi_dbm,
                   flags, ssid) >= 3) {
            b.lat = 0.0;
            b.lon = 0.0;
            bss_list[bss_count++] = b;
        }
        (void)ssid; (void)flags; (void)freq;
    }
    pclose(fp);

    if (bss_count == 0) return false;

    /*
     * Minimum-viable WiFi location via Mozilla Location Services (MLS).
     * Build a JSON POST body with BSSIDs + RSSI, send via curl (posix_spawn),
     * parse {location:{lat,lng}} response.
     * Falls through to local weighted average (lat/lon from local DB cache)
     * if MLS is unreachable or returns an error.
     */
#define MLS_URL "https://location.services.mozilla.com/v1/geolocate?key=geoclue"

    /* Build JSON request body */
    char req_body[8192];
    int body_pos = 0;
    body_pos += snprintf(req_body + body_pos,
                         sizeof(req_body) - (size_t)body_pos,
                         "{\"wifiAccessPoints\":[");
    for (int i = 0; i < bss_count; i++) {
        if (body_pos >= (int)sizeof(req_body) - 64) break;
        if (i > 0) req_body[body_pos++] = ',';
        body_pos += snprintf(req_body + body_pos,
                             sizeof(req_body) - (size_t)body_pos,
                             "{\"macAddress\":\"%s\",\"signalStrength\":%d}",
                             bss_list[i].bssid, bss_list[i].rssi_dbm);
    }
    if (body_pos < (int)sizeof(req_body) - 2)
        body_pos += snprintf(req_body + body_pos,
                             sizeof(req_body) - (size_t)body_pos, "]}");

    char tmp_req[] = "/tmp/zyl-mls-XXXXXX";
    int tmpfd = mkstemp(tmp_req);
    bool tried_mls = false;
    bool triangulated = false;

    if (tmpfd >= 0) {
        if (write(tmpfd, req_body, (size_t)body_pos) == body_pos) {
            close(tmpfd);
            tried_mls = true;

            int pfd[2];
            if (pipe(pfd) == 0) {
                pid_t curl_pid;
                posix_spawn_file_actions_t pacts;
                posix_spawn_file_actions_init(&pacts);
                posix_spawn_file_actions_adddup2(&pacts, pfd[1], STDOUT_FILENO);
                posix_spawn_file_actions_addclose(&pacts, pfd[0]);

                char data_arg[280];
                snprintf(data_arg, sizeof(data_arg), "@%s", tmp_req);
                const char *curl_argv[] = {
                    "/usr/bin/curl", "-s",
                    "--connect-timeout", "5", "--max-time", "10",
                    "-H", "Content-Type: application/json",
                    "-X", "POST", "-d", data_arg, MLS_URL, NULL
                };
                char *curl_env[] = { "PATH=/usr/bin:/bin", NULL };
                int curl_rc = posix_spawn(&curl_pid, "/usr/bin/curl",
                                          &pacts, NULL,
                                          (char *const *)curl_argv, curl_env);
                posix_spawn_file_actions_destroy(&pacts);
                close(pfd[1]);

                if (curl_rc == 0) {
                    char resp[2048] = {0};
                    ssize_t n = read(pfd[0], resp, sizeof(resp) - 1);
                    close(pfd[0]);
                    int wst = 0;
                    waitpid(curl_pid, &wst, 0);

                    if (n > 0) {
                        resp[n] = '\0';
                        const char *lat_p = strstr(resp, "\"lat\":");
                        const char *lng_p = strstr(resp, "\"lng\":");
                        if (lat_p && lng_p) {
                            double mlat = 0.0, mlng = 0.0;
                            if (sscanf(lat_p + 6, "%lf", &mlat) == 1 &&
                                sscanf(lng_p + 6, "%lf", &mlng) == 1 &&
                                !(mlat == 0.0 && mlng == 0.0)) {
                                out->latitude    = mlat;
                                out->longitude   = mlng;
                                out->altitude_m  = 0.0;
                                out->accuracy_m  = WIFI_ACCURACY_M;
                                out->speed_mps   = 0.0f;
                                out->bearing_deg = 0.0f;
                                out->timestamp_ms = now_ms();
                                snprintf(out->provider, sizeof(out->provider),
                                         "wifi");
                                g_message("[Location] MLS WiFi: lat=%.4f lon=%.4f"
                                          " (n_ap=%d)", mlat, mlng, bss_count);
                                triangulated = true;
                            }
                        }
                    }
                } else {
                    close(pfd[0]);
                }
            }
        } else {
            close(tmpfd);
        }
        unlink(tmp_req);
    }

    if (triangulated) {
        (void)svc; (void)tried_mls;
        return true;
    }

    /* Fallback: local weighted average (requires pre-populated lat/lon in DB) */
    double sum_w    = 0.0;
    double sum_wlat = 0.0;
    double sum_wlon = 0.0;

    for (int i = 0; i < bss_count; i++) {
        if (bss_list[i].lat == 0.0 && bss_list[i].lon == 0.0) continue;
        double w = rssi_to_weight(bss_list[i].rssi_dbm);
        sum_w    += w;
        sum_wlat += w * bss_list[i].lat;
        sum_wlon += w * bss_list[i].lon;
    }

    if (sum_w < 1e-9) {
        g_debug("[Location] WiFi: MLS unavailable and no local DB "
                "(%d APs seen, none in cache)", bss_count);
        (void)svc;
        return false;
    }

    out->latitude  = sum_wlat / sum_w;
    out->longitude = sum_wlon / sum_w;
    out->altitude_m = 0.0;
    out->accuracy_m = WIFI_ACCURACY_M;
    out->speed_mps  = 0.0f;
    out->bearing_deg = 0.0f;
    out->timestamp_ms = now_ms();
    snprintf(out->provider, sizeof(out->provider), "wifi");

    g_message("[Location] WiFi local DB: lat=%.4f lon=%.4f (n_ap=%d)",
              out->latitude, out->longitude, bss_count);
    (void)svc;
    return true;
}
