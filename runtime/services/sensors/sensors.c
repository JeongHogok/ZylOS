#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 센서 서비스 — IIO 센서 폴링, 이벤트 전달, D-Bus 시그널 발신
 * 수행범위: /sys/bus/iio/devices/ 스캔, raw→실수 변환, 폴링 스레드
 * 의존방향: sensors.h, gio/gio.h, sysfs (IIO)
 * SOLID: SRP — 센서 데이터 수집 및 전달만 담당
 * ────────────────────────────────────────────────────────── */

#include "sensors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <gio/gio.h>

/* ─── 내부 상수 ─── */
#define IIO_BASE_PATH       "/sys/bus/iio/devices"
#define MAX_LISTENERS       16
#define MAX_IIO_DEVICES     8
#define SYSFS_BUF           256

/* ─── IIO 장치 정보 ─── */
typedef struct {
    char path[256];              /* /sys/bus/iio/devices/iio:deviceN */
    ZylSensorType type;
    bool available;
} IioDevice;

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
    /* IIO 장치 맵 */
    IioDevice devices[MAX_IIO_DEVICES];
    int device_count;

    /* 리스너 */
    SensorListener listeners[MAX_LISTENERS];
    pthread_mutex_t lock;

    /* 최신 값 캐시 */
    ZylSensorEvent latest[5];    /* ZYL_SENSOR_* 인덱스 */

    /* D-Bus */
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* ─── 유틸리티: sysfs 읽기 ─── */
static int sysfs_read_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = 0;
    fclose(f);
    return val;
}

static double sysfs_read_double(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0.0;
    double val = 0.0;
    if (fscanf(f, "%lf", &val) != 1) val = 0.0;
    fclose(f);
    return val;
}

/* ─── 현재 시각 나노초 ─── */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ─── 파일 존재 확인 ─── */
static bool file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

/* ─── IIO 장치 타입 감지 ─── */
static bool detect_iio_type(const char *dev_path, ZylSensorType *out_type) {
    char path[SYSFS_BUF];

    /* 가속도계: in_accel_x_raw 존재 */
    snprintf(path, sizeof(path), "%s/in_accel_x_raw", dev_path);
    if (file_exists(path)) { *out_type = ZYL_SENSOR_ACCELEROMETER; return true; }

    /* 자이로: in_anglvel_x_raw 존재 */
    snprintf(path, sizeof(path), "%s/in_anglvel_x_raw", dev_path);
    if (file_exists(path)) { *out_type = ZYL_SENSOR_GYROSCOPE; return true; }

    /* 근접: in_proximity_raw 존재 */
    snprintf(path, sizeof(path), "%s/in_proximity_raw", dev_path);
    if (file_exists(path)) { *out_type = ZYL_SENSOR_PROXIMITY; return true; }

    /* 조도: in_illuminance_raw 존재 */
    snprintf(path, sizeof(path), "%s/in_illuminance_raw", dev_path);
    if (file_exists(path)) { *out_type = ZYL_SENSOR_LIGHT; return true; }

    /* 자기장: in_magn_x_raw 존재 */
    snprintf(path, sizeof(path), "%s/in_magn_x_raw", dev_path);
    if (file_exists(path)) { *out_type = ZYL_SENSOR_MAGNETOMETER; return true; }

    return false;
}

/* ─── IIO 장치 스캔 ─── */
static void scan_iio_devices(ZylSensorService *svc) {
    DIR *dir = opendir(IIO_BASE_PATH);
    if (!dir) {
        g_message("[Sensors] No IIO subsystem found at %s", IIO_BASE_PATH);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && svc->device_count < MAX_IIO_DEVICES) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0)
            continue;

        IioDevice *dev = &svc->devices[svc->device_count];
        snprintf(dev->path, sizeof(dev->path), "%s/%s", IIO_BASE_PATH, entry->d_name);

        if (detect_iio_type(dev->path, &dev->type)) {
            dev->available = true;
            svc->device_count++;

            static const char *type_names[] = {
                "Accelerometer", "Gyroscope", "Proximity", "Light", "Magnetometer"
            };
            g_message("[Sensors] Found %s at %s",
                      type_names[dev->type], dev->path);
        }
    }
    closedir(dir);

    if (svc->device_count == 0) {
        g_message("[Sensors] No sensor hardware detected — service idle");
    }
}

/* ─── IIO 장치 경로 조회 ─── */
static const IioDevice *find_device(const ZylSensorService *svc,
                                     ZylSensorType type) {
    for (int i = 0; i < svc->device_count; i++) {
        if (svc->devices[i].available && svc->devices[i].type == type)
            return &svc->devices[i];
    }
    return NULL;
}

/* ─── 센서 값 읽기: 가속도계 ─── */
static bool read_accel(const char *dev_path, ZylSensorEvent *evt) {
    char path[SYSFS_BUF];
    double scale;

    snprintf(path, sizeof(path), "%s/in_accel_scale", dev_path);
    scale = sysfs_read_double(path);
    if (scale == 0.0) scale = 1.0;

    snprintf(path, sizeof(path), "%s/in_accel_x_raw", dev_path);
    double rx = sysfs_read_double(path);
    snprintf(path, sizeof(path), "%s/in_accel_y_raw", dev_path);
    double ry = sysfs_read_double(path);
    snprintf(path, sizeof(path), "%s/in_accel_z_raw", dev_path);
    double rz = sysfs_read_double(path);

    evt->type = ZYL_SENSOR_ACCELEROMETER;
    evt->data.accel.x = (float)(rx * scale);
    evt->data.accel.y = (float)(ry * scale);
    evt->data.accel.z = (float)(rz * scale);
    evt->timestamp_ns = now_ns();
    return true;
}

