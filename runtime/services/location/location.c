#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 위치 서비스 매니저 — 프로바이더 융합, 지오펜스, D-Bus, 공개 API
 * 수행범위: GPS/GeoIP/WiFi 프로바이더 조율, 앱별 지오펜스 진입/이탈 감지
 *          (앱 삭제 때 정리), D-Bus 시그널/메서드, 주기적 폴링 스레드
 *          (멈춤 요청에 즉시 깨어남), 서비스 생명주기, 사용자의 위치 스위치
 *          (설정 location.enabled — 끄면 프로바이더에 묻지 않고 앱에 위치를 주지 않음).
 *          로그에 좌표를 남기지 않는다
 * 의존방향: location_internal.h, gio/gio.h
 * SOLID: SRP — 프로바이더 조율 및 서비스 관리만 담당
 *         (개별 프로바이더는 location_gps.c, location_geoip.c, location_wifi.c)
 * ────────────────────────────────────────────────────────── */

#include "location_internal.h"

/* now_ms() — moved to location_internal.h as static inline */

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
/* The owner is the second argument (as NotificationPosted's): an app's fence
   names a place it cares about, not something another app gets to hear. */
static void emit_geofence_signal(ZylLocationService *svc,
                                  const GeofenceEntry *e, bool entered) {
    if (!svc->dbus) return;
    g_dbus_connection_emit_signal(svc->dbus, NULL,
        ZYL_LOCATION_DBUS_PATH,
        ZYL_LOCATION_DBUS_NAME,
        entered ? "GeofenceEnter" : "GeofenceExit",
        g_variant_new("(ss)", e->fence.tag, e->app),
        NULL);
    g_message("[Location] Geofence %s: app='%s'", entered ? "ENTER" : "EXIT", e->app);
}

/* ─── 지오펜스 주기적 체크 (폴링 스레드에서 호출) ─── */
/* Emitted under the lock: the D-Bus thread may remove the fence (and free its
   tag) the moment the lock is let go. Emitting only queues a message. */
static void check_geofences(ZylLocationService *svc,
                             const ZylLocation *loc) {
    pthread_mutex_lock(&svc->lock);
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        GeofenceEntry *e = &svc->geofences[i];
        if (!e->active) continue;

        double dist = haversine_m(loc->latitude, loc->longitude,
                                   e->fence.lat, e->fence.lon);
        bool inside = (dist <= e->fence.radius_m);

        if (inside != e->inside) {
            e->inside = inside;
            emit_geofence_signal(svc, e, inside);
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
        g_variant_new("(ddddddts)",
                       loc->latitude, loc->longitude, loc->altitude_m,
                       (double)loc->accuracy_m, (double)loc->speed_mps,
                       (double)loc->bearing_deg,
                       (guint64)loc->timestamp_ms,
                       loc->provider),
        NULL);
}

/* ─── 프로바이더 결과 검증 ─── */
/* A fix goes out only if it is a place on Earth (NaN/inf/out of range from a
   provider or a forged response is dropped). Unknown extras — gpsd leaves them
   NaN — become 0: JSON has no NaN, the bridge would hand apps "nan.0". */
static bool fix_is_usable(ZylLocation *loc) {
    if (!(fabs(loc->latitude) <= 90.0 && fabs(loc->longitude) <= 180.0))
        return false;
    if (!isfinite(loc->altitude_m))  loc->altitude_m  = 0.0;
    if (!isfinite(loc->accuracy_m))  loc->accuracy_m  = 0.0f;
    if (!isfinite(loc->speed_mps))   loc->speed_mps   = 0.0f;
    if (!isfinite(loc->bearing_deg)) loc->bearing_deg = 0.0f;
    return true;
}

/* GPSD (하드웨어 GPS) > WiFi BSS 삼각측량 > GeoIP — 쓸 수 있는 첫 위치 */
static bool read_fix(ZylLocationService *svc, ZylLocation *loc) {
#ifdef HAVE_GPSD
    if ((svc->gpsd_connected || gpsd_connect(svc)) &&
        gpsd_read(svc, loc) && fix_is_usable(loc))
        return true;
#endif
    if (wifi_triangulate(svc, loc) && fix_is_usable(loc)) return true;
    return geoip_query(loc) && fix_is_usable(loc);
}

