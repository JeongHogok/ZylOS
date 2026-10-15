#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 전력 정책 매니저 — D-Bus, 생성/해제, 서스펜드/종료/재부팅
 * 수행범위: D-Bus 인터페이스, 서비스 생명주기, logind 연동, 정책 설정
 * 의존방향: power_internal.h, gio/gio.h, hal.h (Display·CPU·Battery HAL), systemd-logind
 * SOLID: SRP — 전력 정책 조율 및 D-Bus 인터페이스만 담당
 * ────────────────────────────────────────────────────────── */

#include "power_internal.h"
#include <glib-unix.h>


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
    "    <method name='GetCapabilities'>"
    "      <arg type='a{sv}' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='SetBrightness'>"
    "      <arg type='i' name='percent' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <method name='GetState'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
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

/* Same fields as the emulator's power service. A board without a battery
   reports batteryPresent false — never an invented level */
static const char *state_name(ZylPowerState s) {
    switch (s) {
    case ZYL_POWER_STATE_DIM:        return "DIM";
    case ZYL_POWER_STATE_SCREEN_OFF: return "SCREEN_OFF";
    case ZYL_POWER_STATE_DOZE:       return "DOZE";
    case ZYL_POWER_STATE_SUSPEND:    return "SUSPEND";
    case ZYL_POWER_STATE_SHUTDOWN:   return "SHUTDOWN";
    default:                         return "ACTIVE";
    }
}

static GVariant *state_variant(const ZylPowerService *svc) {
    ZylBatteryState bat;
    zyl_hal_battery()->get(&bat);
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "state", g_variant_new_string(state_name(svc->state)));
    g_variant_builder_add(&d, "{sv}", "screenOn", g_variant_new_boolean(
        svc->state == ZYL_POWER_STATE_ACTIVE || svc->state == ZYL_POWER_STATE_DIM));
    g_variant_builder_add(&d, "{sv}", "brightness", g_variant_new_int32(svc->brightness));
    g_variant_builder_add(&d, "{sv}", "batteryPresent", g_variant_new_boolean(bat.present));
    if (bat.present && bat.level_percent >= 0)
        g_variant_builder_add(&d, "{sv}", "batteryLevel", g_variant_new_int32(bat.level_percent));
    g_variant_builder_add(&d, "{sv}", "charging", g_variant_new_boolean(bat.charging));
    g_variant_builder_add(&d, "{sv}", "plugged", g_variant_new_boolean(bat.plugged));
    return g_variant_new("(a{sv})", &d);
}

/* The brightness the user last chose (Settings keeps it as display.brightness) */
static void on_saved_brightness(GObject *src, GAsyncResult *res, gpointer data) {
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    if (!r) return;
    GVariant *d = g_variant_get_child_value(r, 0);
    gint64 level = 0;
    GVariant *v = g_variant_lookup_value(d, "brightness", NULL);
    if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_INT64)) level = g_variant_get_int64(v);
    else if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_INT32)) level = g_variant_get_int32(v);
    else if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_DOUBLE)) level = (gint64)g_variant_get_double(v);
    if (level > 0) zyl_power_set_brightness(data, (int)level);
    if (v) g_variant_unref(v);
    g_variant_unref(d);
    g_variant_unref(r);
}

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
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)",
            zyl_hal_display()->backlight_present()));
    } else if (g_strcmp0(method, "GetCapabilities") == 0) {
        ZylBatteryState bat;
        zyl_hal_battery()->get(&bat);
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "backlight",
                              g_variant_new_boolean(zyl_hal_display()->backlight_present()));
        g_variant_builder_add(&d, "{sv}", "battery", g_variant_new_boolean(bat.present));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "GetState") == 0) {
        g_dbus_method_invocation_return_value(inv, state_variant(svc));
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
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_POWER_DBUS_PATH,
            info->interfaces[0], &power_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Power] D-Bus registered: %s", ZYL_POWER_DBUS_NAME);
    g_dbus_connection_call(conn, "org.zylos.Settings", "/org/zylos/Settings", "org.zylos.Settings",
        "Get", g_variant_new("(s)", "display"), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, on_saved_brightness, svc);
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

    /* Start from what the backlight shows; Settings' saved level follows on the bus */
    int shown = zyl_hal_display()->get_brightness();
    if (shown > 0) svc->brightness = shown;
    if (!zyl_hal_display()->backlight_present())
        g_message("[Power] No backlight on this device — brightness has nothing to set");

    /* CPU 거버너 설정 */
    const char *gov_str = "schedutil";
    switch (svc->config.cpu_governor) {
        case ZYL_CPU_GOV_PERFORMANCE: gov_str = "performance"; break;
        case ZYL_CPU_GOV_POWERSAVE:   gov_str = "powersave"; break;
        case ZYL_CPU_GOV_ONDEMAND:    gov_str = "ondemand"; break;
        case ZYL_CPU_GOV_SCHEDUTIL:   gov_str = "schedutil"; break;
    }
    if (!zyl_hal_cpu()->set_governor(gov_str)) {
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
    g_free(svc);
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
    /* logind holds the inhibitors and runs the sleep hooks */
    logind_call("Suspend");

    /* 리쥼 후 여기로 돌아옴 */
    transition_state(svc, ZYL_POWER_STATE_ACTIVE);
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

/* ─── 데몬 진입점 ─── */

static GMainLoop *g_power_loop = NULL;

static gboolean on_signal_power(gpointer data) {
    (void)data;
    g_message("[Power] Signal received, shutting down");
    if (g_power_loop) g_main_loop_quit(g_power_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ZylPowerService *svc = zyl_power_create();
    if (!svc) {
        g_critical("[Power] Failed to create service");
        return 1;
    }

    g_message("[Power] Zyl OS Power Manager started");

    g_power_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_power, NULL);
    g_unix_signal_add(SIGINT,  on_signal_power, NULL);
    g_main_loop_run(g_power_loop);

    g_main_loop_unref(g_power_loop);
    zyl_power_destroy(svc);
    return 0;
}
