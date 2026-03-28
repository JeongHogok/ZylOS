/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 위치 서비스 인터페이스 — GPS, 네트워크, 융합 위치 제공
 * 수행범위: GPSD 연동, GeoIP 폴백, 위치 업데이트 콜백, 권한 검사
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 위치 데이터 조회/구독 인터페이스만 노출
 *
 * 실기기: GPSD (libgps) + GeoIP HTTP 폴백
 * 에뮬레이터: JS로 시뮬레이션
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_LOCATION_H
#define ZYL_LOCATION_H

#include <stdbool.h>
#include <stdint.h>

/* ─── 위치 데이터 ─── */
typedef struct {
    double latitude;             /* 위도 (WGS-84 도) */
    double longitude;            /* 경도 (WGS-84 도) */
    double altitude_m;           /* 고도 (m, 해수면 기준) */
    float accuracy_m;            /* 수평 정확도 (m) */
    float speed_mps;             /* 속력 (m/s) */
    float bearing_deg;           /* 방위각 (도, 0=북) */
    uint64_t timestamp_ms;       /* UNIX epoch 밀리초 */
    char provider[16];           /* "gps", "network", "fused" */
} ZylLocation;

/* ─── 콜백 ─── */
typedef void (*zyl_location_callback_fn)(const ZylLocation *loc,
                                          void *user_data);

/* ─── 서비스 인터페이스 ─── */
typedef struct ZylLocationService ZylLocationService;

/* 서비스 생성/해제 */
ZylLocationService *zyl_location_create(void);
void                zyl_location_destroy(ZylLocationService *svc);

/* 주기적 업데이트 요청 */
int  zyl_location_request_updates(ZylLocationService *svc,
                                   int interval_ms,
                                   zyl_location_callback_fn cb,
                                   void *user_data);

/* 업데이트 중지 */
void zyl_location_stop_updates(ZylLocationService *svc);

/* 마지막으로 알려진 위치 조회 */
int  zyl_location_get_last_known(const ZylLocationService *svc,
                                  ZylLocation *out);

/* D-Bus 상수 */
#define ZYL_LOCATION_DBUS_NAME "org.zylos.LocationService"
#define ZYL_LOCATION_DBUS_PATH "/org/zylos/LocationService"

#endif /* ZYL_LOCATION_H */