/* ─── 폴링 스레드 ─── */
/* One interval, or less: a stop wakes it at once (StopUpdates joins this
   thread on the D-Bus main loop). False once asked to stop. */
static bool wait_interval(ZylLocationService *svc) {
    struct timespec until;
    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_sec  += svc->interval_ms / 1000;
    until.tv_nsec += (long)(svc->interval_ms % 1000) * 1000000L;
    if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&svc->lock);
    int rc = 0;
    while (svc->thread_running && rc == 0)
        rc = pthread_cond_timedwait(&svc->wake, &svc->lock, &until);
    bool running = svc->thread_running;
    pthread_mutex_unlock(&svc->lock);
    return running;
}

static void *location_poll_thread(void *arg) {
    ZylLocationService *svc = arg;
    g_message("[Location] Poll thread started (interval=%d ms)", svc->interval_ms);

    do {
        ZylLocation loc = {0};
        pthread_mutex_lock(&svc->lock);
        bool off = svc->off;
        pthread_mutex_unlock(&svc->lock);
        bool got_fix = !off && read_fix(svc, &loc);

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
            /* Take a local copy for use outside the lock */
            ZylLocation loc_copy = svc->last_known;
            pthread_mutex_unlock(&svc->lock);

            /* 지오펜스 체크 — use local copy to avoid data race */
            check_geofences(svc, &loc_copy);

            /* 콜백 호출 */
            if (svc->cb) {
                svc->cb(&loc_copy, svc->cb_data);
            }

            /* D-Bus 시그널 */
            emit_location_signal(svc, &loc_copy);
        }
    } while (wait_interval(svc));

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
    "      <arg type='s' name='caller'   direction='in'/>"
    "      <arg type='d' name='lat'      direction='in'/>"
    "      <arg type='d' name='lon'      direction='in'/>"
    "      <arg type='d' name='radius_m' direction='in'/>"
    "      <arg type='s' name='tag'      direction='in'/>"
    "      <arg type='i' name='result'   direction='out'/>"
    "    </method>"
    "    <method name='RemoveGeofence'>"
    "      <arg type='s' name='caller' direction='in'/>"
    "      <arg type='s' name='tag'    direction='in'/>"
    "      <arg type='i' name='result' direction='out'/>"
    "    </method>"
    "    <signal name='GeofenceEnter'>"
    "      <arg type='s' name='tag'/>"
    "      <arg type='s' name='app_id'/>"
    "    </signal>"
    "    <signal name='GeofenceExit'>"
    "      <arg type='s' name='tag'/>"
    "      <arg type='s' name='app_id'/>"
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
        ZylLocation loc = svc->last_known;     /* cleared while switched off */
        bool off = svc->off;
        pthread_mutex_unlock(&svc->lock);
        if (off) snprintf(loc.provider, sizeof(loc.provider), "off");

        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(ddddddts)",
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
        /* caller: the calling app, filled by the bridge — never the request's */
        const gchar *caller = NULL;
        gdouble lat, lon, radius;
        gchar *tag = NULL;
        g_variant_get(params, "(&sddds)", &caller, &lat, &lon, &radius, &tag);
        ZylGeofence fence = { lat, lon, radius, (char *)tag };
        gint32 r = (gint32)zyl_location_add_geofence(svc, caller, &fence);
        g_free(tag);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(i)", r));

    } else if (g_strcmp0(method, "RemoveGeofence") == 0) {
        const gchar *caller = NULL, *tag = NULL;
        g_variant_get(params, "(&s&s)", &caller, &tag);
        gint32 r = (gint32)zyl_location_remove_geofence(svc, caller, tag);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(i)", r));
    }
}

static const GDBusInterfaceVTable location_vtable = {
    .method_call = handle_location_method,
};

static void on_app_removed(GDBusConnection *conn, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)signal;
    const gchar *app_id = NULL;
    g_variant_get(params, "(&s)", &app_id);
    zyl_location_forget_app(data, app_id);
}

