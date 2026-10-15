/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Display HAL Linux 구현 — DRM 출력·모드(읽기)와 백라이트
 * 수행범위: /sys/class/drm 의 cardN-<커넥터> 마다 연결 여부와 modes 목록(커널이
 *           선호 모드를 먼저 적음, 주사율이 없으면 mode 파일 또는 60Hz),
 *           /sys/class/backlight 의 첫 장치로 밝기 0–100% 읽기·쓰기
 * 의존방향: hal.h (Domain), glib
 * SOLID: SRP — 화면 장치 접근만. 해상도·배율·회전 정책은 display 서비스,
 *        밝기 정책(어두워짐·꺼짐)은 power 서비스 몫
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#ifndef ZYL_SYSFS_ROOT
#define ZYL_SYSFS_ROOT ""
#endif
#define DRM_DIR       ZYL_SYSFS_ROOT "/sys/class/drm"
#define BACKLIGHT_DIR ZYL_SYSFS_ROOT "/sys/class/backlight"

/* sysfs attributes are written in place: no temp file and rename */
static bool write_attr(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = fputs(value, f) >= 0;
    return fclose(f) == 0 && ok;
}

static char *read_text(const char *dir, const char *name, const char *file) {
    char *path = g_strdup_printf("%s/%s/%s", dir, name, file);
    char *text = NULL;
    if (!g_file_get_contents(path, &text, NULL, NULL)) text = NULL;
    else g_strstrip(text);
    g_free(path);
    return text;
}

static int read_int(const char *dir, const char *name, const char *file) {
    char *t = read_text(dir, name, file);
    int v = t ? atoi(t) : -1;
    g_free(t);
    return v;
}

static int display_list_outputs(ZylDisplayOutput *out, int max) {
    GDir *dir = g_dir_open(DRM_DIR, 0, NULL);
    if (!dir) return 0;
    int n = 0;
    const char *name;
    while (n < max && (name = g_dir_read_name(dir))) {
        /* card0-HDMI-A-1, card0-DSI-1 — not card0 itself, not renderD128 */
        const char *dash = g_str_has_prefix(name, "card") ? strchr(name + 4, '-') : NULL;
        if (!dash) continue;
        ZylDisplayOutput *o = &out[n++];
        memset(o, 0, sizeof(*o));
        g_strlcpy(o->name, dash + 1, sizeof(o->name));
        char *status = read_text(DRM_DIR, name, "status");
        o->connected = status && strcmp(status, "connected") == 0;
        g_free(status);
        char *modes = read_text(DRM_DIR, name, "modes");
        char *current = read_text(DRM_DIR, name, "mode");
        char **lines = modes ? g_strsplit(modes, "\n", -1) : NULL;
        for (int i = 0; lines && lines[i] && o->mode_count < ZYL_HAL_MAX_MODES; i++) {
            int w = 0, h = 0, hz = 0;
            if (sscanf(lines[i], "%dx%d@%d", &w, &h, &hz) < 2 || w <= 0 || h <= 0) continue;
            int cw = 0, ch = 0, chz = 0;
            if (hz <= 0 && current && sscanf(current, "%dx%d@%d", &cw, &ch, &chz) == 3 && cw == w && ch == h)
                hz = chz;
            ZylDisplayModeInfo *m = &o->modes[o->mode_count++];
            m->width = w;
            m->height = h;
            m->refresh_hz = hz > 0 ? hz : 60;     /* sysfs modes carry no rate */
        }
        g_strfreev(lines);
        g_free(modes);
        g_free(current);
    }
    g_dir_close(dir);
    return n;
}

/* The first backlight with a usable range */
static bool backlight(char *name, size_t len, int *max) {
    GDir *dir = g_dir_open(BACKLIGHT_DIR, 0, NULL);
    const char *entry;
    bool found = false;
    while (dir && !found && (entry = g_dir_read_name(dir))) {
        int m = read_int(BACKLIGHT_DIR, entry, "max_brightness");
        if (m > 0) {
            g_strlcpy(name, entry, len);
            *max = m;
            found = true;
        }
    }
    if (dir) g_dir_close(dir);
    return found;
}

static bool display_backlight_present(void) {
    char name[128];
    int max;
    return backlight(name, sizeof(name), &max);
}

static int display_get_brightness(void) {
    char name[128];
    int max;
    if (!backlight(name, sizeof(name), &max)) return -1;
    int raw = read_int(BACKLIGHT_DIR, name, "brightness");
    return raw < 0 ? -1 : CLAMP((raw * 100 + max / 2) / max, 0, 100);
}

static bool display_set_brightness(int percent) {
    char name[128];
    int max;
    if (!backlight(name, sizeof(name), &max)) return false;
    char *path = g_strdup_printf(BACKLIGHT_DIR "/%s/brightness", name);
    char value[16];
    snprintf(value, sizeof(value), "%d", CLAMP(percent, 0, 100) * max / 100);
    bool ok = write_attr(path, value);
    g_free(path);
    return ok;
}

static const ZylDisplayHal display_hal = {
    .list_outputs      = display_list_outputs,
    .backlight_present = display_backlight_present,
    .get_brightness    = display_get_brightness,
    .set_brightness    = display_set_brightness,
};

const ZylDisplayHal *zyl_hal_display(void) { return &display_hal; }
