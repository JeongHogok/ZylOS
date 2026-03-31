#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 전력 관리 서비스 — 화면 제어, 서스펜드, CPU 거버너, 웨이크락
 * 수행범위: sysfs backlight, systemd-logind 연동, 타이머 기반 자동 절전
 * 의존방향: power.h, gio/gio.h, sysfs
 * SOLID: SRP — 전력 상태 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "power.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <gio/gio.h>

/* ─── hal_cpu.c 함수 선언 (HAL CPU 거버너 제어) ─── */
extern int zyl_cpu_set_governor(const char *governor);
extern int zyl_cpu_get_core_count(void);
extern int zyl_cpu_set_core_online(int core, int online);
extern int zyl_cpu_get_power_profile(void);
extern int zyl_cpu_set_power_profile(int profile);

/* ─── Forward declarations (함수 정의가 호출보다 뒤에 위치) ─── */
static gboolean zyl_power_enter_doze(gpointer data);
static void exit_doze(ZylPowerService *svc);

/* ─── 내부 상수 ─── */
#define BACKLIGHT_PATH      "/sys/class/backlight"
#define CPUFREQ_GOV_PATH    "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define MAX_WAKELOCKS       32

/* ─── 내부 구조체 ─── */
struct ZylPowerService {
    ZylPowerState state;
    ZylPowerConfig config;
    int brightness;               /* 현재 밝기 0-100 */
    char *backlight_device;       /* sysfs backlight 장치 경로 */
    int max_brightness;           /* 장치 최대 밝기 값 */

    /* 웨이크락 */
    char *wakelocks[MAX_WAKELOCKS];
    int wakelock_count;
    /* #6: 웨이크락 타임아웃 타이머 — 최대 600초 후 자동 release */
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

/* ─── 유틸리티: sysfs 읽기/쓰기 ─── */
static int sysfs_read_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

static bool sysfs_write_int(const char *path, int val) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%d", val);
    fclose(f);
    return true;
}

static bool sysfs_write_str(const char *path, const char *str) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs(str, f);
    fclose(f);
    return true;
}

/* ─── logind D-Bus 호출 ─── */
static bool logind_call(const char *method) {
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!bus) {
        g_warning("[Power] Cannot connect to system bus: %s", error->message);
        g_error_free(error);
        return false;
    }
    GVariant *result = g_dbus_connection_call_sync(bus,
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        method,
        g_variant_new("(b)", TRUE),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, &error);
    if (!result) {
        g_warning("[Power] logind %s failed: %s", method, error->message);
        g_error_free(error);
        g_object_unref(bus);
        return false;
    }
    g_variant_unref(result);
    g_object_unref(bus);
    return true;
}

