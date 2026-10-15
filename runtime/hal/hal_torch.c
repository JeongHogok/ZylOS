/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Torch HAL Linux 구현 — 카메라 플래시 LED 를 손전등으로
 * 수행범위: /sys/class/leds 에서 이름에 flash 또는 torch 가 든 LED(커널 LED
 *           flash 클래스·gpio-leds 의 관례)를 찾아 켜기(max_brightness)·끄기·읽기.
 *           없으면 present=false — 손전등은 비활성
 * 의존방향: hal.h (Domain), glib
 * SOLID: SRP — 플래시 LED 접근만
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#ifndef ZYL_SYSFS_ROOT
#define ZYL_SYSFS_ROOT ""
#endif
#define LEDS_DIR ZYL_SYSFS_ROOT "/sys/class/leds"

/* sysfs attributes are written in place: no temp file and rename */
static bool write_attr(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = fputs(value, f) >= 0;
    return fclose(f) == 0 && ok;
}

static bool torch_led(char *name, size_t len) {
    GDir *dir = g_dir_open(LEDS_DIR, 0, NULL);
    const char *entry;
    bool found = false;
    while (dir && !found && (entry = g_dir_read_name(dir))) {
        if (strstr(entry, "flash") || strstr(entry, "torch")) {
            g_strlcpy(name, entry, len);
            found = true;
        }
    }
    if (dir) g_dir_close(dir);
    return found;
}

static int read_int(const char *led, const char *file) {
    char *path = g_strdup_printf(LEDS_DIR "/%s/%s", led, file), *text = NULL;
    int v = g_file_get_contents(path, &text, NULL, NULL) ? atoi(text) : -1;
    g_free(text);
    g_free(path);
    return v;
}

static bool torch_present(void) {
    char led[128];
    return torch_led(led, sizeof(led));
}

static bool torch_get(void) {
    char led[128];
    return torch_led(led, sizeof(led)) && read_int(led, "brightness") > 0;
}

static bool torch_set(bool on) {
    char led[128];
    if (!torch_led(led, sizeof(led))) return false;
    int max = read_int(led, "max_brightness");
    char value[16], *path = g_strdup_printf(LEDS_DIR "/%s/brightness", led);
    snprintf(value, sizeof(value), "%d", on ? (max > 0 ? max : 1) : 0);
    bool ok = write_attr(path, value);
    g_free(path);
    return ok;
}

static const ZylTorchHal torch_hal = { .present = torch_present, .get = torch_get, .set = torch_set };

const ZylTorchHal *zyl_hal_torch(void) { return &torch_hal; }
