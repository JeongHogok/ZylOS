/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Library
 *
 * 역할: 서비스가 설정 한 범주를 따라가기 — settings_follow.h 의 구현
 * 수행범위: 이름 감시(나타나면 Get, 사라지면 — 한 번도 답이 없었을 때만 — NULL),
 *           CategoryChanged 구독(그 범주면 Get)
 * 의존방향: gio (D-Bus)
 * SOLID: SRP — 설정 범주 구독만
 * ────────────────────────────────────────────────────────── */

#include "settings_follow.h"

#include <string.h>

#define SETTINGS_NAME "org.zylos.Settings"
#define SETTINGS_PATH "/org/zylos/Settings"

struct ZylSettingsFollow {
    GDBusConnection *conn;
    char            *category;
    ZylSettingsFn    fn;
    gpointer         data;
    guint            sub, watch;
    GCancellable    *cancel;           /* an answer on its way when unfollowed */
    bool             answered;         /* the settings service has answered once */
};
typedef ZylSettingsFollow Follow;

static void on_get(GObject *src, GAsyncResult *res, gpointer data) {
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    if (!r) return;                     /* unfollowed (cancelled), or gone: the vanish tells */
    Follow *f = data;
    f->answered = true;
    GVariant *values = g_variant_get_child_value(r, 0);
    f->fn(values, f->data);
    g_variant_unref(values);
    g_variant_unref(r);
}

static void read_category(GDBusConnection *conn, Follow *f) {
    g_dbus_connection_call(conn, SETTINGS_NAME, SETTINGS_PATH, SETTINGS_NAME, "Get",
        g_variant_new("(s)", f->category), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, f->cancel, on_get, f);
}

static void on_changed(GDBusConnection *conn, const gchar *sender, const gchar *path,
                       const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
    (void)sender; (void)path; (void)iface; (void)signal;
    Follow *f = data;
    const gchar *category = NULL;
    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) return;
    g_variant_get(params, "(&s)", &category);
    if (strcmp(category, f->category) == 0) read_category(conn, f);
}

static void on_appeared(GDBusConnection *conn, const gchar *name, const gchar *owner, gpointer data) {
    (void)name; (void)owner;
    read_category(conn, data);
}

static void on_vanished(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)conn; (void)name;
    Follow *f = data;
    if (!f->answered) f->fn(NULL, f->data);   /* a restart keeps what it last said */
}

ZylSettingsFollow *zyl_settings_follow(GDBusConnection *conn, const char *category,
                                       ZylSettingsFn fn, gpointer data) {
    if (!conn || !category || !fn) return NULL;
    Follow *f = g_new0(Follow, 1);
    f->conn = g_object_ref(conn);
    f->category = g_strdup(category);
    f->fn = fn;
    f->data = data;
    f->cancel = g_cancellable_new();
    f->sub = g_dbus_connection_signal_subscribe(conn, SETTINGS_NAME, SETTINGS_NAME,
        "CategoryChanged", SETTINGS_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_changed, f, NULL);
    f->watch = g_bus_watch_name_on_connection(conn, SETTINGS_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_appeared, on_vanished, f, NULL);
    return f;
}

void zyl_settings_unfollow(ZylSettingsFollow *f) {
    if (!f) return;
    g_cancellable_cancel(f->cancel);    /* on_get then sees no reply and touches nothing */
    g_object_unref(f->cancel);
    g_dbus_connection_signal_unsubscribe(f->conn, f->sub);
    g_bus_unwatch_name(f->watch);
    g_object_unref(f->conn);
    g_free(f->category);
    g_free(f);
}

bool zyl_settings_bool(GVariant *values, const char *key, bool def) {
    gboolean b = FALSE;
    return values && g_variant_lookup(values, key, "b", &b) ? (bool)b : def;
}
