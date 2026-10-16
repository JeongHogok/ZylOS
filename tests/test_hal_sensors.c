/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: Sensors HAL Linux 구현(runtime/hal/hal_sensors.c) 단위 테스트
 * 수행범위: 종류별 채널로 IIO 장치 찾기(가속도·자이로가 한 칩이어도 둘 다), raw × scale
 *           (scale 없으면 1), 읽을 수 없는 값은 0 이 아니라 실패, 경로가 버퍼를 넘는
 *           장치는 건너뜀, IIO 가 없으면 아무것도 없음
 * 의존방향: hal_sensors.c (ZYL_SYSFS_ROOT 로 가짜 sysfs), glib
 * SOLID: SRP — IIO 탐색과 단위 환산 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-hal-sensors-test"
#define ZYL_SYSFS_ROOT ROOT_
#include "../runtime/hal/hal_sensors.c"
#include <assert.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>

#define DEV_ ROOT_ "/sys/bus/iio/devices/"

static void attr(const char *device, const char *name, const char *value) {
    char *dir = g_strconcat(DEV_, device, NULL), *file = g_strconcat(dir, "/", name, NULL);
    assert(g_mkdir_with_parents(dir, 0755) == 0);
    assert(g_file_set_contents(file, value, -1, NULL));
    g_free(file);
    g_free(dir);
}

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    const ZylSensorsHal *hal = zyl_hal_sensors();
    ZylSensorSample s;
    printf("=== Sensors HAL (linux) ===\n");
    assert(system("rm -rf " ROOT_) == 0);

    printf("  %-56s ", "no IIO subsystem: no sensors, nothing read");
    for (int k = 0; k < ZYL_HAL_SENSOR_COUNT; k++)
        assert(!hal->present(k) && !hal->read(k, &s));
    assert(!hal->present(ZYL_HAL_SENSOR_COUNT) && !hal->read(ZYL_HAL_SENSOR_COUNT, &s));
    printf("PASS\n");

    printf("  %-56s ", "each kind is found and scaled to its unit");
    /* An IMU chip with accelerometer and gyroscope channels on one device */
    attr("iio:device0", "in_accel_scale", "0.01\n");
    attr("iio:device0", "in_accel_x_raw", "100\n");
    attr("iio:device0", "in_accel_y_raw", "-50\n");
    attr("iio:device0", "in_accel_z_raw", "981\n");
    attr("iio:device0", "in_anglvel_scale", "0.001\n");
    attr("iio:device0", "in_anglvel_x_raw", "10\n");
    attr("iio:device0", "in_anglvel_y_raw", "-20\n");
    attr("iio:device0", "in_anglvel_z_raw", "30\n");
    attr("iio:device1", "in_illuminance_raw", "250\n");          /* no scale: 1 */
    attr("iio:device2", "in_proximity_raw", "3\n");
    attr("iio:device2", "in_proximity_scale", "0\n");            /* 0 would zero it: 1 */
    attr("iio:device3", "in_magn_scale", "0.5\n");
    attr("iio:device3", "in_magn_x_raw", "2\n");
    attr("iio:device3", "in_magn_y_raw", "4\n");
    attr("iio:device3", "in_magn_z_raw", "-6\n");
    for (int k = 0; k < ZYL_HAL_SENSOR_COUNT; k++) assert(hal->present(k));
    assert(hal->read(ZYL_HAL_SENSOR_ACCEL, &s));
    assert(near(s.v[0], 1.0) && near(s.v[1], -0.5) && near(s.v[2], 9.81));
    assert(hal->read(ZYL_HAL_SENSOR_GYRO, &s));
    assert(near(s.v[0], 0.01) && near(s.v[1], -0.02) && near(s.v[2], 0.03));
    assert(hal->read(ZYL_HAL_SENSOR_LIGHT, &s) && near(s.v[0], 250) && s.v[1] == 0 && s.v[2] == 0);
    assert(hal->read(ZYL_HAL_SENSOR_PROXIMITY, &s) && near(s.v[0], 3));
    assert(hal->read(ZYL_HAL_SENSOR_MAGNETIC, &s));
    assert(near(s.v[0], 1) && near(s.v[1], 2) && near(s.v[2], -3));
    assert(!hal->read(ZYL_HAL_SENSOR_ACCEL, NULL));
    printf("PASS\n");

    printf("  %-56s ", "a value that cannot be read is no reading, not 0");
    attr("iio:device1", "in_illuminance_raw", "busy\n");
    assert(!hal->read(ZYL_HAL_SENSOR_LIGHT, &s));
    assert(unlink(DEV_ "iio:device0/in_accel_y_raw") == 0);      /* found, then gone */
    assert(!hal->read(ZYL_HAL_SENSOR_ACCEL, &s));
    printf("PASS\n");

    printf("  %-56s ", "a device whose paths would not fit is skipped");
    assert(system("rm -rf " ROOT_) == 0);
    char name[200] = "iio:device";
    memset(name + 10, 'x', sizeof(name) - 11);
    attr(name, "in_accel_x_raw", "1\n");
    assert(!hal->present(ZYL_HAL_SENSOR_ACCEL) && !hal->read(ZYL_HAL_SENSOR_ACCEL, &s));
    attr("iio:device9", "in_accel_x_raw", "1\n");                 /* a sane one next to it */
    attr("iio:device9", "in_accel_y_raw", "2\n");
    attr("iio:device9", "in_accel_z_raw", "3\n");
    assert(hal->present(ZYL_HAL_SENSOR_ACCEL) && hal->read(ZYL_HAL_SENSOR_ACCEL, &s));
    assert(near(s.v[2], 3));
    printf("PASS\n");

    assert(system("rm -rf " ROOT_) == 0);
    printf("\nResults: 4/4 passed\n");
    return 0;
}
