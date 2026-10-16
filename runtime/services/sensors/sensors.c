#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 센서 서비스 — 센서 폴링, 이벤트 전달, D-Bus 시그널 발신
 * 수행범위: Sensors HAL 로 있는 센서를 찾고 읽는 폴링 스레드, 근접 판정·방위각,
 *           최신값 캐시, GetLatest·SensorEvent
 * 의존방향: sensors.h, hal.h (Sensors HAL), gio/gio.h
 * SOLID: SRP — 센서 데이터 수집 및 전달만 담당
 * ────────────────────────────────────────────────────────── */

#include "sensors.h"
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <gio/gio.h>

/* 화면 회전·근접·조도에 충분하고 배터리에 가벼운 공통 주기 */
#define DEFAULT_RATE_HZ     5.0f
#define MAX_LISTENERS       16
#define NEAR_CM             5.0

/* The D-Bus sensor_type is ZylSensorType; the HAL names the same sensors its way */
static const ZylSensorKind HAL_KIND[] = {
    [ZYL_SENSOR_ACCELEROMETER] = ZYL_HAL_SENSOR_ACCEL,
    [ZYL_SENSOR_GYROSCOPE]     = ZYL_HAL_SENSOR_GYRO,
    [ZYL_SENSOR_PROXIMITY]     = ZYL_HAL_SENSOR_PROXIMITY,
    [ZYL_SENSOR_LIGHT]         = ZYL_HAL_SENSOR_LIGHT,
    [ZYL_SENSOR_MAGNETOMETER]  = ZYL_HAL_SENSOR_MAGNETIC,
};
static const char *TYPE_NAMES[] = {
    "Accelerometer", "Gyroscope", "Proximity", "Light", "Magnetometer"
};

/* ─── 리스너 슬롯 ─── */
typedef struct {
    bool active;
    ZylSensorType type;
    float rate_hz;
    zyl_sensor_callback_fn cb;
    void *user_data;
    pthread_t thread;
    bool thread_running;
} SensorListener;

/* ─── 내부 구조체 ─── */
struct ZylSensorService {
    /* 이 기기에 있는 센서 (ZYL_SENSOR_* 인덱스) */
    bool present[5];

    /* 리스너 */
    SensorListener listeners[MAX_LISTENERS];
    pthread_mutex_t lock;

    /* 최신 값 캐시 */
    ZylSensorEvent latest[5];    /* ZYL_SENSOR_* 인덱스 */