/* ─── 센서 값 읽기: 자이로스코프 ─── */
static bool read_gyro(const char *dev_path, ZylSensorEvent *evt) {
    char path[SYSFS_BUF];
    double scale;

    snprintf(path, sizeof(path), "%s/in_anglvel_scale", dev_path);
    scale = sysfs_read_double(path);
    if (scale == 0.0) scale = 1.0;

    snprintf(path, sizeof(path), "%s/in_anglvel_x_raw", dev_path);
    double rx = sysfs_read_double(path);
    snprintf(path, sizeof(path), "%s/in_anglvel_y_raw", dev_path);
    double ry = sysfs_read_double(path);
    snprintf(path, sizeof(path), "%s/in_anglvel_z_raw", dev_path);
    double rz = sysfs_read_double(path);

    evt->type = ZYL_SENSOR_GYROSCOPE;
    evt->data.gyro.x = (float)(rx * scale);
    evt->data.gyro.y = (float)(ry * scale);
    evt->data.gyro.z = (float)(rz * scale);
    evt->timestamp_ns = now_ns();
    return true;
}

/* ─── 센서 값 읽기: 근접 ─── */
static bool read_proximity(const char *dev_path, ZylSensorEvent *evt) {
    char path[SYSFS_BUF];
    double scale;

    snprintf(path, sizeof(path), "%s/in_proximity_scale", dev_path);
    scale = sysfs_read_double(path);
    if (scale == 0.0) scale = 1.0;

    snprintf(path, sizeof(path), "%s/in_proximity_raw", dev_path);
    double raw = sysfs_read_double(path);
    double distance = raw * scale;

    evt->type = ZYL_SENSOR_PROXIMITY;
    evt->data.proximity.distance_cm = (float)distance;
    evt->data.proximity.near = (distance < 5.0);
    evt->timestamp_ns = now_ns();
    return true;
}

/* ─── 센서 값 읽기: 조도 ─── */
static bool read_light(const char *dev_path, ZylSensorEvent *evt) {
    char path[SYSFS_BUF];
    double scale;

    snprintf(path, sizeof(path), "%s/in_illuminance_scale", dev_path);
    scale = sysfs_read_double(path);
    if (scale == 0.0) scale = 1.0;

    snprintf(path, sizeof(path), "%s/in_illuminance_raw", dev_path);
    double raw = sysfs_read_double(path);

    evt->type = ZYL_SENSOR_LIGHT;
    evt->data.light.lux = (float)(raw * scale);
    evt->timestamp_ns = now_ns();
    return true;
}

/* ─── 센서 값 읽기: 자기장 (heading 계산) ─── */
static bool read_magnetometer(const char *dev_path, ZylSensorEvent *evt) {
    char path[SYSFS_BUF];
    double scale;

    snprintf(path, sizeof(path), "%s/in_magn_scale", dev_path);
    scale = sysfs_read_double(path);
    if (scale == 0.0) scale = 1.0;

    snprintf(path, sizeof(path), "%s/in_magn_x_raw", dev_path);
    double mx = sysfs_read_double(path);
    snprintf(path, sizeof(path), "%s/in_magn_y_raw", dev_path);
    double my = sysfs_read_double(path);

    /* heading = atan2(y, x) → degrees */
    double heading = atan2(my * scale, mx * scale) * 180.0 / M_PI;
    if (heading < 0.0) heading += 360.0;

    evt->type = ZYL_SENSOR_MAGNETOMETER;
    evt->data.compass.heading_deg = (float)heading;
    evt->timestamp_ns = now_ns();
    return true;
}

/* ─── 센서 읽기 디스패치 ─── */
static bool read_sensor(const IioDevice *dev, ZylSensorEvent *evt) {
    switch (dev->type) {
    case ZYL_SENSOR_ACCELEROMETER: return read_accel(dev->path, evt);
    case ZYL_SENSOR_GYROSCOPE:     return read_gyro(dev->path, evt);
    case ZYL_SENSOR_PROXIMITY:     return read_proximity(dev->path, evt);
    case ZYL_SENSOR_LIGHT:         return read_light(dev->path, evt);
    case ZYL_SENSOR_MAGNETOMETER:  return read_magnetometer(dev->path, evt);
    }
    return false;
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

    const IioDevice *dev = find_device(svc, lis->type);
    if (!dev) {
        g_warning("[Sensors] No IIO device for type %d — poll thread exiting",
                  lis->type);
        lis->thread_running = false;
        return NULL;
    }

    long interval_us = (long)(1000000.0 / lis->rate_hz);
    if (interval_us < 1000) interval_us = 1000;
    if (interval_us > 10000000) interval_us = 10000000;

    g_message("[Sensors] Poll thread started: type=%d rate=%.1f Hz",
              lis->type, lis->rate_hz);

    while (lis->thread_running) {
        usleep((useconds_t)interval_us);
        if (!lis->active || !lis->thread_running) break;

        ZylSensorEvent evt = {0};
        if (!read_sensor(dev, &evt)) continue;

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

    /* IIO 장치 스캔 */
    scan_iio_devices(svc);

    /* D-Bus 등록 */
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
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
    if (!svc || !cb) return -1;
    if (rate_hz <= 0.0f) rate_hz = 1.0f;
    if (rate_hz > 1000.0f) rate_hz = 1000.0f;

    /* 해당 센서의 IIO 장치 확인 */
    const IioDevice *dev = find_device(svc, type);
    if (!dev) {
        g_warning("[Sensors] No IIO device for sensor type %d", type);
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
