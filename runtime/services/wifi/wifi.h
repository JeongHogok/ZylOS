/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: WiFi 서비스 인터페이스 — 스캔, 연결, 상태 조회
 * 수행범위: NetworkManager D-Bus 통합, WiFi 스캔/연결/해제
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — WiFi 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WIFI_H
#define ZYL_WIFI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *ssid;
    int   signal;      /* dBm */
    char *security;    /* "WPA2", "WPA3", "Open" */
    bool  connected;
    char *bssid;
} ZylWifiNetwork;

typedef struct ZylWifiService ZylWifiService;

ZylWifiService *zyl_wifi_create(void);
void             zyl_wifi_destroy(ZylWifiService *svc);

int  zyl_wifi_scan(ZylWifiService *svc);
int  zyl_wifi_get_networks(ZylWifiService *svc, ZylWifiNetwork **out,
                            int *count);
int  zyl_wifi_connect(ZylWifiService *svc, const char *ssid,
                       const char *passphrase);
int  zyl_wifi_disconnect(ZylWifiService *svc);
bool zyl_wifi_is_enabled(ZylWifiService *svc);
void zyl_wifi_set_enabled(ZylWifiService *svc, bool enabled);
void zyl_wifi_network_free(ZylWifiNetwork *networks, int count);

#define ZYL_WIFI_DBUS_NAME "org.zylos.WifiService"
#define ZYL_WIFI_DBUS_PATH "/org/zylos/WifiService"

#endif /* ZYL_WIFI_H */