    /* D-Bus */
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* ─── 현재 시각 나노초 ─── */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ─── 센서 찾기: 종류마다 HAL 에 묻는다 ─── */
static void find_sensors(ZylSensorService *svc) {
    int found = 0;
    for (int t = 0; t < 5; t++) {
        svc->present[t] = zyl_hal_sensors()->present(HAL_KIND[t]);
        if (svc->present[t]) {
            g_message("[Sensors] Found %s", TYPE_NAMES[t]);
            found++;
        }
    }
    if (found == 0)
        g_message("[Sensors] No sensor hardware detected — service idle");
}

/* ─── 한 샘플: HAL 의 SI 값 → 이벤트 (근접 판정·방위각은 여기서) ─── */
static bool read_sensor(ZylSensorType type, ZylSensorEvent *evt) {
    ZylSensorSample s;
    if (!zyl_hal_sensors()->read(HAL_KIND[type], &s)) return false;

    evt->type = type;
    evt->timestamp_ns = now_ns();
    switch (type) {
    case ZYL_SENSOR_ACCELEROMETER:
        evt->data.accel.x = (float)s.v[0];
        evt->data.accel.y = (float)s.v[1];
        evt->data.accel.z = (float)s.v[2];
        break;
    case ZYL_SENSOR_GYROSCOPE:
        evt->data.gyro.x = (float)s.v[0];
        evt->data.gyro.y = (float)s.v[1];
        evt->data.gyro.z = (float)s.v[2];
        break;
    case ZYL_SENSOR_PROXIMITY:
        evt->data.proximity.distance_cm = (float)s.v[0];
        evt->data.proximity.near = s.v[0] < NEAR_CM;
        break;
    case ZYL_SENSOR_LIGHT:
        evt->data.light.lux = (float)s.v[0];
        break;
    case ZYL_SENSOR_MAGNETOMETER: {
        /* heading = atan2(y, x) → degrees */
        double heading = atan2(s.v[1], s.v[0]) * 180.0 / M_PI;
        if (heading < 0.0) heading += 360.0;
        evt->data.compass.heading_deg = (float)heading;
        break;
    }
    }
    return true;
}

/* ─── D-Bus 시그널 발신 ─── */
static void emit_sensor_signal(ZylSensorService *svc,
                                const ZylSensorEvent *evt) {
    if (!svc->dbus) return;

    /*
     * SensorEvent(i type, d v0, d v1, d v2, t timestamp_ns)
     *   v0/v1/v2 의미는 type에 따라 다름:
     *   ACCEL/GYRO/MAGN: x, y, z
     *   PROXIMITY:       near(0/1), distance_cm, 0
     *   LIGHT:           lux, 0, 0
     */
    double v0 = 0.0, v1 = 0.0, v2 = 0.0;
    switch (evt->type) {
    case ZYL_SENSOR_ACCELEROMETER:
        v0 = evt->data.accel.x; v1 = evt->data.accel.y; v2 = evt->data.accel.z;
        break;
    case ZYL_SENSOR_GYROSCOPE:
        v0 = evt->data.gyro.x; v1 = evt->data.gyro.y; v2 = evt->data.gyro.z;
        break;
    case ZYL_SENSOR_PROXIMITY:
        v0 = evt->data.proximity.near ? 1.0 : 0.0;
        v1 = evt->data.proximity.distance_cm;
        break;
    case ZYL_SENSOR_LIGHT:
        v0 = evt->data.light.lux;
        break;
    case ZYL_SENSOR_MAGNETOMETER:
        v0 = evt->data.compass.heading_deg;
        break;
    }

    g_dbus_connection_emit_signal(svc->dbus, NULL,
        ZYL_SENSOR_DBUS_PATH,
        ZYL_SENSOR_DBUS_NAME,
        "SensorEvent",
        g_variant_new("(idddt)", (gint32)evt->type,
                       v0, v1, v2, (guint64)evt->timestamp_ns),
        NULL);
}

/* ─── 폴링 스레드 컨텍스트 ─── */
typedef struct {
    ZylSensorService *svc;
    SensorListener *lis;
} PollContext;

static void *sensor_poll_thread_v2(void *arg) {
    PollContext *ctx = arg;
    ZylSensorService *svc = ctx->svc;
    SensorListener *lis = ctx->lis;
    free(ctx);

    long interval_us = (long)(1000000.0 / lis->rate_hz);
    if (interval_us < 1000) interval_us = 1000;
    if (interval_us > 10000000) interval_us = 10000000;

    g_message("[Sensors] Poll thread started: type=%d rate=%.1f Hz",
              lis->type, lis->rate_hz);

    while (lis->thread_running) {
        usleep((useconds_t)interval_us);
        if (!lis->active || !lis->thread_running) break;

        ZylSensorEvent evt = {0};
        if (!read_sensor(lis->type, &evt)) continue;

        /* 캐시 업데이트 */
        pthread_mutex_lock(&svc->lock);
        svc->latest[evt.type] = evt;
        pthread_mutex_unlock(&svc->lock);

        /* 콜백 호출 */
        if (lis->cb) {
            lis->cb(&evt, lis->user_data);
        }

        /* D-Bus 시그널 */
        emit_sensor_signal(svc, &evt);
    }

    g_message("[Sensors] Poll thread stopped: type=%d", lis->type);
    return NULL;
}

/* ─── D-Bus 인트로스펙션 ─── */
static const char *sensor_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_SENSOR_DBUS_NAME "'>"
    "    <method name='GetLatest'>"
    "      <arg type='i' name='sensor_type' direction='in'/>"
    "      <arg type='i' name='result_type' direction='out'/>"
    "      <arg type='d' name='v0' direction='out'/>"
    "      <arg type='d' name='v1' direction='out'/>"
    "      <arg type='d' name='v2' direction='out'/>"
    "      <arg type='t' name='timestamp_ns' direction='out'/>"
    "    </method>"
    "    <signal name='SensorEvent'>"
    "      <arg type='i' name='sensor_type'/>"
    "      <arg type='d' name='v0'/>"
    "      <arg type='d' name='v1'/>"
    "      <arg type='d' name='v2'/>"
    "      <arg type='t' name='timestamp_ns'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── D-Bus 메서드 핸들러 ─── */
static void handle_sensor_method(GDBusConnection *conn, const gchar *sender,
                                  const gchar *path, const gchar *iface,
                                  const gchar *method, GVariant *params,
                                  GDBusMethodInvocation *inv, gpointer data) {
    ZylSensorService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "GetLatest") == 0) {
        gint32 type;
        g_variant_get(params, "(i)", &type);

        if (type < 0 || type > 4) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_INVALID_ARGS, "Invalid sensor type: %d", type);
            return;
        }

        pthread_mutex_lock(&svc->lock);
        ZylSensorEvent evt = svc->latest[type];
        pthread_mutex_unlock(&svc->lock);

        double v0 = 0.0, v1 = 0.0, v2 = 0.0;
        switch (evt.type) {
        case ZYL_SENSOR_ACCELEROMETER:
            v0 = evt.data.accel.x; v1 = evt.data.accel.y; v2 = evt.data.accel.z;
            break;
        case ZYL_SENSOR_GYROSCOPE:
            v0 = evt.data.gyro.x; v1 = evt.data.gyro.y; v2 = evt.data.gyro.z;
            break;
        case ZYL_SENSOR_PROXIMITY:
            v0 = evt.data.proximity.near ? 1.0 : 0.0;
            v1 = evt.data.proximity.distance_cm;
            break;
        case ZYL_SENSOR_LIGHT:
            v0 = evt.data.light.lux;
            break;
        case ZYL_SENSOR_MAGNETOMETER:
            v0 = evt.data.compass.heading_deg;
            break;
        }

        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(idddt)", (gint32)evt.type,
                           v0, v1, v2, (guint64)evt.timestamp_ns));
    }
}