/* ─── backlight 장치 자동 감지 ─── */
static char *detect_backlight_device(int *max_brightness) {
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

/* ─── 상태 전환 (내부) ─── */
static void transition_state(ZylPowerService *svc, ZylPowerState new_state) {
    if (svc->state == new_state) return;
    ZylPowerState old = svc->state;
    svc->state = new_state;

    g_message("[Power] %d → %d", old, new_state);

    if (svc->state_cb) {
        svc->state_cb(old, new_state, svc->state_cb_data);
    }

    /* D-Bus 시그널: StateChanged */
    if (svc->dbus) {
        g_dbus_connection_emit_signal(svc->dbus, NULL,
            ZYL_POWER_DBUS_PATH,
            ZYL_POWER_DBUS_NAME,
            "StateChanged",
            g_variant_new("(ii)", (int)old, (int)new_state),
            NULL);
    }
}

/* ─── 타이머 콜백: 화면 어둡게 ─── */
static gboolean on_dim_timeout(gpointer data) {
    ZylPowerService *svc = data;
    svc->dim_timer_id = 0;

    if (svc->state != ZYL_POWER_STATE_ACTIVE) return G_SOURCE_REMOVE;
    if (svc->wakelock_count > 0) return G_SOURCE_REMOVE;

    /* 밝기를 현재의 30%로 낮춤 */
    int dim_brightness = svc->brightness * 30 / 100;
    if (dim_brightness < 5) dim_brightness = 5;
    zyl_power_set_brightness(svc, dim_brightness);
    transition_state(svc, ZYL_POWER_STATE_DIM);

    /* screen_off 타이머 시작 */
    int remain = svc->config.screen_timeout_sec - svc->config.dim_timeout_sec;
    if (remain > 0) {
        svc->screen_off_timer_id = g_timeout_add_seconds(remain,
            (GSourceFunc)zyl_power_request_screen_off, svc);
    }

    return G_SOURCE_REMOVE;
}

/* ─── 타이머 리셋 (사용자 활동 시 호출) ─── */
static void reset_idle_timers(ZylPowerService *svc) {
    /* 기존 타이머 제거 */
    if (svc->dim_timer_id) { g_source_remove(svc->dim_timer_id); svc->dim_timer_id = 0; }
    if (svc->screen_off_timer_id) { g_source_remove(svc->screen_off_timer_id); svc->screen_off_timer_id = 0; }
    if (svc->suspend_timer_id) { g_source_remove(svc->suspend_timer_id); svc->suspend_timer_id = 0; }

    if (svc->config.screen_timeout_sec <= 0) return;

    /* dim 타이머 시작 */
    int dim_sec = svc->config.dim_timeout_sec;
    if (dim_sec <= 0) dim_sec = svc->config.screen_timeout_sec - 5;
    if (dim_sec < 5) dim_sec = 5;

    svc->dim_timer_id = g_timeout_add_seconds(dim_sec, on_dim_timeout, svc);
}

/* ─── D-Bus 인트로스펙션 ─── */
static const char *power_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_POWER_DBUS_NAME "'>"
    "    <method name='ScreenOff'/>"
    "    <method name='ScreenOn'/>"
    "    <method name='Suspend'/>"
    "    <method name='Shutdown'/>"
    "    <method name='Reboot'/>"
    "    <method name='SetBrightness'>"
    "      <arg type='i' name='percent' direction='in'/>"
    "    </method>"
    "    <method name='GetState'>"
    "      <arg type='i' name='state' direction='out'/>"
    "    </method>"
    "    <method name='AcquireWakelock'>"
    "      <arg type='s' name='tag' direction='in'/>"
    "    </method>"
    "    <method name='ReleaseWakelock'>"
    "      <arg type='s' name='tag' direction='in'/>"
    "    </method>"
    "    <signal name='StateChanged'>"
    "      <arg type='i' name='old_state'/>"
    "      <arg type='i' name='new_state'/>"
    "    </signal>"
    "    <signal name='WakelockExpired'>"
    "      <arg type='s' name='tag'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ─── D-Bus 메서드 핸들러 ─── */
static void handle_power_method(GDBusConnection *conn, const gchar *sender,
                                 const gchar *path, const gchar *iface,
                                 const gchar *method, GVariant *params,
                                 GDBusMethodInvocation *inv, gpointer data) {
    ZylPowerService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "ScreenOff") == 0) {
        zyl_power_request_screen_off(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "ScreenOn") == 0) {
        zyl_power_request_screen_on(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Suspend") == 0) {
        zyl_power_request_suspend(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Shutdown") == 0) {
        zyl_power_request_shutdown(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Reboot") == 0) {
        zyl_power_request_reboot(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "SetBrightness") == 0) {
        gint32 pct;
        g_variant_get(params, "(i)", &pct);
        zyl_power_set_brightness(svc, pct);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetState") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(i)", (int)svc->state));
    } else if (g_strcmp0(method, "AcquireWakelock") == 0) {
        const gchar *tag;
        g_variant_get(params, "(&s)", &tag);
        zyl_power_acquire_wakelock(svc, tag);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "ReleaseWakelock") == 0) {
        const gchar *tag;
        g_variant_get(params, "(&s)", &tag);
        zyl_power_release_wakelock(svc, tag);
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

static const GDBusInterfaceVTable power_vtable = {
    .method_call = handle_power_method,
};

static void on_power_bus_acquired(GDBusConnection *conn, const gchar *name,
                                   gpointer data) {
    ZylPowerService *svc = data;
    svc->dbus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(power_introspection_xml, NULL);
    g_dbus_connection_register_object(conn, ZYL_POWER_DBUS_PATH,
        info->interfaces[0], &power_vtable, svc, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_message("[Power] D-Bus registered: %s", ZYL_POWER_DBUS_NAME);
}

/* ─── #6: 웨이크락 타임아웃 타이머 콜백 ─── */
#define WAKELOCK_TIMEOUT_SEC 600   /* 최대 10분 */

typedef struct {
    ZylPowerService *svc;
    char             tag[128];
} WakelockTimeoutCtx;

static gboolean on_wakelock_timeout(gpointer data) {
    WakelockTimeoutCtx *ctx = data;
    ZylPowerService    *svc = ctx->svc;
    const char         *tag = ctx->tag;

    g_warning("[Power] Wakelock timeout: %s — force releasing", tag);

    /* 타이머 슬롯 초기화 (이미 만료됐으므로 제거 불필요) */
    for (int i = 0; i < svc->wakelock_count; i++) {
        if (svc->wakelocks[i] && strcmp(svc->wakelocks[i], tag) == 0) {
            svc->wakelock_timers[i] = 0;
            break;
        }
    }

    /* D-Bus 시그널: WakelockExpired */
    if (svc->dbus) {
        g_dbus_connection_emit_signal(svc->dbus, NULL,
            ZYL_POWER_DBUS_PATH,
            ZYL_POWER_DBUS_NAME,
            "WakelockExpired",
            g_variant_new("(s)", tag),
            NULL);
    }

    /* 웨이크락 강제 해제 */
    zyl_power_release_wakelock(svc, tag);

    g_free(ctx);
    return G_SOURCE_REMOVE;
}

/* ─── 공개 API ─── */

ZylPowerService *zyl_power_create(void) {
    ZylPowerService *svc = g_new0(ZylPowerService, 1);
    svc->state = ZYL_POWER_STATE_ACTIVE;
    svc->brightness = 80;
    /* wakelock_timers는 g_new0으로 0 초기화됨 */

    /* 기본 설정 */
    svc->config.screen_timeout_sec = 30;
    svc->config.dim_timeout_sec = 25;
    svc->config.auto_suspend = true;
    svc->config.suspend_delay_sec = 60;
    svc->config.cpu_governor = ZYL_CPU_GOV_SCHEDUTIL;
    svc->config.doze_enabled = true;
    svc->config.doze_interval_min = 15;

    /* backlight 장치 감지 */
    svc->backlight_device = detect_backlight_device(&svc->max_brightness);
    if (svc->backlight_device) {
        g_message("[Power] Backlight: %s (max=%d)", svc->backlight_device, svc->max_brightness);
    } else {
        g_message("[Power] No backlight device found — software control only");
    }

    /* CPU 거버너 설정 */
    const char *gov_str = "schedutil";
    switch (svc->config.cpu_governor) {
        case ZYL_CPU_GOV_PERFORMANCE: gov_str = "performance"; break;
        case ZYL_CPU_GOV_POWERSAVE:   gov_str = "powersave"; break;
        case ZYL_CPU_GOV_ONDEMAND:    gov_str = "ondemand"; break;
        case ZYL_CPU_GOV_SCHEDUTIL:   gov_str = "schedutil"; break;
    }
    if (!sysfs_write_str(CPUFREQ_GOV_PATH, gov_str)) {
        g_message("[Power] Cannot set CPU governor (may need root)");
    }

    /* D-Bus 등록 */
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_POWER_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_power_bus_acquired, NULL, NULL, svc, NULL);

    /* idle 타이머 시작 */
    reset_idle_timers(svc);

    return svc;
}

void zyl_power_destroy(ZylPowerService *svc) {
    if (!svc) return;

    if (svc->dim_timer_id) g_source_remove(svc->dim_timer_id);
    if (svc->screen_off_timer_id) g_source_remove(svc->screen_off_timer_id);
    if (svc->suspend_timer_id) g_source_remove(svc->suspend_timer_id);

    for (int i = 0; i < svc->wakelock_count; i++) {
        if (svc->wakelock_timers[i]) {
            g_source_remove(svc->wakelock_timers[i]);
            svc->wakelock_timers[i] = 0;
        }
        g_free(svc->wakelocks[i]);
    }

    g_bus_unown_name(svc->dbus_owner_id);
    g_free(svc->backlight_device);
    g_free(svc);
}

ZylPowerState zyl_power_get_state(const ZylPowerService *svc) {
    return svc ? svc->state : ZYL_POWER_STATE_ACTIVE;
}

int zyl_power_request_screen_off(ZylPowerService *svc) {
    if (!svc) return -1;
    zyl_power_set_brightness(svc, 0);
    transition_state(svc, ZYL_POWER_STATE_SCREEN_OFF);

    /* Doze → Suspend 단계적 전력 절감 */
    if (svc->wakelock_count == 0) {
        if (svc->config.doze_enabled) {
            /* Screen off → Doze 전환 (5분 후) */
            svc->suspend_timer_id = g_timeout_add_seconds(
                300, /* 5 minutes to doze */
                (GSourceFunc)zyl_power_enter_doze, svc);
        } else if (svc->config.auto_suspend) {
            svc->suspend_timer_id = g_timeout_add_seconds(
                svc->config.suspend_delay_sec,
                (GSourceFunc)zyl_power_request_suspend, svc);
        }
    }
    return 0;
}

int zyl_power_request_screen_on(ZylPowerService *svc) {
    if (!svc) return -1;
    /* Exit doze if in doze state */
    if (svc->state == ZYL_POWER_STATE_DOZE) exit_doze(svc);
    zyl_power_set_brightness(svc, svc->brightness > 0 ? svc->brightness : 80);
    transition_state(svc, ZYL_POWER_STATE_ACTIVE);
    reset_idle_timers(svc);
    return 0;
}

int zyl_power_request_suspend(ZylPowerService *svc) {
    if (!svc) return -1;
    if (svc->wakelock_count > 0) {
        g_message("[Power] Suspend blocked by %d wakelock(s)", svc->wakelock_count);
        return -1;
    }

    transition_state(svc, ZYL_POWER_STATE_SUSPEND);

    /*
     * 실제 서스펜드:
     *   echo mem > /sys/power/state
     * 또는 systemd-logind:
     *   dbus-send --system --dest=org.freedesktop.login1
     *     /org/freedesktop/login1
     *     org.freedesktop.login1.Manager.Suspend
     *     boolean:true
     */
    if (!sysfs_write_str("/sys/power/state", "mem")) {
        g_warning("[Power] Failed to suspend via sysfs — trying logind");
        logind_call("Suspend");
    }

    /* 리쥼 후 여기로 돌아옴 */
    transition_state(svc, ZYL_POWER_STATE_ACTIVE);
    if (svc->wake_cb) {
        svc->wake_cb(ZYL_WAKE_POWER_BUTTON, svc->wake_cb_data);
    }
    return 0;
}

int zyl_power_request_shutdown(ZylPowerService *svc) {
    if (!svc) return -1;
    transition_state(svc, ZYL_POWER_STATE_SHUTDOWN);
    g_message("[Power] Shutting down...");

    sync();
    logind_call("PowerOff");
    return 0;
}

int zyl_power_request_reboot(ZylPowerService *svc) {
    if (!svc) return -1;
    transition_state(svc, ZYL_POWER_STATE_SHUTDOWN);
    g_message("[Power] Rebooting...");

    sync();
    logind_call("Reboot");
    return 0;
}

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

int zyl_power_get_brightness(const ZylPowerService *svc) {
    return svc ? svc->brightness : -1;
}

int zyl_power_set_config(ZylPowerService *svc, const ZylPowerConfig *cfg) {
    if (!svc || !cfg) return -1;
    svc->config = *cfg;
    reset_idle_timers(svc);
    return 0;
}

void zyl_power_get_config(const ZylPowerService *svc, ZylPowerConfig *out) {
    if (!svc || !out) return;
    *out = svc->config;
}

int zyl_power_acquire_wakelock(ZylPowerService *svc, const char *tag) {
    if (!svc || !tag || svc->wakelock_count >= MAX_WAKELOCKS) return -1;

    /* 중복 체크 */
    for (int i = 0; i < svc->wakelock_count; i++) {
        if (strcmp(svc->wakelocks[i], tag) == 0) return 0;
    }

    int slot = svc->wakelock_count;
    svc->wakelocks[slot] = g_strdup(tag);

    /* #6: 웨이크락 최대 타임아웃 타이머 — 600초 후 자동 해제 + WakelockExpired 시그널 */
    WakelockTimeoutCtx *ctx = g_new(WakelockTimeoutCtx, 1);
    ctx->svc = svc;
    g_strlcpy(ctx->tag, tag, sizeof(ctx->tag));
    svc->wakelock_timers[slot] = g_timeout_add_seconds(WAKELOCK_TIMEOUT_SEC,
        on_wakelock_timeout, ctx);

    svc->wakelock_count++;
    g_message("[Power] Wakelock acquired: %s (total: %d, timeout: %ds)",
              tag, svc->wakelock_count, WAKELOCK_TIMEOUT_SEC);

    /* 서스펜드 타이머 취소 */
    if (svc->suspend_timer_id) {
        g_source_remove(svc->suspend_timer_id);
        svc->suspend_timer_id = 0;
    }

    return 0;
}

void zyl_power_release_wakelock(ZylPowerService *svc, const char *tag) {
    if (!svc || !tag) return;

    for (int i = 0; i < svc->wakelock_count; i++) {
        if (strcmp(svc->wakelocks[i], tag) == 0) {
            /* #6: 타임아웃 타이머 취소 (정상 해제 시) */
            if (svc->wakelock_timers[i]) {
                g_source_remove(svc->wakelock_timers[i]);
                svc->wakelock_timers[i] = 0;
            }
            g_free(svc->wakelocks[i]);
            /* 마지막 요소로 교체 */
            int last = --svc->wakelock_count;
            svc->wakelocks[i]      = svc->wakelocks[last];
            svc->wakelock_timers[i] = svc->wakelock_timers[last];
            svc->wakelocks[last]      = NULL;
            svc->wakelock_timers[last] = 0;
            g_message("[Power] Wakelock released: %s (remaining: %d)", tag, svc->wakelock_count);
            return;
        }
    }
}

int zyl_power_get_wakelock_count(const ZylPowerService *svc) {
    return svc ? svc->wakelock_count : 0;
}

void zyl_power_on_state_change(ZylPowerService *svc,
                                zyl_power_state_fn cb, void *data) {
    if (!svc) return;
    svc->state_cb = cb;
    svc->state_cb_data = data;
}

void zyl_power_on_wake(ZylPowerService *svc, zyl_wake_fn cb, void *data) {
    if (!svc) return;
    svc->wake_cb = cb;
    svc->wake_cb_data = data;
}

/* ─── Doze 모드 ─── */

static gboolean zyl_power_enter_doze(gpointer data) {
    ZylPowerService *svc = data;
    if (!svc || svc->state != ZYL_POWER_STATE_SCREEN_OFF) return G_SOURCE_REMOVE;
    if (svc->wakelock_count > 0) return G_SOURCE_REMOVE; /* 웨이크락 보유 → doze 안 함 */

    transition_state(svc, ZYL_POWER_STATE_DOZE);
    g_message("[Power] Entered DOZE mode — network restricted, alarms only");

    /* CPU 절전 거버너 전환 */
    zyl_cpu_set_governor("powersave");

    /* 비활성 코어 오프라인 (코어 4~7) */
    int ncores = zyl_cpu_get_core_count();
    for (int i = ncores / 2; i < ncores; i++) {
        zyl_cpu_set_core_online(i, 0);
    }

    /* Doze → Deep Sleep 전환 타이머 (30분) */
    if (svc->config.auto_suspend) {
        svc->suspend_timer_id = g_timeout_add_seconds(
            1800, /* 30 minutes in doze → suspend */
            (GSourceFunc)zyl_power_request_suspend, svc);
    }

    return G_SOURCE_REMOVE;
}

/* Doze 해제 — 화면 켜질 때 호출 */
static void exit_doze(ZylPowerService *svc) {
    if (!svc || svc->state != ZYL_POWER_STATE_DOZE) return;

    /* 코어 다시 온라인 */
    int ncores = zyl_cpu_get_core_count();
    for (int i = 1; i < ncores; i++) {
        zyl_cpu_set_core_online(i, 1);
    }

    /* 거버너 복원 */
    int profile = zyl_cpu_get_power_profile();
    zyl_cpu_set_power_profile(profile);

    g_message("[Power] Exited DOZE mode — all cores online");
}

/* ─── 데몬 진입점 ─── */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ZylPowerService *svc = zyl_power_create();
    if (!svc) {
        g_critical("[Power] Failed to create service");
        return 1;
    }

    g_message("[Power] Zyl OS Power Manager started");

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_power_destroy(svc);
    return 0;
}
