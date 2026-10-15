/* ----------------------------------------------------------
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 디스플레이 관리 서비스 -- DRM/KMS 모드, 스케일링, 회전 제어
 * 수행범위: 연결된 출력의 모드(Display HAL), 모드 선택, DPI 스케일, 회전,
 *           자동 회전(가속도계 D-Bus 시그널 구독), D-Bus 인터페이스
 * 의존방향: display.h, hal.h (Display HAL — DRM 출력·모드), gio/gio.h (D-Bus,
 *           회전은 컴포지터에 RotationChanged 로)
 * SOLID: SRP -- 디스플레이 제어만 담당
 * ---------------------------------------------------------- */

#include "display.h"
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <gio/gio.h>

/* --- Internal constants --- */
#define MAX_MODES            32
#define HYSTERESIS_NS        (500ULL * 1000000ULL) /* 500ms */

/* --- Internal service structure --- */
struct ZylDisplayService {
    /* Cached modes for primary connector */
    ZylDisplayMode modes[MAX_MODES];
    int mode_count;

    /* Current state */
    ZylDisplayMode current_mode;
    float          scale;
    ZylRotation    rotation;
    bool           auto_rotate;

    /* Auto-rotate hysteresis */
    ZylRotation    pending_rotation;
    uint64_t       pending_since_ns;

    /* D-Bus */
    GDBusConnection *dbus;
    guint            dbus_owner_id;
    guint            sensor_sub_id;

    /* GLib main loop (for daemon mode) */
    GMainLoop *loop;
};

/* --- Utility: current monotonic time in nanoseconds --- */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* --- Modes of the first connected output, as the Display HAL reads them --- */
static void load_modes(ZylDisplayService *svc) {
    ZylDisplayOutput outputs[4];
    int n = zyl_hal_display()->list_outputs(outputs, 4);
    for (int i = 0; i < n; i++) {
        if (!outputs[i].connected) continue;
        for (int k = 0; k < outputs[i].mode_count && svc->mode_count < MAX_MODES; k++) {
            ZylDisplayMode *m = &svc->modes[svc->mode_count++];
            m->width = outputs[i].modes[k].width;
            m->height = outputs[i].modes[k].height;
            m->refresh_hz = outputs[i].modes[k].refresh_hz;
            snprintf(m->connector, sizeof(m->connector), "%s", outputs[i].name);
        }
        break;
    }
    if (svc->mode_count > 0) {
        svc->current_mode = svc->modes[0];         /* the kernel lists the preferred mode first */
        g_message("[Display] Primary mode: %dx%d@%dHz on %s", svc->current_mode.width,
                  svc->current_mode.height, svc->current_mode.refresh_hz, svc->current_mode.connector);
    } else {
        g_message("[Display] No connected output");
    }
}

/* --- Determine rotation from accelerometer data (H7) --- */
static ZylRotation rotation_from_accel(double x, double y) {
    /*
     * Gravity vector interpretation (device held upright = portrait):
     *   Portrait  (0):   y < -5   (gravity pulling down)
     *   Landscape (90):  x < -5   (gravity pulling left)
     *   Inverted  (180): y >  5   (gravity pulling up)
     *   Landscape (270): x >  5   (gravity pulling right)
     *
     * Threshold of 5 m/s^2 (roughly half of 9.8) avoids false triggers.
     */
    const double threshold = 5.0;

    if (y < -threshold)      return ZYL_ROTATION_0;
    if (x < -threshold)      return ZYL_ROTATION_90;
    if (y >  threshold)      return ZYL_ROTATION_180;
    if (x >  threshold)      return ZYL_ROTATION_270;

    /* Ambiguous -- keep current */
    return ZYL_ROTATION_0;
}

/* --- Apply rotation to DRM connector --- */
static int apply_rotation(ZylDisplayService *svc, ZylRotation rot) {
    /* Rotation is a KMS plane property the compositor sets (there is no
       sysfs file for it): tell the compositor */
    if (svc->dbus) {
        g_dbus_connection_emit_signal(svc->dbus, NULL,
            ZYL_DISPLAY_DBUS_PATH, ZYL_DISPLAY_DBUS_NAME,
            "RotationChanged",
            g_variant_new("(i)", (gint32)rot),
            NULL);
        g_message("[Display] Rotation signal emitted: %d", (int)rot);
    }
    return 0;
}

