/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Sensors HAL Linux 구현 — IIO 센서
 * 수행범위: /sys/bus/iio/devices 에서 종류별 채널(in_accel_x_raw 등)을 가진 장치를
 *           찾고, 한 샘플을 raw × scale 로 읽는다(scale 이 없으면 1). 경로가 버퍼에
 *           다 들어가지 않는 장치는 건너뛴다 — 잘린 경로가 다른 파일을 가리키지 않게
 * 의존방향: hal.h (Domain), dirent, pthread
 * SOLID: SRP — IIO 읽기와 단위 환산만. 근접 판정·방위각은 서비스 몫
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Overridable so tests point at a fake sysfs */
#ifndef ZYL_SYSFS_ROOT
#define ZYL_SYSFS_ROOT ""
#endif
#define IIO_DIR      ZYL_SYSFS_ROOT "/sys/bus/iio/devices"
#define SYSFS_BUF    256
#define IIO_ATTR_MAX 32    /* "/in_illuminance_scale" and the like, with room */

/* Per kind: the IIO channel and how many axes it has */
static const struct { const char *chan; int axes; } CHANNELS[ZYL_HAL_SENSOR_COUNT] = {
    [ZYL_HAL_SENSOR_ACCEL]     = { "accel",       3 },
    [ZYL_HAL_SENSOR_GYRO]      = { "anglvel",     3 },
    [ZYL_HAL_SENSOR_PROXIMITY] = { "proximity",   1 },
    [ZYL_HAL_SENSOR_LIGHT]     = { "illuminance", 1 },
    [ZYL_HAL_SENSOR_MAGNETIC]  = { "magn",        3 },
};

/* The device present() found for each kind, "" for none; poll threads read it */
static char g_device[ZYL_HAL_SENSOR_COUNT][SYSFS_BUF];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* in_accel_x_raw, in_illuminance_raw (one axis), in_accel_scale (axis < 0) */
static void attr_path(char *out, const char *dev, ZylSensorKind kind, int axis, const char *suffix) {
    if (axis < 0 || CHANNELS[kind].axes == 1)
        snprintf(out, SYSFS_BUF, "%s/in_%s_%s", dev, CHANNELS[kind].chan, suffix);
    else
        snprintf(out, SYSFS_BUF, "%s/in_%s_%c_%s", dev, CHANNELS[kind].chan, "xyz"[axis], suffix);
}

static bool read_double(const char *path, double *out) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    bool ok = fscanf(f, "%lf", out) == 1;
    fclose(f);
    return ok;
}

static bool find_device(ZylSensorKind kind, char *dev) {
    DIR *dir = opendir(IIO_DIR);
    if (!dir) return false;
    struct dirent *entry;
    bool found = false;
    while (!found && (entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0) continue;
        /* Every attribute path is this plus "/<attr>": take the device only when
           all of them fit, so no read below can name a cut-off (other) file */
        int n = snprintf(dev, SYSFS_BUF, IIO_DIR "/%s", entry->d_name);
        if (n < 0 || (size_t)n + IIO_ATTR_MAX >= SYSFS_BUF) continue;
        char raw[SYSFS_BUF];
        attr_path(raw, dev, kind, 0, "raw");
        found = access(raw, R_OK) == 0;
    }
    closedir(dir);
    if (!found) dev[0] = '\0';
    return found;
}

static bool sensors_present(ZylSensorKind kind) {
    if ((unsigned)kind >= ZYL_HAL_SENSOR_COUNT) return false;
    char dev[SYSFS_BUF];
    bool found = find_device(kind, dev);
    pthread_mutex_lock(&g_lock);
    memcpy(g_device[kind], dev, sizeof(dev));
    pthread_mutex_unlock(&g_lock);
    return found;
}

static bool sensors_read(ZylSensorKind kind, ZylSensorSample *out) {
    if ((unsigned)kind >= ZYL_HAL_SENSOR_COUNT || !out) return false;
    char dev[SYSFS_BUF], path[SYSFS_BUF];
    pthread_mutex_lock(&g_lock);
    memcpy(dev, g_device[kind], sizeof(dev));
    pthread_mutex_unlock(&g_lock);
    if (!dev[0]) return false;

    double scale;
    attr_path(path, dev, kind, -1, "scale");
    if (!read_double(path, &scale) || scale == 0.0) scale = 1.0;

    memset(out, 0, sizeof(*out));
    for (int axis = 0; axis < CHANNELS[kind].axes; axis++) {
        double raw;
        attr_path(path, dev, kind, axis, "raw");
        if (!read_double(path, &raw)) return false;
        out->v[axis] = raw * scale;
    }
    return true;
}

static const ZylSensorsHal sensors_hal = { .present = sensors_present, .read = sensors_read };

const ZylSensorsHal *zyl_hal_sensors(void) { return &sensors_hal; }
