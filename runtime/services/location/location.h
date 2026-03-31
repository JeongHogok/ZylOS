/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 위치 서비스 인터페이스 — GPS, 네트워크, WiFi 삼각측량, 융합 위치 제공
 * 수행범위: GPSD 연동, GeoIP 폴백, WiFi BSS RSSI 삼각측량,
 *          위치 업데이트 콜백, 지오펜스 등록/제거/진입출 시그널
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 위치 데이터 조회/구독/지오펜스 인터페이스만 노출
 *
 * 실기기: GPSD (libgps) + GeoIP HTTP 폴백 + wpa_supplicant BSS
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

/* ─── 지오펜스 ─── */

/**
 * ZylGeofence: 원형 지오펜스 정의.
 * tag 는 고유 식별자 (NULL 불가).
 */
typedef struct {
    double  lat;        /* 중심 위도 (WGS-84 도) */
    double  lon;        /* 중심 경도 (WGS-84 도) */
    double  radius_m;   /* 반경 (미터) */
    char   *tag;        /* 식별자 (내부에서 strdup 복사) */
} ZylGeofence;

/**
 * zyl_location_add_geofence: 지오펜스 등록.
 * @return 0=성공, -1=오류 (NULL 파라미터, 용량 초과 등)
 */
int zyl_location_add_geofence(ZylLocationService *svc,
                               const ZylGeofence *fence);

/**
 * zyl_location_remove_geofence: 지오펜스 제거.
 * @param tag 등록 시 지정한 식별자
 * @return 0=성공, -1=없음
 */
int zyl_location_remove_geofence(ZylLocationService *svc,
                                  const char *tag);

/* D-Bus 상수 */
#define ZYL_LOCATION_DBUS_NAME "org.zylos.LocationService"
#define ZYL_LOCATION_DBUS_PATH "/org/zylos/LocationService"

/* 지오펜스 최대 개수 */
#define ZYL_GEOFENCE_MAX 32

#endif /* ZYL_LOCATION_H */
