#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 백라이트 장치 감지 및 밝기 제어
 * 수행범위: sysfs backlight 장치 탐색, 밝기 읽기/쓰기
 * 의존방향: power_internal.h, sysfs
 * SOLID: SRP — 백라이트 관련 기능만 담당
 * ────────────────────────────────────────────────────────── */

#include "power_internal.h"

/* ─── backlight 장치 자동 감지 ─── */
char *detect_backlight_device(int *max_brightness) {
    GDir *dir = g_dir_open(BACKLIGHT_PATH, 0, NULL);
    if (!dir) return NULL;

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        char max_path[256];
        snprintf(max_path, sizeof(max_path),
                 "%s/%s/max_brightness", BACKLIGHT_PATH, name);
        int max_val = sysfs_read_int(max_path);
        if (max_val > 0) {
            *max_brightness = max_val;
            char *device = g_strdup_printf("%s/%s", BACKLIGHT_PATH, name);
            g_dir_close(dir);
            return device;
        }
    }
    g_dir_close(dir);
    return NULL;
}

/* ─── 밝기 설정 ─── */
int zyl_power_set_brightness(ZylPowerService *svc, int percent) {
    if (!svc) return -1;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    svc->brightness = percent;

    /* sysfs backlight 제어 */
    if (svc->backlight_device && svc->max_brightness > 0) {
        char path[256];
        snprintf(path, sizeof(path), "%s/brightness", svc->backlight_device);
        int raw = percent * svc->max_brightness / 100;
        sysfs_write_int(path, raw);
    }

    return 0;
}

/* ─── 밝기 조회 ─── */
int zyl_power_get_brightness(const ZylPowerService *svc) {
    return svc ? svc->brightness : -1;
}
