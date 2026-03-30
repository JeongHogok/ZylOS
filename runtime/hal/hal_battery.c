/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Implementation
 *
 * 역할: Battery HAL Linux 구현체 — sysfs power_supply
 * 수행범위: 배터리 레벨, 충전 상태, 건강, 온도, 전압 조회, 변경 콜백
 * 의존방향: hal.h (Domain), stdio/dirent (Platform)
 * SOLID: DIP — hal.h 인터페이스 구현, SRP — 배터리 정보만 담당
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>

#define POWER_SUPPLY_BASE "/sys/class/power_supply"

static char g_bat_path[256] = {0};   /* e.g. /sys/class/power_supply/BAT0 */
static void (*g_callback)(const ZylBatteryState *, void *) = NULL;
static void *g_cb_data = NULL;
static pthread_t g_monitor_thread;
static volatile int g_running = 0;

/* ─── sysfs helpers ─── */
static int sysfs_read_int(const char *dir, const char *file) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

static int sysfs_read_str(const char *dir, const char *file, char *out, size_t len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(out, (int)len, f)) { fclose(f); return -1; }
    size_t slen = strlen(out);
    if (slen > 0 && out[slen - 1] == '\n') out[slen - 1] = '\0';
    fclose(f);
    return 0;
}

/* ─── Find battery device ─── */
static int find_battery(void) {
    DIR *d = opendir(POWER_SUPPLY_BASE);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char type_path[512];
        snprintf(type_path, sizeof(type_path), "%s/%s", POWER_SUPPLY_BASE, ent->d_name);
        char type[32] = {0};
        if (sysfs_read_str(type_path, "type", type, sizeof(type)) == 0) {
            if (strcmp(type, "Battery") == 0) {
                snprintf(g_bat_path, sizeof(g_bat_path), "%s", type_path);
                closedir(d);
                return 0;
            }
        }
    }
    closedir(d);
    return -1;
}

static int read_battery_state(ZylBatteryState *out) {
    if (!out || !g_bat_path[0]) return -1;
    memset(out, 0, sizeof(*out));

    /* Level */
    int cap = sysfs_read_int(g_bat_path, "capacity");
    out->level_percent = (cap >= 0) ? cap : -1;

    /* Charging */
    char status[32] = {0};
    sysfs_read_str(g_bat_path, "status", status, sizeof(status));
    out->charging = (strcmp(status, "Charging") == 0 || strcmp(status, "Full") == 0);

    /* USB connected */
    out->usb_connected = out->charging; /* Simplification — real impl checks usb/ac supply */

    /* Health */
    char health[32] = {0};
    if (sysfs_read_str(g_bat_path, "health", health, sizeof(health)) == 0) {
        snprintf(out->health, sizeof(out->health), "%s", health);
    } else {
        snprintf(out->health, sizeof(out->health), "Good");
    }

    /* Temperature (in 0.1°C) → ×10 for our format */
    int temp = sysfs_read_int(g_bat_path, "temp");
    out->temperature_c10 = (temp >= 0) ? temp : 0;

    /* Voltage (in µV) → mV */
    int voltage_uv = sysfs_read_int(g_bat_path, "voltage_now");
    out->voltage_mv = (voltage_uv >= 0) ? (voltage_uv / 1000) : 0;

    return 0;
}

/* ─── Monitor thread: poll battery state every 30s ─── */
static void *battery_monitor(void *arg) {
    (void)arg;
    ZylBatteryState prev = {0};
    read_battery_state(&prev);

    while (g_running) {
        sleep(30);
        if (!g_running) break;

        ZylBatteryState cur;
        if (read_battery_state(&cur) != 0) continue;

        /* Notify on change */
        if (g_callback && (cur.level_percent != prev.level_percent ||
                           cur.charging != prev.charging)) {
            g_callback(&cur, g_cb_data);
        }
        prev = cur;
    }
    return NULL;
}

/* ─── HAL implementation ─── */
static int battery_init(void) {
    return find_battery();
}

static void battery_shutdown(void) {
    if (g_running) {
        g_running = 0;
        pthread_join(g_monitor_thread, NULL);
    }
    g_callback = NULL;
    g_cb_data = NULL;
}

static int battery_get_state(ZylBatteryState *out) {
    return read_battery_state(out);
}

static int battery_set_change_callback(
    void (*cb)(const ZylBatteryState *, void *), void *user_data) {
    g_callback = cb;
    g_cb_data = user_data;

    if (!g_running && cb) {
        g_running = 1;
        if (pthread_create(&g_monitor_thread, NULL, battery_monitor, NULL) != 0) {
            g_running = 0;
            return -1;
        }
    }
    return 0;
}

/* ─── HAL 인스턴스 ─── */
static ZylBatteryHal battery_hal_instance = {
    .init                = battery_init,
    .shutdown            = battery_shutdown,
    .get_state           = battery_get_state,
    .set_change_callback = battery_set_change_callback,
};

ZylBatteryHal *zyl_hal_battery_linux(void) {
    return &battery_hal_instance;
}
