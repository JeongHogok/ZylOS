#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 위치 서비스 — GPSD GPS, GeoIP 폴백, WiFi BSS 삼각측량, 융합 위치,
 *       지오펜스 진입/이탈 감지 및 D-Bus 시그널
 * 수행범위: libgps 연동, HTTP GeoIP 조회, wpa_supplicant BSS RSSI 삼각측량,
 *          주기적 업데이트, 지오펜스 배열 관리 + 주기적 체크 + D-Bus 시그널
 * 의존방향: location.h, gio/gio.h, libgps, libcurl
 * SOLID: SRP — 위치 데이터 수집 및 전달만 담당
 * ────────────────────────────────────────────────────────── */

#include "location.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <gio/gio.h>

/* GPSD 지원: 컴파일 시 -DHAVE_GPSD + -lgps */
#ifdef HAVE_GPSD
#include <gps.h>
#endif

/* cURL: GeoIP 폴백용 */
#ifdef HAVE_CURL
#include <curl/curl.h>
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

/* ─── 현재 시각 밀리초 ─── */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* ─── GPSD 연결 ─── */
#ifdef HAVE_GPSD
static bool gpsd_connect(ZylLocationService *svc) {
    if (gps_open(GPSD_HOST, GPSD_PORT, &svc->gps_data) != 0) {
        g_message("[Location] Cannot connect to GPSD at %s:%s",
                  GPSD_HOST, GPSD_PORT);
        return false;
    }
    if (gps_stream(&svc->gps_data, WATCH_ENABLE | WATCH_JSON, NULL) != 0) {
        g_warning("[Location] gps_stream() failed, closing GPSD connection");
        gps_close(&svc->gps_data);
        return false;
    }
    svc->gpsd_connected = true;
    g_message("[Location] Connected to GPSD");
    return true;
}

static bool gpsd_read(ZylLocationService *svc, ZylLocation *loc) {
    if (!svc->gpsd_connected) return false;

    if (!gps_waiting(&svc->gps_data, 500000)) /* 500ms timeout */
        return false;

    if (gps_read(&svc->gps_data, NULL, 0) == -1) {
        g_warning("[Location] GPSD read error");
        svc->gpsd_connected = false;
        return false;
    }

    if (svc->gps_data.fix.mode < MODE_2D)
        return false;

    loc->latitude = svc->gps_data.fix.latitude;
    loc->longitude = svc->gps_data.fix.longitude;
    loc->altitude_m = (svc->gps_data.fix.mode >= MODE_3D)
                      ? svc->gps_data.fix.altMSL : 0.0;
    loc->accuracy_m = (float)svc->gps_data.fix.eph;
    loc->speed_mps = (float)svc->gps_data.fix.speed;
    loc->bearing_deg = (float)svc->gps_data.fix.track;
    loc->timestamp_ms = now_ms();
    snprintf(loc->provider, sizeof(loc->provider), "gps");
    return true;
}

static void gpsd_disconnect(ZylLocationService *svc) {
    if (svc->gpsd_connected) {
        gps_stream(&svc->gps_data, WATCH_DISABLE, NULL);
        gps_close(&svc->gps_data);
        svc->gpsd_connected = false;
    }
}
#endif /* HAVE_GPSD */

/* ─── GeoIP HTTP 폴백 ─── */
#ifdef HAVE_CURL
typedef struct {
    char *data;
    size_t len;
} CurlBuffer;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb,
                             void *userdata) {
    CurlBuffer *buf = userdata;
    size_t total = size * nmemb;
    char *tmp = realloc(buf->data, buf->len + total + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static bool geoip_query(ZylLocation *loc) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    CurlBuffer buf = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, GEOIP_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || !buf.data) {
        free(buf.data);
        return false;
    }

    /* 간단한 JSON 파싱 (GLib JSON 없이) */
    double lat = 0.0, lon = 0.0;
    const char *p;

    p = strstr(buf.data, "\"lat\":");
    if (p) lat = atof(p + 6);
    p = strstr(buf.data, "\"lon\":");
    if (p) lon = atof(p + 6);

    free(buf.data);

    if (lat == 0.0 && lon == 0.0) return false;

    loc->latitude = lat;
    loc->longitude = lon;
    loc->altitude_m = 0.0;
    loc->accuracy_m = NETWORK_ACCURACY_M;
    loc->speed_mps = 0.0f;
    loc->bearing_deg = 0.0f;
    loc->timestamp_ms = now_ms();
    snprintf(loc->provider, sizeof(loc->provider), "network");

    g_message("[Location] GeoIP fix: lat=%.4f lon=%.4f", lat, lon);
    return true;
}
#else
static bool geoip_query(ZylLocation *loc) {
    (void)loc;
    g_message("[Location] GeoIP unavailable (built without libcurl)");
    return false;
}
#endif /* HAVE_CURL */

