/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Internal Header
 *
 * 역할: 전력 관리 서비스 내부 공유 헤더 — 구조체, 상수, 유틸리티
 * 수행범위: 서브시스템 간 공유 타입·함수 선언
 * 의존방향: power.h, gio/gio.h, sysfs
 * SOLID: ISP — 내부 서브시스템에만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_POWER_INTERNAL_H
#define ZYL_POWER_INTERNAL_H

#include "power.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <gio/gio.h>

/* ─── 내부 상수 ─── */
#define BACKLIGHT_PATH       "/sys/class/backlight"
#define CPUFREQ_GOV_PATH     "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define MAX_WAKELOCKS        32
#define WAKELOCK_TIMEOUT_SEC 600

/* ─── hal_cpu.c 함수 선언 (HAL CPU 거버너 제어) ─── */
extern int zyl_cpu_set_governor(const char *governor);
extern int zyl_cpu_get_core_count(void);
extern int zyl_cpu_set_core_online(int core, int online);
extern int zyl_cpu_get_power_profile(void);
extern int zyl_cpu_set_power_profile(int profile);

/* ─── 내부 구조체 ─── */
struct ZylPowerService {
    ZylPowerState state;
    ZylPowerConfig config;
    int brightness;               /* 현재 밝기 0-100 */
    int saved_brightness;         /* screen_off 전 밝기 (복원용) */
    char *backlight_device;       /* sysfs backlight 장치 경로 */
    int max_brightness;           /* 장치 최대 밝기 값 */

    /* 웨이크락 */
    char *wakelocks[MAX_WAKELOCKS];
    int wakelock_count;
    /* 웨이크락 타임아웃 타이머 — 최대 600초 후 자동 release */
    guint wakelock_timers[MAX_WAKELOCKS];

    /* 타이머 */
    guint dim_timer_id;
    guint screen_off_timer_id;
    guint suspend_timer_id;

    /* 콜백 */
    zyl_power_state_fn state_cb;
    void *state_cb_data;
    zyl_wake_fn wake_cb;
    void *wake_cb_data;

    /* D-Bus */
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* ─── 웨이크락 타임아웃 컨텍스트 ─── */
typedef struct {
    ZylPowerService *svc;
    char             tag[128];
} WakelockTimeoutCtx;

/* ─── 공유 유틸리티: sysfs 읽기/쓰기 ─── */
int  sysfs_read_int(const char *path);
bool sysfs_write_int(const char *path, int val);
bool sysfs_write_str(const char *path, const char *str);

/* ─── 상태 전환 (여러 서브시스템에서 사용) ─── */
void transition_state(ZylPowerService *svc, ZylPowerState new_state);

/* ─── 백라이트 서브시스템 ─── */
char *detect_backlight_device(int *max_brightness);

/* ─── 디스플레이 서브시스템 ─── */
void reset_idle_timers(ZylPowerService *svc);

/* ─── CPU/Doze 서브시스템 ─── */
gboolean zyl_power_enter_doze(gpointer data);
void     exit_doze(ZylPowerService *svc);

#endif /* ZYL_POWER_INTERNAL_H */
