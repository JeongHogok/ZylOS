/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Internal Interface
 *
 * 역할: 위치 서비스 내부 공유 헤더 — 타입 정의, 상수, 프로바이더 함수 선언
 * 수행범위: ZylLocationService 구조체 정의, WifiBss/GeofenceEntry 타입,
 *          GPS/GeoIP/WiFi 프로바이더 함수 선언, 유틸리티 함수
 * 의존방향: location.h, gio/gio.h, pthread.h
 * SOLID: ISP — 내부 모듈 간 최소 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_LOCATION_INTERNAL_H
#define ZYL_LOCATION_INTERNAL_H

#include "location.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <gio/gio.h>

#ifdef HAVE_GPSD
#include <gps.h>
#endif

/* ─── 내부 상수 ─── */
#define GEOIP_URL           "http://ip-api.com/json/?fields=lat,lon,city,query"
#define GPSD_HOST           "localhost"
#define GPSD_PORT           "2947"
#define NETWORK_ACCURACY_M  5000.0f    /* GeoIP 정확도 추정치 */
#define WIFI_ACCURACY_M     150.0f     /* WiFi 삼각측량 정확도 추정치 */
#define WPA_CTRL_PATH       "/var/run/wpa_supplicant" /* wpa_supplicant 소켓 디렉토리 */

/* ─── WiFi BSS 항목 (wpa_supplicant BSS 결과) ─── */
typedef struct {
    char   bssid[18];   /* MAC 주소 문자열 */
    int    rssi_dbm;    /* 수신 신호 강도 (dBm, 음수) */
    double lat;         /* 알려진 위도 (0이면 미등록) */
    double lon;         /* 알려진 경도 */
} WifiBss;

/* ─── 지오펜스 내부 상태 ─── */
typedef struct {
    ZylGeofence fence;
    bool        inside;  /* 직전 폴링 시 내부 여부 */
    bool        active;  /* 등록됨 여부 */
} GeofenceEntry;

/* ─── 내부 구조체 ─── */
struct ZylLocationService {
    /* 최신 위치 */
    ZylLocation last_known;
    bool has_fix;

    /* 업데이트 콜백 */
    zyl_location_callback_fn cb;
    void *cb_data;
    int interval_ms;
    bool updates_active;

    /* GPSD */
#ifdef HAVE_GPSD
    struct gps_data_t gps_data;
    bool gpsd_connected;
#endif

    /* 폴링 스레드 */
    pthread_t poll_thread;
    bool thread_running;
    pthread_mutex_t lock;

    /* D-Bus */
    GDBusConnection *dbus;
    guint dbus_owner_id;

    /* 지오펜스 */
    GeofenceEntry geofences[ZYL_GEOFENCE_MAX];
    int geofence_count;
};

/* ─── 유틸리티 ─── */
uint64_t now_ms(void);

/* ─── 프로바이더 함수 ─── */
#ifdef HAVE_GPSD
bool gpsd_connect(ZylLocationService *svc);
bool gpsd_read(ZylLocationService *svc, ZylLocation *loc);
void gpsd_disconnect(ZylLocationService *svc);
#endif

bool geoip_query(ZylLocation *loc);
bool wifi_triangulate(ZylLocationService *svc, ZylLocation *out);

#endif /* ZYL_LOCATION_INTERNAL_H */
