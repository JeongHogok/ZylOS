/* ----------------------------------------------------------
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 접근성 서비스 구현 — D-Bus로 고대비/폰트스케일/스크린리더 관리
 * 수행범위: D-Bus 메서드 핸들링, 상태 관리, 시그널 발송,
 *           스크린리더 켜기·끄기(at-spi2-registryd + orca 하나, 끄거나 서비스가
 *           끝나면 멈춤)와 읽어주기(espeak-ng)
 * 의존방향: accessibility.h, gio/gio.h
 * SOLID: SRP — 접근성 제어 로직만 담당
 * ---------------------------------------------------------- */

#include "accessibility.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifndef ZYL_ATSPI_REGISTRYD
#define ZYL_ATSPI_REGISTRYD "/usr/libexec/at-spi2-registryd"
#endif

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
    "    <method name='SetScreenReader'>"
    "      <arg direction='in'  type='b' name='enabled'/>"
    "    </method>"
    "    <method name='Speak'>"
    "      <arg direction='in'  type='s' name='text'/>"
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
    GPid                   orca_pid;     /* the reader while it runs, else 0 */
    guint                  orca_watch;
};

/* -- Screen reader (AT-SPI2 + Orca + espeak-ng) -------------- */

/*
 * WebKitGTK already exposes app content over ATK → AT-SPI, so the OS only has
 * to start the reader and speak its own chrome. Missing orca/espeak-ng is not
 * an error: the image may ship neither.
 */
static void spawn_detached(gchar **argv)
{
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                       G_SPAWN_STDERR_TO_DEV_NULL,
                       NULL, NULL, NULL, &error)) {
        g_message("Accessibility: %s unavailable: %s", argv[0],
                  error ? error->message : "spawn failed");
        g_clear_error(&error);
    }
}

static void speak_text(const char *text)
{
    if (!text || !text[0]) {
        return;
    }
    /* "--": an app's text starting with '-' is words, never an option such
       as -w <file> (write anywhere) or -f <file> (read any file aloud) */
    gchar *argv[] = { (gchar *)"espeak-ng", (gchar *)"--", (gchar *)text, NULL };
    spawn_detached(argv);
}

static void reap(GPid pid, gint status, gpointer data)
{
    (void)status;
    (void)data;
    g_spawn_close_pid(pid);
}

/* Orca ended by itself: the next "on" starts it again */
static void on_orca_exit(GPid pid, gint status, gpointer data)
{
    ZylAccessibilityService *svc = data;
    reap(pid, status, NULL);
    svc->orca_pid = 0;
    svc->orca_watch = 0;
}

static void orca_stop(ZylAccessibilityService *svc, bool wait_for_exit)
{
    if (!svc->orca_pid) {
        return;
    }
    g_source_remove(svc->orca_watch);
    kill(svc->orca_pid, SIGTERM);
    /* Off and straight back on starts a new one; the old one is reaped on its
       own. A service on its way out leaves that to init */
    if (wait_for_exit) {
        g_child_watch_add(svc->orca_pid, reap, NULL);
    } else {
        g_spawn_close_pid(svc->orca_pid);
    }
    svc->orca_pid = 0;
    svc->orca_watch = 0;
}

/* One orca while the reader is on; stopped when it is turned off */
static void screen_reader_set(ZylAccessibilityService *svc, gboolean on)
{
    svc->state.screen_reader_active = on;
    if (!on) {
        orca_stop(svc, true);
        return;
    }
    if (svc->orca_pid) return;
    /* at-spi2-registryd is what carries the widget tree to the reader */
    gchar *registry[] = { (gchar *)ZYL_ATSPI_REGISTRYD, NULL };
    gchar *orca[]     = { (gchar *)"orca", (gchar *)"--no-setup", NULL };
    spawn_detached(registry);
    GError *error = NULL;
    if (!g_spawn_async(NULL, orca, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                       G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                       NULL, NULL, &svc->orca_pid, &error)) {
        g_message("Accessibility: orca unavailable: %s", error->message);
        g_clear_error(&error);
        svc->orca_pid = 0;
        return;
    }
    svc->orca_watch = g_child_watch_add(svc->orca_pid, on_orca_exit, svc);
}

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
        /* Written so NaN fails too: it compares false with everything */
        if (!(scale >= ZYL_FONT_SCALE_MIN && scale <= ZYL_FONT_SCALE_MAX)) {
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

    } else if (g_strcmp0(method_name, "SetScreenReader") == 0) {
        gboolean enabled = FALSE;
        g_variant_get(parameters, "(b)", &enabled);
        screen_reader_set(svc, enabled);
        g_message("Accessibility: screen_reader=%s",
                  enabled ? "true" : "false");
        emit_state_changed(svc);
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (g_strcmp0(method_name, "Speak") == 0) {
        const gchar *text = NULL;
        g_variant_get(parameters, "(&s)", &text);
        speak_text(text);
        g_dbus_method_invocation_return_value(invocation, NULL);

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
        G_BUS_TYPE_SYSTEM,
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

    /* The reader does not outlive the service that started it */
    orca_stop(svc, false);

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

/* -- main() ------------------------------------------------- */

#ifndef ZYL_TEST
static gboolean on_term(gpointer loop)
{
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

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
    g_unix_signal_add(SIGTERM, on_term, loop);   /* so destroy stops the reader */
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    zyl_accessibility_service_destroy(svc);
    g_message("Zyl OS Accessibility Service stopped");
    return 0;
}
#endif /* ZYL_TEST */