/* --- Sensor D-Bus signal callback (H7 auto-rotate) --- */
static void on_sensor_event(GDBusConnection *conn,
                            const gchar *sender,
                            const gchar *object_path,
                            const gchar *interface_name,
                            const gchar *signal_name,
                            GVariant *parameters,
                            gpointer user_data) {
    (void)conn; (void)sender; (void)object_path;
    (void)interface_name; (void)signal_name;

    ZylDisplayService *svc = user_data;
    if (!svc->auto_rotate) return;

    /* SensorEvent(i type, d v0, d v1, d v2, t timestamp_ns) */
    gint32 type;
    gdouble v0, v1, v2;
    guint64 ts;
    g_variant_get(parameters, "(idddt)", &type, &v0, &v1, &v2, &ts);

    /* Only care about accelerometer (type 0) */
    if (type != 0) return;

    ZylRotation desired = rotation_from_accel(v0, v1);
    uint64_t now = now_ns();

    if (desired != svc->rotation) {
        if (desired == svc->pending_rotation) {
            /* Check hysteresis: require stable for HYSTERESIS_NS */
            if ((now - svc->pending_since_ns) >= HYSTERESIS_NS) {
                svc->rotation = desired;
                apply_rotation(svc, desired);
                g_message("[Display] Auto-rotate: %d -> %d",
                          (int)svc->pending_rotation, (int)desired);
            }
        } else {
            /* New candidate -- start hysteresis timer */
            svc->pending_rotation = desired;
            svc->pending_since_ns = now;
        }
    }
}

/* --- Subscribe to sensor service D-Bus signals --- */
static void subscribe_sensor_signals(ZylDisplayService *svc) {
    if (!svc->dbus) return;

    svc->sensor_sub_id = g_dbus_connection_signal_subscribe(
        svc->dbus,
        NULL,                          /* any sender */
        "org.zylos.SensorService",     /* interface */
        "SensorEvent",                 /* signal name */
        "/org/zylos/SensorService",    /* object path */
        NULL,                          /* arg0 match */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_sensor_event,
        svc,
        NULL);

    g_message("[Display] Subscribed to SensorService signals for auto-rotate");
}

/* ======================================================
 * D-Bus interface
 * ====================================================== */

static const char *display_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_DISPLAY_DBUS_NAME "'>"
    "    <method name='GetModes'>"
    "      <arg type='a(iiis)' name='modes' direction='out'/>"
    "    </method>"
    "    <method name='SetMode'>"
    "      <arg type='i' name='width'  direction='in'/>"
    "      <arg type='i' name='height' direction='in'/>"
    "      <arg type='i' name='hz'     direction='in'/>"
    "      <arg type='b' name='ok'     direction='out'/>"
    "    </method>"
    "    <method name='GetCurrentMode'>"
    "      <arg type='i' name='width'      direction='out'/>"
    "      <arg type='i' name='height'     direction='out'/>"
    "      <arg type='i' name='refresh_hz' direction='out'/>"
    "      <arg type='s' name='connector'  direction='out'/>"
    "    </method>"
    "    <method name='SetScale'>"
    "      <arg type='d' name='scale' direction='in'/>"
    "      <arg type='b' name='ok'    direction='out'/>"
    "    </method>"
    "    <method name='GetScale'>"
    "      <arg type='d' name='scale' direction='out'/>"
    "    </method>"
    "    <method name='SetRotation'>"
    "      <arg type='i' name='rotation' direction='in'/>"
    "      <arg type='b' name='ok'       direction='out'/>"
    "    </method>"
    "    <method name='GetRotation'>"
    "      <arg type='i' name='rotation' direction='out'/>"
    "    </method>"
    "    <method name='SetAutoRotate'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "    </method>"
    "    <signal name='RotationChanged'>"
    "      <arg type='i' name='rotation'/>"
    "    </signal>"
    "    <signal name='ModeChanged'>"
    "      <arg type='i' name='width'/>"
    "      <arg type='i' name='height'/>"
    "      <arg type='i' name='refresh_hz'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static void handle_display_method(GDBusConnection *conn,
                                  const gchar *sender,
                                  const gchar *path,
                                  const gchar *iface,
                                  const gchar *method,
                                  GVariant *params,
                                  GDBusMethodInvocation *inv,
                                  gpointer data) {
    ZylDisplayService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "GetModes") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiis)"));
        for (int i = 0; i < svc->mode_count; i++) {
            ZylDisplayMode *m = &svc->modes[i];
            g_variant_builder_add(&builder, "(iiis)",
                                  m->width, m->height,
                                  m->refresh_hz, m->connector);
        }
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(a(iiis))", &builder));

    } else if (g_strcmp0(method, "SetMode") == 0) {
        gint32 w, h, hz;
        g_variant_get(params, "(iii)", &w, &h, &hz);
        int ret = zyl_display_set_mode(svc, w, h, hz);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));

    } else if (g_strcmp0(method, "GetCurrentMode") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(iiis)",
                          svc->current_mode.width,
                          svc->current_mode.height,
                          svc->current_mode.refresh_hz,
                          svc->current_mode.connector));

    } else if (g_strcmp0(method, "SetScale") == 0) {
        gdouble s;
        g_variant_get(params, "(d)", &s);
        int ret = zyl_display_set_scale(svc, (float)s);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));

    } else if (g_strcmp0(method, "GetScale") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(d)", (gdouble)svc->scale));

    } else if (g_strcmp0(method, "SetRotation") == 0) {
        gint32 r;
        g_variant_get(params, "(i)", &r);
        int ret = zyl_display_set_rotation(svc, (ZylRotation)r);
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", ret == 0));

    } else if (g_strcmp0(method, "GetRotation") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(i)", (gint32)svc->rotation));

    } else if (g_strcmp0(method, "SetAutoRotate") == 0) {
        gboolean en;
        g_variant_get(params, "(b)", &en);
        zyl_display_set_auto_rotate(svc, en);
        g_dbus_method_invocation_return_value(inv, NULL);

    } else {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method: %s", method);
    }
}

