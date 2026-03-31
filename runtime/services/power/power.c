#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 전력 정책 매니저 — D-Bus, 생성/해제, 서스펜드/종료/재부팅
 * 수행범위: D-Bus 인터페이스, 서비스 생명주기, logind 연동, 정책 설정
 * 의존방향: power_internal.h, gio/gio.h, sysfs, systemd-logind
 * SOLID: SRP — 전력 정책 조율 및 D-Bus 인터페이스만 담당
 * ────────────────────────────────────────────────────────── */

#include "power_internal.h"

/* ─── 유틸리티: sysfs 읽기/쓰기 ─── */
int sysfs_read_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

bool sysfs_write_int(const char *path, int val) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%d", val);
    fclose(f);
    return true;
}

bool sysfs_write_str(const char *path, const char *str) {
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

/* ─── 공개 API: 생성 ─── */

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

/* ─── 공개 API: 해제 ─── */

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

/* ─── 공개 API: 상태 조회 ─── */

ZylPowerState zyl_power_get_state(const ZylPowerService *svc) {
    return svc ? svc->state : ZYL_POWER_STATE_ACTIVE;
}

/* ─── 공개 API: 서스펜드 ─── */

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

/* ─── 공개 API: 종료 ─── */

int zyl_power_request_shutdown(ZylPowerService *svc) {
    if (!svc) return -1;
    transition_state(svc, ZYL_POWER_STATE_SHUTDOWN);
    g_message("[Power] Shutting down...");

    sync();
    logind_call("PowerOff");
    return 0;
}

/* ─── 공개 API: 재부팅 ─── */

int zyl_power_request_reboot(ZylPowerService *svc) {
    if (!svc) return -1;
    transition_state(svc, ZYL_POWER_STATE_SHUTDOWN);
    g_message("[Power] Rebooting...");

    sync();
    logind_call("Reboot");
    return 0;
}

/* ─── 공개 API: 설정 ─── */

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

/* ─── 공개 API: 콜백 등록 ─── */

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
