/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: Wi-Fi 서비스 인터페이스 — 무선 연결과 핫스팟 정책
 * 수행범위: 서비스 생성·해제, 핫스팟 이름·암호 규칙(검증·생성)
 * 의존방향: stdbool.h, hal.h (네트워크·핫스팟 자료형)
 * SOLID: ISP — 서비스 수명과 정책만 노출. 장치 접근은 Wi-Fi HAL
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WIFI_H
#define ZYL_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include "hal.h"

typedef struct ZylWifiService ZylWifiService;

ZylWifiService *zyl_wifi_create(void);
void            zyl_wifi_destroy(ZylWifiService *svc);

/* Hotspot name and WPA2 passphrase as the user gave them, or made up when
   empty. NULL when acceptable, else what is wrong: "ssid" or "passphrase" */
const char *zyl_wifi_hotspot_credentials(const char *ssid, const char *passphrase,
                                         char *ssid_out, size_t ssid_len,
                                         char *pass_out, size_t pass_len);

#define ZYL_WIFI_DBUS_NAME "org.zylos.WifiService"
#define ZYL_WIFI_DBUS_PATH "/org/zylos/WifiService"

#endif /* ZYL_WIFI_H */
