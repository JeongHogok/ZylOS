/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Battery HAL Linux 구현 — /sys/class/power_supply
 * 수행범위: type 이 Battery 인 장치의 잔량(capacity)·충전 상태(status), Mains/USB
 *           장치의 online 으로 외부 전원 연결. 배터리가 없는 보드(개발 보드)는
 *           present=false — 지어내지 않는다
 * 의존방향: hal.h (Domain), glib
 * SOLID: SRP — 전원 장치 읽기만. 표시·절전 정책은 서비스 몫
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#ifndef ZYL_SYSFS_ROOT
#define ZYL_SYSFS_ROOT ""
#endif
#define SUPPLY_DIR ZYL_SYSFS_ROOT "/sys/class/power_supply"

static char *read_text(const char *name, const char *file) {
    char *path = g_strdup_printf(SUPPLY_DIR "/%s/%s", name, file);
    char *text = NULL;
    if (g_file_get_contents(path, &text, NULL, NULL)) g_strstrip(text);
    else text = NULL;
    g_free(path);
    return text;
}

static bool battery_get(ZylBatteryState *out) {
    memset(out, 0, sizeof(*out));
    out->level_percent = -1;
    GDir *dir = g_dir_open(SUPPLY_DIR, 0, NULL);
    if (!dir) return true;                       /* no supplies known: no battery */
    const char *name;
    while ((name = g_dir_read_name(dir))) {
        char *type = read_text(name, "type");
        if (type && strcmp(type, "Battery") == 0 && !out->present) {
            out->present = true;
            char *cap = read_text(name, "capacity");
            char *status = read_text(name, "status");
            if (cap) out->level_percent = CLAMP(atoi(cap), 0, 100);
            out->charging = status && (strcmp(status, "Charging") == 0 || strcmp(status, "Full") == 0);
            g_free(cap);
            g_free(status);
        } else if (type && (strcmp(type, "Mains") == 0 || g_str_has_prefix(type, "USB"))) {
            char *online = read_text(name, "online");
            if (online && strcmp(online, "1") == 0) out->plugged = true;
            g_free(online);
        }
        g_free(type);
    }
    g_dir_close(dir);
    if (out->charging) out->plugged = true;
    return true;
}

static const ZylBatteryHal battery_hal = { .get = battery_get };

const ZylBatteryHal *zyl_hal_battery(void) { return &battery_hal; }