/* The user's switch: off forgets the last place too — no app reads it afterwards */
static void on_location_setting(GVariant *values, gpointer data) {
    ZylLocationService *svc = data;
    bool off = !zyl_settings_bool(values, "enabled", true);
    pthread_mutex_lock(&svc->lock);
    bool changed = off != svc->off;
    svc->off = off;
    if (off) {
        memset(&svc->last_known, 0, sizeof(svc->last_known));
        svc->has_fix = false;
    }
    pthread_mutex_unlock(&svc->lock);
    if (changed) g_message("[Location] Switched %s by the user", off ? "off" : "on");
}

static void on_location_bus_acquired(GDBusConnection *conn, const gchar *name,
                                      gpointer data) {
    (void)name;
    ZylLocationService *svc = data;
    svc->dbus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(location_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_LOCATION_DBUS_PATH,
            info->interfaces[0], &location_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_dbus_connection_signal_subscribe(conn, "org.zylos.AppStore", "org.zylos.AppStore",
        "AppRemoved", "/org/zylos/AppStore", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_app_removed, svc, NULL);
    svc->follow = zyl_settings_follow(conn, "location", on_location_setting, svc);
    g_message("[Location] D-Bus registered: %s", ZYL_LOCATION_DBUS_NAME);
}

/* ─── 공개 API ─── */

static void init_sync(ZylLocationService *svc) {
    pthread_mutex_init(&svc->lock, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);   /* a clock change doesn't stretch a wait */
    pthread_cond_init(&svc->wake, &attr);
    pthread_condattr_destroy(&attr);
}

/* Stops the poll thread, waking it from its wait; returns once it is gone */
static void stop_poll_thread(ZylLocationService *svc) {
    pthread_mutex_lock(&svc->lock);
    bool running = svc->thread_running;
    svc->thread_running = false;
    pthread_cond_signal(&svc->wake);
    pthread_mutex_unlock(&svc->lock);
    if (running) pthread_join(svc->poll_thread, NULL);
}

static void geofence_clear(ZylLocationService *svc, GeofenceEntry *e) {
    free(e->fence.tag);
    free(e->app);
    memset(e, 0, sizeof(*e));
    svc->geofence_count--;
}

ZylLocationService *zyl_location_create(void) {
    ZylLocationService *svc = g_new0(ZylLocationService, 1);
    init_sync(svc);
    memset(svc->geofences, 0, sizeof(svc->geofences));
    svc->geofence_count = 0;

#ifdef HAVE_GPSD
    gpsd_connect(svc);
#else
    g_message("[Location] Built without GPSD support — network-only mode");
#endif

    /* D-Bus 등록 */
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
        ZYL_LOCATION_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_location_bus_acquired, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_location_destroy(ZylLocationService *svc) {
    if (!svc) return;

    stop_poll_thread(svc);
    zyl_settings_unfollow(svc->follow);

#ifdef HAVE_GPSD
    gpsd_disconnect(svc);
#endif

    /* 지오펜스 메모리 해제 */
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++)
        if (svc->geofences[i].active) geofence_clear(svc, &svc->geofences[i]);

    g_bus_unown_name(svc->dbus_owner_id);
    pthread_cond_destroy(&svc->wake);
    pthread_mutex_destroy(&svc->lock);
    g_free(svc);
}

int zyl_location_request_updates(ZylLocationService *svc,
                                  int interval_ms,
                                  zyl_location_callback_fn cb,
                                  void *user_data) {
    if (!svc) return -1;
    /* D-Bus input: any app's number. Too short spins the providers; past the
       maximum there is nothing an app would wait that long for. */
    if (interval_ms < ZYL_LOCATION_INTERVAL_MIN_MS) interval_ms = ZYL_LOCATION_INTERVAL_MIN_MS;
    if (interval_ms > ZYL_LOCATION_INTERVAL_MAX_MS) interval_ms = ZYL_LOCATION_INTERVAL_MAX_MS;

    /* 기존 스레드 중지 */
    stop_poll_thread(svc);

    svc->interval_ms = interval_ms;
    if (cb) {
        svc->cb = cb;
        svc->cb_data = user_data;
    }
    svc->updates_active = true;
    svc->thread_running = true;   /* no thread yet: nothing else reads it */

    int thr_rc = pthread_create(&svc->poll_thread, NULL, location_poll_thread, svc);
    if (thr_rc != 0) {
        svc->thread_running = false;
        svc->updates_active = false;
        g_warning("[Location] pthread_create failed: %s", strerror(thr_rc));
        return -1;
    }

    g_message("[Location] Updates requested: interval=%d ms", interval_ms);
    return 0;
}

void zyl_location_stop_updates(ZylLocationService *svc) {
    if (!svc || !svc->updates_active) return;

    stop_poll_thread(svc);
    svc->updates_active = false;

    g_message("[Location] Updates stopped");
}

/* ─── 지오펜스 공개 API ─── */

int zyl_location_add_geofence(ZylLocationService *svc, const char *app,
                               const ZylGeofence *fence) {
    if (!svc || !app || !app[0] || !fence || !fence->tag) return -1;
    /* D-Bus input: a fence off the globe or with a NaN/inf/negative radius
       can never fire correctly and would only hold one of the shared slots */
    if (!(fabs(fence->lat) <= 90.0 && fabs(fence->lon) <= 180.0 &&
          fence->radius_m > 0.0 && isfinite(fence->radius_m)))
        return -1;

    pthread_mutex_lock(&svc->lock);

    /* 빈 슬롯, 그리고 이 앱의 지오펜스 — 같은 tag 가 있으면 거부 */
    int slot = -1, owned = 0;
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        GeofenceEntry *e = &svc->geofences[i];
        if (!e->active) {
            if (slot < 0) slot = i;
            continue;
        }
        if (strcmp(e->app, app) != 0) continue;
        if (strcmp(e->fence.tag, fence->tag) == 0) {
            pthread_mutex_unlock(&svc->lock);
            g_warning("[Location] Geofence '%s' of %s already exists", fence->tag, app);
            return -1;
        }
        owned++;
    }

    /* one app may not take the device's slots from every other */
    if (slot < 0 || owned >= ZYL_GEOFENCE_PER_APP_MAX) {
        pthread_mutex_unlock(&svc->lock);
        g_warning("[Location] Geofence capacity reached (%s: %d of %d, device %d)",
                  app, owned, ZYL_GEOFENCE_PER_APP_MAX, ZYL_GEOFENCE_MAX);
        return -1;
    }

    GeofenceEntry *e = &svc->geofences[slot];
    e->fence.tag = strdup(fence->tag);
    e->app       = strdup(app);
    if (!e->fence.tag || !e->app) {
        free(e->fence.tag);
        free(e->app);
        memset(e, 0, sizeof(*e));
        pthread_mutex_unlock(&svc->lock);
        return -1; /* allocation failure */
    }
    e->fence.lat      = fence->lat;
    e->fence.lon      = fence->lon;
    e->fence.radius_m = fence->radius_m;
    e->inside         = false;
    e->active         = true;
    svc->geofence_count++;

    pthread_mutex_unlock(&svc->lock);
    g_message("[Location] Geofence added: app='%s'", app);   /* not where: a place the user goes */
    return 0;
}