/* ─── WiFi 삼각측량 ─── */
/*
 * wpa_supplicant 소켓에 BSS 쿼리를 보내 스캔 결과를 읽어온다.
 * RSSI 기반 가중 평균으로 위치를 추정한다.
 *
 * 실제 BSS→위도/경도 매핑은 별도 BSSID 위치 DB가 필요하다.
 * 여기서는 구조 구현 + 가중 평균 알고리즘을 제공하며,
 * BSSID DB 연동은 외부 플러그인(예: Mozilla Location Services API)으로 확장 가능.
 */

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
static bool wifi_triangulate(ZylLocationService *svc, ZylLocation *out) {
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
     * BSSID → 위도/경도 매핑: 외부 Mozilla Location Services 또는
     * 로컬 캐시 DB 에서 조회 (여기서는 stub).
     * 실제 환경에서는 HTTP API 호출로 채운다.
     * stub: 모든 위도/경도가 0 → 추정 불가
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

/* ─── 지오펜스: Haversine 거리 계산 ─── */
#define DEG2RAD(d)  ((d) * M_PI / 180.0)
#define EARTH_R_M   6371000.0

static double haversine_m(double lat1, double lon1,
                           double lat2, double lon2) {
    double dlat = DEG2RAD(lat2 - lat1);
    double dlon = DEG2RAD(lon2 - lon1);
    double a = sin(dlat / 2.0) * sin(dlat / 2.0)
             + cos(DEG2RAD(lat1)) * cos(DEG2RAD(lat2))
             * sin(dlon / 2.0) * sin(dlon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return EARTH_R_M * c;
}

/* ─── 지오펜스 D-Bus 시그널 발신 ─── */
static void emit_geofence_signal(ZylLocationService *svc,
                                  const char *tag, bool entered) {
    if (!svc->dbus) return;
    g_dbus_connection_emit_signal(svc->dbus, NULL,
        ZYL_LOCATION_DBUS_PATH,
        ZYL_LOCATION_DBUS_NAME,
        entered ? "GeofenceEnter" : "GeofenceExit",
        g_variant_new("(s)", tag),
        NULL);
    g_message("[Location] Geofence %s: tag='%s'",
              entered ? "ENTER" : "EXIT", tag);
}

/* ─── 지오펜스 주기적 체크 (폴링 스레드에서 호출) ─── */
static void check_geofences(ZylLocationService *svc,
                             const ZylLocation *loc) {
    pthread_mutex_lock(&svc->lock);
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        GeofenceEntry *e = &svc->geofences[i];
        if (!e->active) continue;

        double dist = haversine_m(loc->latitude, loc->longitude,
                                   e->fence.lat, e->fence.lon);
        bool inside = (dist <= e->fence.radius_m);

        if (inside && !e->inside) {
            e->inside = true;
            pthread_mutex_unlock(&svc->lock);
            emit_geofence_signal(svc, e->fence.tag, true);
            pthread_mutex_lock(&svc->lock);
        } else if (!inside && e->inside) {
            e->inside = false;
            pthread_mutex_unlock(&svc->lock);
            emit_geofence_signal(svc, e->fence.tag, false);
            pthread_mutex_lock(&svc->lock);
        }
    }
    pthread_mutex_unlock(&svc->lock);
}

/* ─── D-Bus 시그널 발신 ─── */
static void emit_location_signal(ZylLocationService *svc,
                                  const ZylLocation *loc) {
    if (!svc->dbus) return;

    g_dbus_connection_emit_signal(svc->dbus, NULL,
        ZYL_LOCATION_DBUS_PATH,
        ZYL_LOCATION_DBUS_NAME,
        "LocationUpdated",
        g_variant_new("(ddddddt&s)",
                       loc->latitude, loc->longitude, loc->altitude_m,
                       (double)loc->accuracy_m, (double)loc->speed_mps,
                       (double)loc->bearing_deg,
                       (guint64)loc->timestamp_ms,
                       loc->provider),
        NULL);
}

/* ─── 폴링 스레드 ─── */
static void *location_poll_thread(void *arg) {
    ZylLocationService *svc = arg;
    g_message("[Location] Poll thread started (interval=%d ms)", svc->interval_ms);

    while (svc->thread_running) {
        ZylLocation loc = {0};
        bool got_fix = false;

        /* 1순위: GPSD (하드웨어 GPS) */
#ifdef HAVE_GPSD
        if (svc->gpsd_connected || gpsd_connect(svc)) {
            got_fix = gpsd_read(svc, &loc);
        }
#endif

        /* 2순위: WiFi BSS RSSI 삼각측량 */
        if (!got_fix) {
            got_fix = wifi_triangulate(svc, &loc);
        }

        /* 3순위: GeoIP 네트워크 폴백 */
        if (!got_fix) {
            got_fix = geoip_query(&loc);
        }

        /* 융합: GPS/WiFi/네트워크 결합 (GPS > WiFi > GeoIP 우선순위) */
        if (got_fix) {
            pthread_mutex_lock(&svc->lock);
            svc->last_known = loc;
            svc->has_fix = true;

            /* fused provider: GPS 데이터면 fused, 아니면 그대로 */
            if (strcmp(loc.provider, "gps") == 0) {
                snprintf(svc->last_known.provider,
                         sizeof(svc->last_known.provider), "fused");
            }
            pthread_mutex_unlock(&svc->lock);

            /* 지오펜스 체크 */
            check_geofences(svc, &svc->last_known);

            /* 콜백 호출 */
            if (svc->cb) {
                svc->cb(&svc->last_known, svc->cb_data);
            }

            /* D-Bus 시그널 */
            emit_location_signal(svc, &svc->last_known);
        }

        /* 대기 */
        long sleep_us = (long)svc->interval_ms * 1000L;
        if (sleep_us < 100000) sleep_us = 100000;  /* 최소 100ms */
        usleep((useconds_t)sleep_us);
    }

    g_message("[Location] Poll thread stopped");
    return NULL;
}

/* ─── D-Bus 인트로스펙션 ─── */
static const char *location_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_LOCATION_DBUS_NAME "'>"
    "    <method name='GetLastKnown'>"
    "      <arg type='d' name='latitude' direction='out'/>"
    "      <arg type='d' name='longitude' direction='out'/>"
    "      <arg type='d' name='altitude_m' direction='out'/>"
    "      <arg type='d' name='accuracy_m' direction='out'/>"
    "      <arg type='d' name='speed_mps' direction='out'/>"
    "      <arg type='d' name='bearing_deg' direction='out'/>"
    "      <arg type='t' name='timestamp_ms' direction='out'/>"
    "      <arg type='s' name='provider' direction='out'/>"
    "    </method>"
    "    <method name='RequestUpdates'>"
    "      <arg type='i' name='interval_ms' direction='in'/>"
    "    </method>"
    "    <method name='StopUpdates'/>"
    "    <signal name='LocationUpdated'>"
    "      <arg type='d' name='latitude'/>"
    "      <arg type='d' name='longitude'/>"
    "      <arg type='d' name='altitude_m'/>"
    "      <arg type='d' name='accuracy_m'/>"
    "      <arg type='d' name='speed_mps'/>"
    "      <arg type='d' name='bearing_deg'/>"
    "      <arg type='t' name='timestamp_ms'/>"
    "      <arg type='s' name='provider'/>"
    "    </signal>"
    "    <method name='AddGeofence'>"
    "      <arg type='d' name='lat'      direction='in'/>"
    "      <arg type='d' name='lon'      direction='in'/>"
    "      <arg type='d' name='radius_m' direction='in'/>"
    "      <arg type='s' name='tag'      direction='in'/>"
    "      <arg type='i' name='result'   direction='out'/>"
    "    </method>"
    "    <method name='RemoveGeofence'>"
    "      <arg type='s' name='tag'    direction='in'/>"
    "      <arg type='i' name='result' direction='out'/>"
    "    </method>"
    "    <signal name='GeofenceEnter'>"
    "      <arg type='s' name='tag'/>"
    "    </signal>"
    "    <signal name='GeofenceExit'>"
    "      <arg type='s' name='tag'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── D-Bus 메서드 핸들러 ─── */
static void handle_location_method(GDBusConnection *conn, const gchar *sender,
                                    const gchar *path, const gchar *iface,
                                    const gchar *method, GVariant *params,
                                    GDBusMethodInvocation *inv, gpointer data) {
    ZylLocationService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "GetLastKnown") == 0) {
        pthread_mutex_lock(&svc->lock);
        ZylLocation loc = svc->last_known;
        pthread_mutex_unlock(&svc->lock);

        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(ddddddt&s)",
                           loc.latitude, loc.longitude, loc.altitude_m,
                           (double)loc.accuracy_m, (double)loc.speed_mps,
                           (double)loc.bearing_deg,
                           (guint64)loc.timestamp_ms,
                           loc.provider[0] ? loc.provider : "none"));
    } else if (g_strcmp0(method, "RequestUpdates") == 0) {
        gint32 interval;
        g_variant_get(params, "(i)", &interval);
        zyl_location_request_updates(svc, interval, NULL, NULL);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "StopUpdates") == 0) {
        zyl_location_stop_updates(svc);
        g_dbus_method_invocation_return_value(inv, NULL);

    } else if (g_strcmp0(method, "AddGeofence") == 0) {
        gdouble lat, lon, radius;
        const gchar *tag = NULL;
        g_variant_get(params, "(ddds)", &lat, &lon, &radius, &tag);
        ZylGeofence fence = { lat, lon, radius, (char *)tag };
        gint32 r = (gint32)zyl_location_add_geofence(svc, &fence);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(i)", r));

    } else if (g_strcmp0(method, "RemoveGeofence") == 0) {
        const gchar *tag = NULL;
        g_variant_get(params, "(&s)", &tag);
        gint32 r = (gint32)zyl_location_remove_geofence(svc, tag);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(i)", r));
    }
}

