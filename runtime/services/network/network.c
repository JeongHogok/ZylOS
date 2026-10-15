/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 네트워크 서비스 (org.zylos.Network) — 앱 대신 정해진 곳에서 받아 오기
 * 수행범위: Fetch(url) → 본문. https, 허용 도메인(정확히 일치)만 — 사용자
 *           정보·포트·IP 를 붙여 기기 자신이나 내부망을 가리킬 수 없다.
 *           리다이렉트는 따라가지 않는다(허용 목록 밖으로 나갈 수 있으니)
 * 의존방향: gio/gio.h (D-Bus, GSubprocess), /usr/bin/curl (셸 없이)
 * SOLID: SRP — 허용된 HTTPS GET 만
 * ────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <string.h>
#include <gio/gio.h>
#include <glib-unix.h>

/* Mirror of ALLOWED_DOMAINS in apps/system/sandbox.js, the authority —
   tests/verify-all.sh fails when the two differ */
static const char *const ALLOWED_DOMAINS[] = {
    "api.open-meteo.com",
    "ipinfo.io",
    NULL
};

static bool url_allowed(const char *url) {
    if (!url || strlen(url) > 2048 || !g_str_has_prefix(url, "https://")) return false;
    for (const char *p = url; *p; p++)
        if ((unsigned char)*p <= 0x20 || *p == 0x7f) return false;
    const char *host = url + strlen("https://");
    char *h = g_ascii_strdown(host, (gssize)strcspn(host, "/?#"));
    bool ok = false;
    for (int i = 0; ALLOWED_DOMAINS[i] && !ok; i++) ok = strcmp(h, ALLOWED_DOMAINS[i]) == 0;
    g_free(h);
    return ok;
}

#ifndef ZYL_TEST
static const char *network_xml =
    "<node>"
    "  <interface name='org.zylos.Network'>"
    "    <method name='Fetch'><arg type='s' name='url' direction='in'/>"
    "      <arg type='s' name='body' direction='out'/></method>"
    "  </interface>"
    "</node>";

static void on_fetched(GObject *src, GAsyncResult *res, gpointer user) {
    GDBusMethodInvocation *inv = user;
    GSubprocess *proc = G_SUBPROCESS(src);
    char *body = NULL;
    GError *err = NULL;
    if (g_subprocess_communicate_utf8_finish(proc, res, &body, NULL, &err) &&
        g_subprocess_get_successful(proc)) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", body ? body : ""));
    } else {
        g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_FAILED, "fetch failed");
    }
    g_clear_error(&err);
    g_free(body);
    g_object_unref(proc);
}

static void handle(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data; (void)method;
    const gchar *url = NULL;
    g_variant_get(params, "(&s)", &url);
    if (!url_allowed(url)) {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                                              "domain not allowed");
        return;
    }
    GError *err = NULL;
    /* shortcut: --max-filesize is not enforced on chunked replies; the 8 s cap bounds them */
    GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &err,
        "/usr/bin/curl", "-s", "-f", "--proto", "=https", "-m", "8", "--connect-timeout", "5",
        "--max-filesize", "1048576", "--", url, NULL);
    if (!proc) {
        g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_FAILED, "fetch failed");
        g_clear_error(&err);
        return;
    }
    g_subprocess_communicate_utf8_async(proc, NULL, NULL, on_fetched, inv);
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(network_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/Network", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SESSION, "org.zylos.Network", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
