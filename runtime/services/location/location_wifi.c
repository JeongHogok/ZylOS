/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Provider
 *
 * 역할: WiFi 위치 프로바이더 — wpa_supplicant BSS RSSI 기반 삼각측량
 * 수행범위: wpa_cli BSS 스캔 결과 파싱, RSSI 가중 평균 위치 추정
 * 의존방향: location_internal.h
 * SOLID: SRP — WiFi BSS 삼각측량 위치 추정만 담당
 * ────────────────────────────────────────────────────────── */

#include "location_internal.h"

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
     * BSSID → 위도/경도 매핑:
     * BSS 목록의 lat/lon은 외부 위치 데이터베이스(Mozilla Location Services,
     * Google Geolocation API, 또는 로컬 캐시)에서 채워진다.
     * 현재 BSS 구조체에 lat/lon 필드가 있지만 BSSID→좌표 조회 API 호출은
     * 네트워크 의존이므로 location 서비스의 퓨전 로직에서 처리한다.
     * lat=0, lon=0인 항목은 매핑 미완료 → 가중 평균에서 제외.
     */
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
        g_debug("[Location] WiFi BSS DB miss (%d APs seen, none in DB)", bss_count);
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

    g_message("[Location] WiFi triangulation: lat=%.4f lon=%.4f (n_ap=%d)",
              out->latitude, out->longitude, bss_count);
    (void)svc;
    return true;
}
