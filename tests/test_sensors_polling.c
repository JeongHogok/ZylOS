/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 센서 서비스(runtime/services/sensors) — 시작하면 바로 읽기 시작하는지
 * 수행범위: 가짜 IIO sysfs 를 스캔해 폴링 스레드를 띄우고 최신값을 채우는지,
 *           장치가 없으면 조용히 비어 있는지
 * 의존방향: sensors.c (ZYL_TEST, IIO_BASE_PATH 교체), glib, pthread
 * SOLID: SRP — "아무도 시작시키지 않으면 영구히 0" 회귀만 검증
 *
 * D-Bus 에 구독 메서드가 없어 폴링을 시작시키는 쪽이 없었고, 그래서
 * GetLatest 는 센서가 있어도 늘 0 을 돌려줬다.
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-sensors-test"
#define IIO_BASE_PATH ROOT_
#include "../runtime/services/sensors/sensors.c"
#include <assert.h>

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

/* An accelerometer as the kernel's IIO driver exposes one */
static void make_accel(void) {
    char p[512];
    assert(g_mkdir_with_parents(ROOT_ "/iio:device0", 0700) == 0);
    snprintf(p, sizeof(p), "%s/iio:device0/in_accel_scale", ROOT_);
    write_file(p, "0.01\n");
    snprintf(p, sizeof(p), "%s/iio:device0/in_accel_x_raw", ROOT_);
    write_file(p, "100\n");
    snprintf(p, sizeof(p), "%s/iio:device0/in_accel_y_raw", ROOT_);
    write_file(p, "0\n");
    snprintf(p, sizeof(p), "%s/iio:device0/in_accel_z_raw", ROOT_);
    write_file(p, "981\n");
}

/* DEFAULT_RATE_HZ is 5 Hz: one sample is due within 200 ms */
static void wait_for_sample(ZylSensorService *svc, ZylSensorEvent *out) {
    for (int i = 0; i < 40; i++) {
        if (zyl_sensor_get_latest(svc, ZYL_SENSOR_ACCELEROMETER, out) == 0) return;
        usleep(25000);
    }
}

int main(void) {
    printf("=== sensors polling tests ===\n");
    assert(system("rm -rf " ROOT_) == 0);
    make_accel();

    ZylSensorService *svc = zyl_sensor_create();
    assert(svc);
    assert(svc->device_count == 1);
    assert(svc->devices[0].type == ZYL_SENSOR_ACCELEROMETER);

    ZylSensorEvent evt = {0};
    wait_for_sample(svc, &evt);
    /* Unasked-for polling is the whole point: a fresh service has a value */
    assert(zyl_sensor_get_latest(svc, ZYL_SENSOR_ACCELEROMETER, &evt) == 0);
    assert(evt.timestamp_ns > 0);
    assert(evt.data.accel.x > 0.9f && evt.data.accel.x < 1.1f);   /* 100 * 0.01 */
    assert(evt.data.accel.z > 9.7f && evt.data.accel.z < 9.9f);   /* 981 * 0.01 */
    /* A sensor this device does not have stays empty */
    assert(zyl_sensor_get_latest(svc, ZYL_SENSOR_GYROSCOPE, &evt) == -1);
    printf("  found hardware is polled from the start                    PASS\n");

    zyl_sensor_destroy(svc);

    /* No IIO at all: the service comes up idle instead of failing */
    assert(system("rm -rf " ROOT_) == 0);
    svc = zyl_sensor_create();
    assert(svc && svc->device_count == 0);
    assert(zyl_sensor_get_latest(svc, ZYL_SENSOR_ACCELEROMETER, &evt) == -1);
    zyl_sensor_destroy(svc);
    printf("  no sensor hardware: idle, not broken                       PASS\n");

    printf("=== 2/2 passed ===\n");
    return 0;
}
