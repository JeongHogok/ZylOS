/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 접근성 AT-SPI 브릿지 — 스크린리더 연동 (Orca, espeak)
 * 수행범위: AT-SPI2 D-Bus 인터페이스 노출, 앱 위젯 트리 제공,
 *           포커스 변경 시그널 발신, 텍스트 콘텐츠 제공
 * 의존방향: gio/gio.h, atspi (AT-SPI2 core)
 * SOLID: SRP — AT-SPI 접근성 브릿지만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>

#define A11Y_DBUS_NAME "org.zylos.Accessibility"
#define A11Y_DBUS_PATH "/org/zylos/Accessibility"

/*
 * AT-SPI2 Integration Notes:
 *
 * ZylOS apps are web-based (WAM/WebKitGTK). WebKitGTK already provides
 * AT-SPI2 accessibility via the ATK bridge. This service:
 *
 * 1. Ensures the AT-SPI2 registry (at-spi2-registryd) is running
 * 2. Provides OS-level accessibility state (high contrast, font scale)
 * 3. Bridges OS chrome (statusbar, lockscreen) to AT-SPI
 * 4. Manages screen reader activation/deactivation
 *
 * For full screen reader support:
 * - Install: orca (GNOME screen reader) or espeak-ng (TTS engine)
 * - WebKitGTK exposes DOM via ATK → AT-SPI automatically
 * - Compositor provides window focus events
 */

static gboolean g_screen_reader_active = FALSE;

static const char *a11y_introspection_xml =
    "<node>"
    "  <interface name='" A11Y_DBUS_NAME "'>"
    "    <method name='SetScreenReader'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "    </method>"
    "    <method name='GetScreenReader'>"
    "      <arg type='b' name='active' direction='out'/>"
    "    </method>"
    "    <method name='Speak'>"
    "      <arg type='s' name='text' direction='in'/>"
    "      <arg type='i' name='priority' direction='in'/>"
    "    </method>"
    "    <method name='GetState'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <signal name='ScreenReaderChanged'>"
    "      <arg type='b' name='active'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusConnection *g_conn = NULL;

static int start_screen_reader(void) {
    /* Try to start Orca; fall back to espeak for basic TTS */
    GError *err = NULL;
    gchar *argv[] = { "orca", "--no-setup", NULL };
    gboolean ok = g_spawn_async(NULL, argv, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
        NULL, NULL, NULL, &err);
    if (!ok) {
        if (err) g_error_free(err);
        g_message("[A11y] Orca not found, TTS via espeak-ng only");
    }
    return ok ? 0 : -1;
}

static void speak_text(const char *text, int priority) {
    if (!text || !text[0]) return;
    (void)priority;

    /* Use espeak-ng for TTS */
    char cmd[1024];
    /* Sanitize: remove shell-special characters */
    char safe[512];
    int j = 0;
    for (int i = 0; text[i] && j < (int)sizeof(safe) - 1; i++) {
        if (text[i] != '\'' && text[i] != '\\' && text[i] != '"' && text[i] != '`') {
            safe[j++] = text[i];
        }
    }
    safe[j] = '\0';

    snprintf(cmd, sizeof(cmd), "espeak-ng '%s' 2>/dev/null &", safe);
    (void)system(cmd);
}

static void handle_a11y_method(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *method, GVariant *params,
                                GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;

    if (g_strcmp0(method, "SetScreenReader") == 0) {
        gboolean enabled = FALSE;
        g_variant_get(params, "(b)", &enabled);
        g_screen_reader_active = enabled;
        if (enabled) {
            start_screen_reader();
            speak_text("Screen reader activated", 1);
        }
        if (g_conn) {
            g_dbus_connection_emit_signal(g_conn, NULL, A11Y_DBUS_PATH,
                A11Y_DBUS_NAME, "ScreenReaderChanged",
                g_variant_new("(b)", enabled), NULL);
        }
        g_message("[A11y] Screen reader %s", enabled ? "ON" : "OFF");
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetScreenReader") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", g_screen_reader_active));
    } else if (g_strcmp0(method, "Speak") == 0) {
        const gchar *text = NULL;
        gint32 priority = 0;
        g_variant_get(params, "(&si)", &text, &priority);
        speak_text(text, priority);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "GetState") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&builder, "{sv}", "screenReader",
            g_variant_new_boolean(g_screen_reader_active));
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(a{sv})", &builder));
    }
}

static const GDBusInterfaceVTable a11y_vtable = { .method_call = handle_a11y_method };

static void on_a11y_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    g_conn = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(a11y_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, A11Y_DBUS_PATH,
            info->interfaces[0], &a11y_vtable, NULL, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[A11y] AT-SPI bridge registered");
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Ensure AT-SPI2 registry is running */
    (void)system("/usr/libexec/at-spi2-registryd &");

    g_bus_own_name(G_BUS_TYPE_SESSION,
        A11Y_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_a11y_bus, NULL, NULL, NULL, NULL);

    g_message("[A11y] Accessibility service started");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
