/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 위치 서비스 — GPSD 기반 GPS, GeoIP 폴백, 융합 위치 제공
 * 수행범위: libgps 연동, HTTP GeoIP 조회, 주기적 업데이트, D-Bus 시그널
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

        /* 2순위: GeoIP 네트워크 폴백 */
        if (!got_fix) {
            got_fix = geoip_query(&loc);
        }

        /* 융합: GPS + 네트워크 결합 (GPS 우선, 네트워크 보조) */
        if (got_fix) {
            pthread_mutex_lock(&svc->lock);
            svc->last_known = loc;
            svc->has_fix = true;

            /* fused provider: GPS 데이터가 있으면 그대로, 아니면 network */
            if (strcmp(loc.provider, "gps") == 0) {
                snprintf(svc->last_known.provider,
                         sizeof(svc->last_known.provider), "fused");
            }
            pthread_mutex_unlock(&svc->lock);

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
