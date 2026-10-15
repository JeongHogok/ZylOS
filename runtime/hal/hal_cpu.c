/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: CPU HAL Linux 구현 — cpufreq 거버너, 코어 온·오프라인
 * 수행범위: /sys/devices/system/cpu 의 코어 수, 모든 코어의 scaling_governor,
 *           cpuN/online (cpu0 은 항상 켜짐)
 * 의존방향: hal.h (Domain), glib
 * SOLID: SRP — CPU 장치 접근만. 언제 어떤 거버너를 쓸지(Doze)는 power 서비스 몫
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

#ifndef ZYL_SYSFS_ROOT
#define ZYL_SYSFS_ROOT ""
#endif
#define CPU_DIR ZYL_SYSFS_ROOT "/sys/devices/system/cpu"

static bool write_attr(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = fputs(value, f) >= 0;
    return fclose(f) == 0 && ok;
}

static int cpu_core_count(void) {
    GDir *dir = g_dir_open(CPU_DIR, 0, NULL);
    const char *name;
    int n = 0;
    while (dir && (name = g_dir_read_name(dir)))
        if (g_str_has_prefix(name, "cpu") && g_ascii_isdigit(name[3])) n++;
    if (dir) g_dir_close(dir);
    return n > 0 ? n : 1;
}

static bool cpu_set_core_online(int core, bool online) {
    if (core <= 0) return false;
    char path[128];
    snprintf(path, sizeof(path), CPU_DIR "/cpu%d/online", core);
    return write_attr(path, online ? "1" : "0");
}

static bool cpu_set_governor(const char *governor) {
    int set = 0;
    for (int i = 0; i < cpu_core_count(); i++) {
        char path[128];
        snprintf(path, sizeof(path), CPU_DIR "/cpu%d/cpufreq/scaling_governor", i);
        if (write_attr(path, governor)) set++;
    }
    return set > 0;
}

static const ZylCpuHal cpu_hal = {
    .core_count      = cpu_core_count,
    .set_core_online = cpu_set_core_online,
    .set_governor    = cpu_set_governor,
};

const ZylCpuHal *zyl_hal_cpu(void) { return &cpu_hal; }
