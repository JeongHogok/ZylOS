/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Implementation
 *
 * 역할: CPU HAL Linux 구현체 — cpufreq sysfs 제어
 * 수행범위: 거버너 설정, 주파수 조회, thermal 모니터링
 * 의존방향: hal.h (Domain), stdio (Platform)
 * SOLID: DIP — hal.h 인터페이스 구현, SRP — CPU 주파수 관리만
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#define CPUFREQ_BASE "/sys/devices/system/cpu/cpu0/cpufreq"
#define THERMAL_BASE "/sys/class/thermal"

/* ─── sysfs helpers ─── */

static int sysfs_read_str(const char *path, char *out, size_t len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(out, (int)len, f)) { fclose(f); return -1; }
    fclose(f);
    size_t l = strlen(out);
    if (l > 0 && out[l - 1] == '\n') out[l - 1] = '\0';
    return 0;
}

static int sysfs_write_str(const char *path, const char *val) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(val, f);
    fclose(f);
    return 0;
}

static int sysfs_read_int(const char *path) {
    char buf[32];
    if (sysfs_read_str(path, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* ─── 전력 프로필 → 거버너 매핑 ─── */

typedef enum {
    ZYL_POWER_PROFILE_PERFORMANCE = 0,
    ZYL_POWER_PROFILE_BALANCED    = 1,
    ZYL_POWER_PROFILE_POWERSAVE   = 2,
} ZylPowerProfile;

static const char *PROFILE_GOVERNORS[] = {
    "performance",  /* PERFORMANCE */
    "schedutil",    /* BALANCED — 커널 스케줄러 연동 */
    "powersave",    /* POWERSAVE */
};

static ZylPowerProfile current_profile = ZYL_POWER_PROFILE_BALANCED;

/* ─── CPU 코어 수 감지 ─── */

static int get_cpu_count(void) {
    int count = 0;
    DIR *d = opendir("/sys/devices/system/cpu");
    if (!d) return 1;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, "cpu", 3) == 0 &&
            entry->d_name[3] >= '0' && entry->d_name[3] <= '9') {
            count++;
        }
    }
    closedir(d);
    return count > 0 ? count : 1;
}

/* ─── 공개 API ─── */

int zyl_cpu_get_freq_khz(void) {
    return sysfs_read_int(CPUFREQ_BASE "/scaling_cur_freq");
}

int zyl_cpu_get_max_freq_khz(void) {
    return sysfs_read_int(CPUFREQ_BASE "/scaling_max_freq");
}

int zyl_cpu_get_min_freq_khz(void) {
    return sysfs_read_int(CPUFREQ_BASE "/scaling_min_freq");
}

int zyl_cpu_get_governor(char *out, size_t len) {
    return sysfs_read_str(CPUFREQ_BASE "/scaling_governor", out, len);
}

int zyl_cpu_set_governor(const char *governor) {
    int ncpu = get_cpu_count();
    int ok = 0;
    for (int i = 0; i < ncpu; i++) {
        char path[256];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
        if (sysfs_write_str(path, governor) == 0) ok++;
    }
    return ok > 0 ? 0 : -1;
}

int zyl_cpu_set_power_profile(int profile) {
    if (profile < 0 || profile > 2) return -1;
    current_profile = (ZylPowerProfile)profile;
    return zyl_cpu_set_governor(PROFILE_GOVERNORS[profile]);
}

int zyl_cpu_get_power_profile(void) {
    return (int)current_profile;
}

int zyl_cpu_get_temperature_mc(void) {
    /* Read thermal zone 0 (CPU) in millidegrees Celsius */
    return sysfs_read_int(THERMAL_BASE "/thermal_zone0/temp");
}

int zyl_cpu_get_core_count(void) {
    return get_cpu_count();
}

/* ─── 코어 핫플러그 ─── */

int zyl_cpu_set_core_online(int core, int online) {
    if (core <= 0) return -1; /* cpu0은 항상 online */
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/online", core);
    return sysfs_write_str(path, online ? "1" : "0");
}

int zyl_cpu_is_core_online(int core) {
    if (core == 0) return 1; /* cpu0 항상 online */
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/online", core);
    return sysfs_read_int(path);
}
