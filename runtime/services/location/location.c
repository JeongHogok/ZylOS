#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 위치 서비스 매니저 — 프로바이더 융합, 지오펜스, D-Bus, 공개 API
 * 수행범위: GPS/GeoIP/WiFi 프로바이더 조율, 지오펜스 진입/이탈 감지,
 *          D-Bus 시그널/메서드, 주기적 폴링 스레드, 서비스 생명주기
 * 의존방향: location_internal.h, gio/gio.h
 * SOLID: SRP — 프로바이더 조율 및 서비스 관리만 담당
 *         (개별 프로바이더는 location_gps.c, location_geoip.c, location_wifi.c)
 * ────────────────────────────────────────────────────────── */

#include "location_internal.h"

/* ─── 현재 시각 밀리초 ─── */
uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
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