static const GDBusInterfaceVTable display_vtable = {
    .method_call = handle_display_method,
};

static void on_display_bus_acquired(GDBusConnection *conn,
                                    const gchar *name,
                                    gpointer data) {
    ZylDisplayService *svc = data;
    svc->dbus = conn;
    (void)name;

    GDBusNodeInfo *info =
        g_dbus_node_info_new_for_xml(display_introspection_xml, NULL);
    if (!info || !info->interfaces || !info->interfaces[0]) {
        g_warning("[Display] Failed to parse introspection XML");
        return;
    }
    g_dbus_connection_register_object(conn, ZYL_DISPLAY_DBUS_PATH,
        info->interfaces[0], &display_vtable, svc, NULL, NULL);
    g_dbus_node_info_unref(info);

    /* Subscribe to sensor events for auto-rotate */
    subscribe_sensor_signals(svc);

    g_message("[Display] D-Bus registered: %s", ZYL_DISPLAY_DBUS_NAME);
}

/* ======================================================
 * Public API
 * ====================================================== */

ZylDisplayService *zyl_display_create(void) {
    ZylDisplayService *svc = calloc(1, sizeof(ZylDisplayService));
    if (!svc) return NULL;

    svc->scale    = 1.0f;
    svc->rotation = ZYL_ROTATION_0;
    svc->auto_rotate = true;
    svc->pending_rotation = ZYL_ROTATION_0;
    svc->pending_since_ns = 0;

    load_modes(svc);

    /* Register D-Bus name */
    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
        ZYL_DISPLAY_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_display_bus_acquired, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_display_destroy(ZylDisplayService *svc) {
    if (!svc) return;

    if (svc->dbus && svc->sensor_sub_id > 0) {
        g_dbus_connection_signal_unsubscribe(svc->dbus,
                                             svc->sensor_sub_id);
    }
    if (svc->dbus_owner_id > 0) {
        g_bus_unown_name(svc->dbus_owner_id);
    }
    free(svc);
}

int zyl_display_set_mode(ZylDisplayService *svc,
                         int width, int height, int hz) {
    if (!svc) return -1;

    /* Find matching mode */
    for (int i = 0; i < svc->mode_count; i++) {
        ZylDisplayMode *m = &svc->modes[i];
        if (m->width == width && m->height == height &&
            (hz <= 0 || m->refresh_hz == hz)) {
            svc->current_mode = *m;

            /* Emit D-Bus signal */
            if (svc->dbus) {
                g_dbus_connection_emit_signal(svc->dbus, NULL,
                    ZYL_DISPLAY_DBUS_PATH, ZYL_DISPLAY_DBUS_NAME,
                    "ModeChanged",
                    g_variant_new("(iii)", m->width, m->height,
                                  m->refresh_hz),
                    NULL);
            }

            g_message("[Display] Mode set: %dx%d@%dHz",
                      width, height, m->refresh_hz);
            return 0;
        }
    }

    g_warning("[Display] Requested mode %dx%d@%dHz not found",
              width, height, hz);
    return -1;
}

int zyl_display_set_scale(ZylDisplayService *svc, float scale) {
    if (!svc) return -1;
    if (scale < 0.5f || scale > 4.0f) {
        g_warning("[Display] Scale %.2f out of range [0.5, 4.0]", scale);
        return -1;
    }
    svc->scale = scale;
    g_message("[Display] Scale set: %.2f", scale);
    return 0;
}

int zyl_display_set_rotation(ZylDisplayService *svc, ZylRotation rot) {
    if (!svc) return -1;

    switch (rot) {
    case ZYL_ROTATION_0:
    case ZYL_ROTATION_90:
    case ZYL_ROTATION_180:
    case ZYL_ROTATION_270:
        break;
    default:
        g_warning("[Display] Invalid rotation value: %d", (int)rot);
        return -1;
    }

    svc->rotation = rot;
    return apply_rotation(svc, rot);
}

int zyl_display_set_auto_rotate(ZylDisplayService *svc, bool enabled) {
    if (!svc) return -1;
    svc->auto_rotate = enabled;
    g_message("[Display] Auto-rotate %s", enabled ? "enabled" : "disabled");
    return 0;
}

/* ======================================================
 * main(): standalone daemon entry point
 * ====================================================== */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    g_message("[Display] Zyl Display Management Service starting...");

    ZylDisplayService *svc = zyl_display_create();
    if (!svc) {
        g_critical("[Display] Failed to create display service");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    svc->loop = loop;
    g_message("[Display] Entering main loop");
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_display_destroy(svc);
    g_message("[Display] Service stopped");
    return 0;
}
