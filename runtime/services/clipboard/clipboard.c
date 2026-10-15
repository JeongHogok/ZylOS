/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 클립보드 서비스 (org.zylos.Clipboard) — 앱 사이 복사·붙여넣기
 * 수행범위: 복사(누가 복사했는지는 브리지가 넣는 caller), 붙여넣기, 지우기.
 *           민감한 값(sensitive)은 60초 뒤 스스로 지움. 메모리에만 두고
 *           디스크에 남기지 않는다 (같은 계약: apps/system/services/clipboard.js)
 * 의존방향: gio/gio.h (D-Bus)
 * SOLID: SRP — 클립보드 내용 보관만
 * ────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <string.h>
#include <gio/gio.h>
#include <glib-unix.h>

#define SENSITIVE_CLEAR_S 60
#define MAX_TEXT (1024 * 1024)

typedef struct {
    char   *text;        /* NULL: empty */
    char   *source_app;
    gint64  timestamp_ms;
    guint   clear_id;    /* pending clear of a sensitive value */
} Clip;

static void clip_clear(Clip *c) {
    g_clear_pointer(&c->text, g_free);
    g_clear_pointer(&c->source_app, g_free);
    c->timestamp_ms = 0;
    if (c->clear_id) g_source_remove(c->clear_id);
    c->clear_id = 0;
}

static gboolean on_sensitive_timeout(gpointer data) {
    Clip *c = data;
    c->clear_id = 0;
    clip_clear(c);
    return G_SOURCE_REMOVE;
}

/* false: no text, or more than MAX_TEXT bytes */
static bool clip_copy(Clip *c, const char *caller, const char *text, bool sensitive, gint64 now_ms) {
    if (!text || strlen(text) > MAX_TEXT) return false;
    clip_clear(c);
    c->text = g_strdup(text);
    c->source_app = g_strdup(caller);
    c->timestamp_ms = now_ms;
    if (sensitive) c->clear_id = g_timeout_add_seconds(SENSITIVE_CLEAR_S, on_sensitive_timeout, c);
    return true;
}

/* { text, sourceApp, timestamp }, or {} when empty */
static GVariant *clip_variant(const Clip *c) {
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    if (c->text) {
        g_variant_builder_add(&d, "{sv}", "text", g_variant_new_string(c->text));
        g_variant_builder_add(&d, "{sv}", "sourceApp", g_variant_new_string(c->source_app));
        g_variant_builder_add(&d, "{sv}", "timestamp", g_variant_new_int64(c->timestamp_ms));
    }
    return g_variant_builder_end(&d);
}

#ifndef ZYL_TEST
static Clip g_clip;

static const char *clipboard_xml =
    "<node>"
    "  <interface name='org.zylos.Clipboard'>"
    "    <method name='Copy'><arg type='s' name='caller' direction='in'/>"
    "      <arg type='a{sv}' name='params' direction='in'/>"
    "      <arg type='b' name='copied' direction='out'/></method>"
    "    <method name='Paste'><arg type='a{sv}' name='clip' direction='out'/></method>"
    "    <method name='Clear'><arg type='b' name='cleared' direction='out'/></method>"
    "  </interface>"
    "</node>";

static void handle(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    if (g_strcmp0(method, "Copy") == 0) {
        const gchar *caller = NULL, *text = NULL;
        gboolean sensitive = FALSE;
        GVariant *p = NULL;
        g_variant_get(params, "(&s@a{sv})", &caller, &p);
        g_variant_lookup(p, "text", "&s", &text);
        g_variant_lookup(p, "sensitive", "b", &sensitive);
        bool ok = clip_copy(&g_clip, caller, text, sensitive, g_get_real_time() / 1000);
        g_variant_unref(p);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ok));
    } else if (g_strcmp0(method, "Paste") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{sv})", clip_variant(&g_clip)));
    } else if (g_strcmp0(method, "Clear") == 0) {
        clip_clear(&g_clip);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", TRUE));
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(clipboard_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/Clipboard", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SESSION, "org.zylos.Clipboard", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
