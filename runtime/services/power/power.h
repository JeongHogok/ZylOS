/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 전력 관리 서비스 인터페이스 — 절전, 화면 끄기, 웨이크업
 * 수행범위: 화면 타임아웃, 서스펜드/리쥼, CPU 거버너, 배터리 감시
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 전력 관리 관련 인터페이스만 노출
 *
 * 실기기: systemd-logind + sysfs backlight + cpufreq
 * 에뮬레이터: JS로 시뮬레이션
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_POWER_H
#define ZYL_POWER_H

#include <stdbool.h>
#include <stdint.h>

/* ─── 전력 상태 ─── */
typedef enum {
    ZYL_POWER_STATE_ACTIVE,       /* 화면 켜짐, 정상 동작 */
    ZYL_POWER_STATE_DIM,          /* 화면 어두워짐 (타임아웃 전) */
    ZYL_POWER_STATE_SCREEN_OFF,   /* 화면 꺼짐, CPU 활성 */
    ZYL_POWER_STATE_DOZE,         /* 도즈 모드 (주기적 깨어남) */
    ZYL_POWER_STATE_SUSPEND,      /* 전체 서스펜드 (S3) */
    ZYL_POWER_STATE_SHUTDOWN,     /* 종료 중 */
} ZylPowerState;

/* ─── CPU 거버너 ─── */
typedef enum {
    ZYL_CPU_GOV_PERFORMANCE,
    ZYL_CPU_GOV_POWERSAVE,
    ZYL_CPU_GOV_ONDEMAND,
    ZYL_CPU_GOV_SCHEDUTIL,        /* Linux 기본 권장 */
} ZylCpuGovernor;

/* ─── 전력 관리 설정 ─── */
typedef struct {
    int screen_timeout_sec;       /* 화면 자동 꺼짐 (0=never) */
    int dim_timeout_sec;          /* 화면 어두워짐 (screen_timeout 전) */
    bool auto_suspend;            /* 자동 서스펜드 활성화 */
    int suspend_delay_sec;        /* 화면 꺼짐 후 서스펜드까지 대기 */
    ZylCpuGovernor cpu_governor;  /* CPU 절전 정책 */
    bool doze_enabled;            /* 도즈 모드 활성화 */
    int doze_interval_min;        /* 도즈 깨어남 주기 (분) */
} ZylPowerConfig;

/* ─── 서비스 인터페이스 ─── */
typedef struct ZylPowerService ZylPowerService;

/* 서비스 생성/해제 */
ZylPowerService *zyl_power_create(void);
void             zyl_power_destroy(ZylPowerService *svc);

/* 전환 요청 */
int zyl_power_request_screen_off(ZylPowerService *svc);
int zyl_power_request_screen_on(ZylPowerService *svc);
/* The user touched the screen or pressed a key (the compositor says so): an
   awake screen stays awake, a dimmed one comes back; a dark one stays dark */
int zyl_power_user_activity(ZylPowerService *svc);
int zyl_power_request_suspend(ZylPowerService *svc);
int zyl_power_request_shutdown(ZylPowerService *svc);
int zyl_power_request_reboot(ZylPowerService *svc);

/* 화면 밝기 제어 */
int zyl_power_set_brightness(ZylPowerService *svc, int percent);

/* 웨이크락 (앱이 화면 꺼짐을 방지) */
int  zyl_power_acquire_wakelock(ZylPowerService *svc, const char *tag);
void zyl_power_release_wakelock(ZylPowerService *svc, const char *tag);

/* D-Bus 상수 */
#define ZYL_POWER_DBUS_NAME "org.zylos.PowerManager"
#define ZYL_POWER_DBUS_PATH "/org/zylos/PowerManager"

#endif /* ZYL_POWER_H */