int zyl_location_remove_geofence(ZylLocationService *svc, const char *app,
                                  const char *tag) {
    if (!svc || !app || !tag) return -1;

    pthread_mutex_lock(&svc->lock);
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        GeofenceEntry *e = &svc->geofences[i];
        if (e->active && strcmp(e->app, app) == 0 && strcmp(e->fence.tag, tag) == 0) {
            geofence_clear(svc, e);
            pthread_mutex_unlock(&svc->lock);
            g_message("[Location] Geofence removed: app='%s' tag='%s'", app, tag);
            return 0;
        }
    }
    pthread_mutex_unlock(&svc->lock);
    g_warning("[Location] Geofence '%s' of %s not found", tag, app);
    return -1;
}

int zyl_location_forget_app(ZylLocationService *svc, const char *app) {
    if (!svc || !app) return 0;
    int removed = 0;
    pthread_mutex_lock(&svc->lock);
    for (int i = 0; i < ZYL_GEOFENCE_MAX; i++) {
        GeofenceEntry *e = &svc->geofences[i];
        if (e->active && strcmp(e->app, app) == 0) {
            geofence_clear(svc, e);
            removed++;
        }
    }
    pthread_mutex_unlock(&svc->lock);
    if (removed) g_message("[Location] %d geofence(s) of removed app %s dropped", removed, app);
    return removed;
}

#ifndef ZYL_TEST
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
#endif /* ZYL_TEST */
