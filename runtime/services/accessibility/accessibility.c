/* ----------------------------------------------------------
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 접근성 서비스 구현 — D-Bus로 고대비/폰트스케일/스크린리더 관리
 * 수행범위: D-Bus 메서드 핸들링, 상태 관리, 시그널 발송
 * 의존방향: accessibility.h, gio/gio.h
 * SOLID: SRP — 접근성 제어 로직만 담당
 * ---------------------------------------------------------- */

#include "accessibility.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/* -- D-Bus Introspection XML -------------------------------- */

static const char *introspection_xml =
    "<node>"
    "  <interface name='" ZYL_ACCESSIBILITY_DBUS_IFACE "'>"
    "    <method name='SetHighContrast'>"
    "      <arg direction='in'  type='b' name='enabled'/>"
    "    </method>"
    "    <method name='SetFontScale'>"
    "      <arg direction='in'  type='d' name='scale'/>"
    "      <arg direction='out' type='b' name='success'/>"
    "    </method>"
    "    <method name='GetState'>"
    "      <arg direction='out' type='b' name='high_contrast'/>"
    "      <arg direction='out' type='d' name='font_scale'/>"
    "      <arg direction='out' type='b' name='screen_reader_active'/>"
    "    </method>"
    "    <signal name='StateChanged'>"
    "      <arg type='b' name='high_contrast'/>"
    "      <arg type='d' name='font_scale'/>"
    "      <arg type='b' name='screen_reader_active'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* -- Internal Service Structure ----------------------------- */

struct _ZylAccessibilityService {
    ZylAccessibilityState  state;
    GDBusConnection       *connection;
    GDBusNodeInfo         *introspection_data;
    guint                  bus_owner_id;
    guint                  registration_id;
};

/* -- Emit StateChanged signal ------------------------------- */

static void emit_state_changed(ZylAccessibilityService *svc)
{
    if (!svc->connection) {
        return;
    }

    GError *error = NULL;
    g_dbus_connection_emit_signal(
        svc->connection,
        NULL,
        ZYL_ACCESSIBILITY_DBUS_PATH,
        ZYL_ACCESSIBILITY_DBUS_IFACE,
        "StateChanged",
        g_variant_new("(bdb)",
                       svc->state.high_contrast,
                       svc->state.font_scale,
                       svc->state.screen_reader_active),
        &error);

    if (error) {
        g_warning("Accessibility: failed to emit StateChanged: %s",
                  error->message);
        g_error_free(error);
    }
}

/* -- D-Bus method handler ----------------------------------- */

static void handle_method_call(GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    ZylAccessibilityService *svc = user_data;

    if (g_strcmp0(method_name, "SetHighContrast") == 0) {
        gboolean enabled = FALSE;
        g_variant_get(parameters, "(b)", &enabled);
        svc->state.high_contrast = enabled;
        g_message("Accessibility: high_contrast=%s",
                  enabled ? "true" : "false");
        emit_state_changed(svc);
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "SetFontScale") == 0) {
        gdouble scale = 1.0;
        g_variant_get(parameters, "(d)", &scale);

        gboolean success = TRUE;
        if (scale < ZYL_FONT_SCALE_MIN || scale > ZYL_FONT_SCALE_MAX) {
            success = FALSE;
            g_warning("Accessibility: font scale %.2f out of range [%.1f, %.1f]",
                      scale, ZYL_FONT_SCALE_MIN, ZYL_FONT_SCALE_MAX);
        } else {
            svc->state.font_scale = scale;
            g_message("Accessibility: font_scale=%.2f", scale);
            emit_state_changed(svc);
        }
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(b)", success));

    } else if (g_strcmp0(method_name, "GetState") == 0) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(bdb)",
                           svc->state.high_contrast,
                           svc->state.font_scale,
                           svc->state.screen_reader_active));

    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.zylos.Error.UnknownMethod", "Unknown method");
    }
}

/* -- D-Bus vtable ------------------------------------------- */

static const GDBusInterfaceVTable vtable = {
    .method_call  = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

/* -- Bus acquired callback ---------------------------------- */

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data)
{
    (void)name;
    ZylAccessibilityService *svc = user_data;
    svc->connection = connection;

    GError *error = NULL;
    svc->registration_id = g_dbus_connection_register_object(
        connection,
        ZYL_ACCESSIBILITY_DBUS_PATH,
        svc->introspection_data->interfaces[0],
        &vtable,
        svc,
        NULL,
        &error);

    if (error) {
        g_critical("Accessibility: register_object failed: %s",
                   error->message);
        g_error_free(error);
    }
}

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_message("Accessibility: D-Bus name acquired: %s", name);
}

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_warning("Accessibility: D-Bus name lost: %s", name);
}

/* -- Public API --------------------------------------------- */

ZylAccessibilityService *zyl_accessibility_service_create(void)
{
    ZylAccessibilityService *svc = calloc(1, sizeof(*svc));
    if (!svc) {
        g_critical("Accessibility: failed to allocate service");
        return NULL;
    }

    /* Defaults */
    svc->state.high_contrast      = false;
    svc->state.font_scale         = 1.0;
    svc->state.screen_reader_active = false;

    svc->introspection_data = g_dbus_node_info_new_for_xml(
        introspection_xml, NULL);
    if (!svc->introspection_data) {
        g_critical("Accessibility: failed to parse introspection XML");
        free(svc);
        return NULL;
    }

    svc->bus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        ZYL_ACCESSIBILITY_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        svc,
        NULL);

    return svc;
}

void zyl_accessibility_service_destroy(ZylAccessibilityService *svc)
{
    if (!svc) {
        return;
    }

    if (svc->registration_id > 0 && svc->connection) {
        g_dbus_connection_unregister_object(svc->connection,
                                            svc->registration_id);
    }

    if (svc->bus_owner_id > 0) {
        g_bus_unown_name(svc->bus_owner_id);
    }

    if (svc->introspection_data) {
        g_dbus_node_info_unref(svc->introspection_data);
    }

    free(svc);
}

void zyl_accessibility_set_high_contrast(ZylAccessibilityService *svc,
                                         bool enabled)
{
    if (!svc) {
        return;
    }
    svc->state.high_contrast = enabled;
    emit_state_changed(svc);
}

bool zyl_accessibility_set_font_scale(ZylAccessibilityService *svc,
                                      double scale)
{
    if (!svc) {
        return false;
    }
    if (scale < ZYL_FONT_SCALE_MIN || scale > ZYL_FONT_SCALE_MAX) {
        return false;
    }
    svc->state.font_scale = scale;
    emit_state_changed(svc);
    return true;
}

ZylAccessibilityState zyl_accessibility_get_state(
    const ZylAccessibilityService *svc)
{
    if (!svc) {
        return (ZylAccessibilityState){false, 1.0, false};
    }
    return svc->state;
}

/* -- main() ------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_message("Zyl OS Accessibility Service starting...");

    ZylAccessibilityService *svc = zyl_accessibility_service_create();
    if (!svc) {
        g_critical("Failed to create accessibility service");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_accessibility_service_destroy(svc);
    g_message("Zyl OS Accessibility Service stopped");
    return 0;
}