static const GDBusInterfaceVTable location_vtable = {
    .method_call = handle_location_method,
};

static void on_location_bus_acquired(GDBusConnection *conn, const gchar *name,
                                      gpointer data) {
    ZylLocationService *svc = data;
    svc->dbus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(location_introspection_xml, NULL);
    g_dbus_connection_register_object(conn, ZYL_LOCATION_DBUS_PATH,
        info->interfaces[0], &location_vtable, svc, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_message("[Location] D-Bus registered: %s", ZYL_LOCATION_DBUS_NAME);
}

/* ─── 공개 API ─── */

ZylLocationService *zyl_location_create(void) {
    ZylLocationService *svc = g_new0(ZylLocationService, 1);
    pthread_mutex_init(&svc->lock, NULL);
    memset(svc->geofences, 0, sizeof(svc->geofences));
    svc->geofence_count = 0;

#ifdef HAVE_GPSD
    gpsd_connect(svc);
#else
    g_message("[Location] Built without GPSD support — network-only mode");
#endif

    /* D-Bus 등록 */
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_LOCATION_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_location_bus_acquired, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_location_destroy(ZylLocationService *svc) {
    if (!svc) return;

    /* 스레드 중지 */
    if (svc->thread_running) {
        svc->thread_running = false;
        pthread_join(svc->poll_thread, NULL);
    }

#ifdef HAVE_GPSD
    gpsd_disconnect(svc);
#endif

    /* 지오펜스 메모리 해제 */
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        if (svc->geofences[i].active && svc->geofences[i].fence.tag) {
            free(svc->geofences[i].fence.tag);
            svc->geofences[i].fence.tag = NULL;
        }
    }

    g_bus_unown_name(svc->dbus_owner_id);
    pthread_mutex_destroy(&svc->lock);
    g_free(svc);
}

int zyl_location_request_updates(ZylLocationService *svc,
                                  int interval_ms,
                                  zyl_location_callback_fn cb,
                                  void *user_data) {
    if (!svc) return -1;
    if (interval_ms < 100) interval_ms = 100;

    /* 기존 스레드 중지 */
    if (svc->thread_running) {
        svc->thread_running = false;
        pthread_join(svc->poll_thread, NULL);
    }

    svc->interval_ms = interval_ms;
    if (cb) {
        svc->cb = cb;
        svc->cb_data = user_data;
    }
    svc->updates_active = true;
    svc->thread_running = true;

    pthread_create(&svc->poll_thread, NULL, location_poll_thread, svc);

    g_message("[Location] Updates requested: interval=%d ms", interval_ms);
    return 0;
}

void zyl_location_stop_updates(ZylLocationService *svc) {
    if (!svc || !svc->updates_active) return;

    svc->thread_running = false;
    pthread_join(svc->poll_thread, NULL);
    svc->updates_active = false;

    g_message("[Location] Updates stopped");
}

int zyl_location_get_last_known(const ZylLocationService *svc,
                                 ZylLocation *out) {
    if (!svc || !out) return -1;

    pthread_mutex_lock((pthread_mutex_t *)&svc->lock);
    *out = svc->last_known;
    bool valid = svc->has_fix;
    pthread_mutex_unlock((pthread_mutex_t *)&svc->lock);

    return valid ? 0 : -1;
}

/* ─── 지오펜스 공개 API ─── */

int zyl_location_add_geofence(ZylLocationService *svc,
                               const ZylGeofence *fence) {
    if (!svc || !fence || !fence->tag) return -1;

    pthread_mutex_lock(&svc->lock);

    /* 중복 tag 확인 */
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        if (svc->geofences[i].active &&
            strcmp(svc->geofences[i].fence.tag, fence->tag) == 0) {
            pthread_mutex_unlock(&svc->lock);
            g_warning("[Location] Geofence tag '%s' already exists", fence->tag);
            return -1;
        }
    }

    /* 빈 슬롯 탐색 */
    int slot = -1;
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        if (!svc->geofences[i].active) { slot = i; break; }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&svc->lock);
        g_warning("[Location] Geofence capacity (%d) reached", ZYL_GEOFENCE_MAX);
        return -1;
    }

    GeofenceEntry *e = &svc->geofences[slot];
    e->fence.lat      = fence->lat;
    e->fence.lon      = fence->lon;
    e->fence.radius_m = fence->radius_m;
    e->fence.tag      = strdup(fence->tag);
    e->inside         = false;
    e->active         = true;
    svc->geofence_count++;

    pthread_mutex_unlock(&svc->lock);
    g_message("[Location] Geofence added: tag='%s' lat=%.4f lon=%.4f r=%.0fm",
              fence->tag, fence->lat, fence->lon, fence->radius_m);
    return 0;
}

int zyl_location_remove_geofence(ZylLocationService *svc,
                                  const char *tag) {
    if (!svc || !tag) return -1;

    pthread_mutex_lock(&svc->lock);
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        GeofenceEntry *e = &svc->geofences[i];
        if (e->active && strcmp(e->fence.tag, tag) == 0) {
            free(e->fence.tag);
            e->fence.tag = NULL;
            e->active = false;
            svc->geofence_count--;
            pthread_mutex_unlock(&svc->lock);
            g_message("[Location] Geofence removed: tag='%s'", tag);
            return 0;
        }
    }
    pthread_mutex_unlock(&svc->lock);
    g_warning("[Location] Geofence tag '%s' not found", tag);
    return -1;
}

/* ─── main(): 독립 데몬 실행 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    g_message("[Location] Zyl Location Service starting...");

    ZylLocationService *svc = zyl_location_create();
    if (!svc) {
        g_critical("[Location] Failed to create location service");
        return 1;
    }

    /* 기본 1초 간격으로 업데이트 시작 */
    zyl_location_request_updates(svc, 1000, NULL, NULL);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_message("[Location] Entering main loop");
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_location_destroy(svc);
    g_message("[Location] Service stopped");
    return 0;
}
