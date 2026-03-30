/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Implementation
 *
 * 역할: Display HAL Linux 구현체 — sysfs backlight + DRM/KMS
 * 수행범위: 밝기 조절, 자동 밝기, 다크 모드, 폰트 크기, 화면 타임아웃
 * 의존방향: hal.h (Domain), stdio/stdlib (Platform)
 * SOLID: DIP — hal.h 인터페이스 구현, SRP — 디스플레이 제어만 담당
 * ────────────────────────────────────────────────────────── */

#include "../hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#define BACKLIGHT_BASE "/sys/class/backlight"
#define SETTINGS_DIR   "/data/settings"

static char g_bl_path[256] = {0};       /* e.g. /sys/class/backlight/intel_backlight */
static int  g_max_brightness = 0;
static ZylDisplayState g_state = {
    .brightness_percent = 80,
    .auto_brightness = false,
    .dark_mode = false,
    .font_size = "medium",
    .screen_timeout_sec = 60,
};

/* ─── sysfs helpers ─── */
static int sysfs_read_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

static int sysfs_write_int(const char *path, int val) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d", val);
    fclose(f);
    return 0;
}

static int settings_write(const char *key, const char *value) {
    char path[512];
    snprintf(path, sizeof(path), "%s/display_%s", SETTINGS_DIR, key);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s", value);
    fclose(f);
    return 0;
}

static int settings_read(const char *key, char *out, size_t out_len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/display_%s", SETTINGS_DIR, key);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(out, (int)out_len, f)) { fclose(f); return -1; }
    /* Strip newline */
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
    fclose(f);
    return 0;
}

/* ─── Find backlight device ─── */
static int find_backlight(void) {
    DIR *d = opendir(BACKLIGHT_BASE);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        snprintf(g_bl_path, sizeof(g_bl_path), "%s/%s", BACKLIGHT_BASE, ent->d_name);
        break;
    }
    closedir(d);

    if (!g_bl_path[0]) return -1;

    char max_path[512];
    snprintf(max_path, sizeof(max_path), "%s/max_brightness", g_bl_path);
    g_max_brightness = sysfs_read_int(max_path);
    return (g_max_brightness > 0) ? 0 : -1;
}

/* ─── HAL implementation ─── */
static int display_init(void) {
    int ret = find_backlight();

    /* Load persisted settings */
    char buf[64];
    if (settings_read("dark_mode", buf, sizeof(buf)) == 0)
        g_state.dark_mode = (strcmp(buf, "1") == 0);
    if (settings_read("font_size", buf, sizeof(buf)) == 0)
        snprintf(g_state.font_size, sizeof(g_state.font_size), "%s", buf);
    if (settings_read("screen_timeout", buf, sizeof(buf)) == 0)
        g_state.screen_timeout_sec = atoi(buf);
    if (settings_read("auto_brightness", buf, sizeof(buf)) == 0)
        g_state.auto_brightness = (strcmp(buf, "1") == 0);

    /* Read current brightness */
    if (ret == 0 && g_max_brightness > 0) {
        char cur_path[512];
        snprintf(cur_path, sizeof(cur_path), "%s/brightness", g_bl_path);
        int cur = sysfs_read_int(cur_path);
        if (cur >= 0) {
            g_state.brightness_percent = (cur * 100) / g_max_brightness;
        }
    }

    return ret;
}

static void display_shutdown(void) {
    /* No resources to release */
}

static int display_set_brightness(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_state.brightness_percent = percent;

    if (g_bl_path[0] && g_max_brightness > 0) {
        int raw = (percent * g_max_brightness) / 100;
        /* Minimum 1 to avoid fully off display */
        if (raw < 1 && percent > 0) raw = 1;
        char path[512];
        snprintf(path, sizeof(path), "%s/brightness", g_bl_path);
        return sysfs_write_int(path, raw);
    }
    return 0;
}

static int display_get_brightness(int *out_percent) {
    if (!out_percent) return -1;
    *out_percent = g_state.brightness_percent;
    return 0;
}

static int display_set_auto_brightness(bool enabled) {
    g_state.auto_brightness = enabled;
    settings_write("auto_brightness", enabled ? "1" : "0");
    return 0;
}

static int display_set_dark_mode(bool enabled) {
    g_state.dark_mode = enabled;
    settings_write("dark_mode", enabled ? "1" : "0");
    return 0;
}

static int display_set_font_size(const char *size) {
    if (!size) return -1;
    /* Validate */
    if (strcmp(size, "small") != 0 && strcmp(size, "medium") != 0 &&
        strcmp(size, "large") != 0) {
        return -1;
    }
    snprintf(g_state.font_size, sizeof(g_state.font_size), "%s", size);
    settings_write("font_size", size);
    return 0;
}

static int display_set_screen_timeout(int seconds) {
    if (seconds < 0) seconds = 0;
    g_state.screen_timeout_sec = seconds;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", seconds);
    settings_write("screen_timeout", buf);
    return 0;
}

static int display_get_state(ZylDisplayState *out) {
    if (!out) return -1;
    *out = g_state;
    return 0;
}

/* ─── HAL 인스턴스 ─── */
static ZylDisplayHal display_hal_instance = {
    .init               = display_init,
    .shutdown            = display_shutdown,
    .set_brightness      = display_set_brightness,
    .get_brightness      = display_get_brightness,
    .set_auto_brightness = display_set_auto_brightness,
    .set_dark_mode       = display_set_dark_mode,
    .set_font_size       = display_set_font_size,
    .set_screen_timeout  = display_set_screen_timeout,
    .get_state           = display_get_state,
};

ZylDisplayHal *zyl_hal_display_linux(void) {
    return &display_hal_instance;
}