static const GDBusInterfaceVTable sensor_vtable = {
    .method_call = handle_sensor_method,
};

static void on_sensor_bus_acquired(GDBusConnection *conn, const gchar *name,
                                    gpointer data) {
    (void)name;
    ZylSensorService *svc = data;
    svc->dbus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(sensor_introspection_xml, NULL);
    if (!info || !info->interfaces || !info->interfaces[0]) {
        g_warning("[Sensors] Failed to parse introspection XML");
        return;
    }
    g_dbus_connection_register_object(conn, ZYL_SENSOR_DBUS_PATH,
        info->interfaces[0], &sensor_vtable, svc, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_message("[Sensors] D-Bus registered: %s", ZYL_SENSOR_DBUS_NAME);
}

/* ─── 공개 API ─── */

ZylSensorService *zyl_sensor_create(void) {
    ZylSensorService *svc = g_new0(ZylSensorService, 1);
    pthread_mutex_init(&svc->lock, NULL);

    /* 센서 찾기 */
    find_sensors(svc);

    /* 찾은 장치는 서비스가 바로 읽기 시작한다. D-Bus 에 구독 메서드가 없어
       폴링을 시작시키는 쪽이 아무도 없었고, 그래서 GetLatest 는 늘 0 을,
       SensorEvent 는 아무것도 내보내지 않았다.
       shortcut: 화면이 꺼져도 계속 읽는다 — 배터리가 문제가 되면 앱별 구독
       (Subscribe/Unsubscribe)을 D-Bus 에 넣고 그때만 켤 것 */
    for (int t = 0; t < 5; t++) {
        if (svc->present[t])
            zyl_sensor_register_listener(svc, (ZylSensorType)t, DEFAULT_RATE_HZ, NULL, NULL);
    }

    /* D-Bus 등록 */
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
        ZYL_SENSOR_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_sensor_bus_acquired, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_sensor_destroy(ZylSensorService *svc) {
    if (!svc) return;

    /* 모든 폴링 스레드 중지 */
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (svc->listeners[i].active) {
            svc->listeners[i].thread_running = false;
            if (svc->listeners[i].thread) {
                pthread_join(svc->listeners[i].thread, NULL);
            }
        }
    }

    g_bus_unown_name(svc->dbus_owner_id);
    pthread_mutex_destroy(&svc->lock);
    g_free(svc);
}

int zyl_sensor_register_listener(ZylSensorService *svc,
                                  ZylSensorType type,
                                  float rate_hz,
                                  zyl_sensor_callback_fn cb,
                                  void *user_data) {
    if (!svc) return -1;        /* cb 없음 = 캐시와 시그널만 (서비스 자체 폴링) */
    if (rate_hz <= 0.0f) rate_hz = 1.0f;
    if (rate_hz > 1000.0f) rate_hz = 1000.0f;

    /* 이 기기에 있는 센서만 */
    if ((unsigned)type > ZYL_SENSOR_MAGNETOMETER || !svc->present[type]) {
        g_warning("[Sensors] No sensor of type %d", type);
        return -1;
    }

    /* 빈 슬롯 찾기 */
    pthread_mutex_lock(&svc->lock);
    int slot = -1;
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (!svc->listeners[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&svc->lock);
        g_warning("[Sensors] Max listeners reached");
        return -1;
    }

    SensorListener *lis = &svc->listeners[slot];
    lis->active = true;
    lis->type = type;
    lis->rate_hz = rate_hz;
    lis->cb = cb;
    lis->user_data = user_data;
    lis->thread_running = true;

    /* 폴링 스레드 시작 */
    PollContext *ctx = malloc(sizeof(PollContext));
    if (!ctx) {
        lis->active = false;
        lis->thread_running = false;
        pthread_mutex_unlock(&svc->lock);
        return -1;
    }
    ctx->svc = svc;
    ctx->lis = lis;
    pthread_mutex_unlock(&svc->lock);
    pthread_create(&lis->thread, NULL, sensor_poll_thread_v2, ctx);

    g_message("[Sensors] Listener registered: type=%d rate=%.1f Hz", type, rate_hz);
    return 0;
}

void zyl_sensor_unregister_listener(ZylSensorService *svc,
                                     ZylSensorType type,
                                     zyl_sensor_callback_fn cb) {
    if (!svc) return;

    pthread_mutex_lock(&svc->lock);
    for (int i = 0; i < MAX_LISTENERS; i++) {
        SensorListener *lis = &svc->listeners[i];
        if (lis->active && lis->type == type && lis->cb == cb) {
            lis->thread_running = false;
            lis->active = false;
            pthread_mutex_unlock(&svc->lock);

            pthread_join(lis->thread, NULL);
            lis->thread = 0;

            g_message("[Sensors] Listener unregistered: type=%d", type);
            return;
        }
    }
    pthread_mutex_unlock(&svc->lock);
}

int zyl_sensor_get_latest(ZylSensorService *svc,
                           ZylSensorType type,
                           ZylSensorEvent *out) {
    if (!svc || !out) return -1;
    if (type < 0 || type > 4) return -1;

    pthread_mutex_lock(&svc->lock);
    *out = svc->latest[type];
    pthread_mutex_unlock(&svc->lock);

    return (out->timestamp_ns > 0) ? 0 : -1;
}

#ifndef ZYL_TEST
/* ─── main(): 독립 데몬 실행 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    g_message("[Sensors] Zyl Sensor Service starting...");

    ZylSensorService *svc = zyl_sensor_create();
    if (!svc) {
        g_critical("[Sensors] Failed to create sensor service");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_message("[Sensors] Entering main loop");
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_sensor_destroy(svc);
    g_message("[Sensors] Service stopped");
    return 0;
}
#endif /* ZYL_TEST */
