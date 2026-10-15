/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 앱 데이터 서비스 (org.zylos.AppData) — 앱마다의 비공개 키-값 저장소
 * 수행범위: 읽기·쓰기·지우기·키 목록·전부 지우기. 어느 앱의 것인지는 브리지가
 *           넣는 caller 만으로 정해져 다른 앱의 데이터는 이름조차 댈 수 없다.
 *           앱 하나 = 컨테이너 /data/appdata/<앱 id>/ 하나 — 앱을 지우면
 *           컨테이너째 사라진다. 앱당 5 MiB, 키 256 바이트
 * 의존방향: gio/gio.h (D-Bus)
 * SOLID: SRP — 앱 비공개 데이터 보관만
 * ────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <string.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include "container.h"

#ifndef ZYL_APPDATA_ROOT
#define ZYL_APPDATA_ROOT ZYL_CONTAINERS
#endif

#define QUOTA_BYTES (5 * 1024 * 1024)
#define KEY_MAX 256

#define valid_app_id zyl_container_valid_id

static char *store_path(const char *app) {
    return g_build_filename(ZYL_APPDATA_ROOT, app, "appdata", NULL);
}

/* key → value of one app; empty when it has none yet */
static GHashTable *load(const char *app) {
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    char *path = store_path(app);
    gchar *data = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &data, &len, NULL)) {
        GVariant *v = g_variant_new_from_data(G_VARIANT_TYPE("a{ss}"), data, len, FALSE, g_free, data);
        GVariantIter it;
        const gchar *k, *val;
        g_variant_iter_init(&it, v);
        while (g_variant_iter_next(&it, "{&s&s}", &k, &val))
            g_hash_table_insert(t, g_strdup(k), g_strdup(val));
        g_variant_unref(v);
    }
    g_free(path);
    return t;
}

static gsize used_bytes(GHashTable *t) {
    gsize n = 0;
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, t);
    while (g_hash_table_iter_next(&it, &k, &v)) n += strlen(k) + strlen(v);
    return n;
}

static bool make_container(const char *app) {
    char *dir = zyl_container_dir(ZYL_APPDATA_ROOT, app, NULL, true);
    bool ok = dir != NULL;
    g_free(dir);
    return ok;
}

static bool save(const char *app, GHashTable *t) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{ss}"));
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, t);
    while (g_hash_table_iter_next(&it, &k, &v)) g_variant_builder_add(&b, "{ss}", k, v);
    GVariant *all = g_variant_ref_sink(g_variant_builder_end(&b));
    char *path = store_path(app);
    bool ok = make_container(app) &&
              g_file_set_contents_full(path, g_variant_get_data(all), (gssize)g_variant_get_size(all),
                  G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, NULL);
    g_free(path);
    g_variant_unref(all);
    return ok;
}

/* '' when absent: storing '' is the same as removing */
static char *get_item(const char *app, const char *key) {
    if (!valid_app_id(app)) return g_strdup("");
    GHashTable *t = load(app);
    const char *v = g_hash_table_lookup(t, key);
    char *out = g_strdup(v ? v : "");
    g_hash_table_unref(t);
    return out;
}

/* false: bad key, or the app would go over its quota */
static bool set_item(const char *app, const char *key, const char *value) {
    if (!valid_app_id(app) || !key || !*key || strlen(key) > KEY_MAX || !value) return false;
    GHashTable *t = load(app);
    g_hash_table_insert(t, g_strdup(key), g_strdup(value));
    bool ok = used_bytes(t) <= QUOTA_BYTES && save(app, t);
    g_hash_table_unref(t);
    return ok;
}

static bool remove_item(const char *app, const char *key) {
    if (!valid_app_id(app)) return false;
    GHashTable *t = load(app);
    bool ok = g_hash_table_remove(t, key) && save(app, t);
    g_hash_table_unref(t);
    return ok;
}

static GVariant *keys(const char *app) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
    if (valid_app_id(app)) {
        GHashTable *t = load(app);
        GHashTableIter it;
        gpointer k;
        g_hash_table_iter_init(&it, t);
        while (g_hash_table_iter_next(&it, &k, NULL)) g_variant_builder_add(&b, "s", k);
        g_hash_table_unref(t);
    }
    return g_variant_builder_end(&b);
}

static bool clear_all(const char *app) {
    if (!valid_app_id(app)) return false;
    GHashTable *t = g_hash_table_new(g_str_hash, g_str_equal);
    bool ok = save(app, t);
    g_hash_table_unref(t);
    return ok;
}

#ifndef ZYL_TEST
static const char *appdata_xml =
    "<node>"
    "  <interface name='org.zylos.AppData'>"
    "    <method name='GetItem'><arg type='s' name='caller' direction='in'/>"
    "      <arg type='s' name='key' direction='in'/><arg type='s' name='value' direction='out'/></method>"
    "    <method name='SetItem'><arg type='s' name='caller' direction='in'/>"
    "      <arg type='s' name='key' direction='in'/><arg type='s' name='value' direction='in'/>"
    "      <arg type='b' name='stored' direction='out'/></method>"
    "    <method name='RemoveItem'><arg type='s' name='caller' direction='in'/>"
    "      <arg type='s' name='key' direction='in'/><arg type='b' name='removed' direction='out'/></method>"
    "    <method name='Keys'><arg type='s' name='caller' direction='in'/>"
    "      <arg type='as' name='keys' direction='out'/></method>"
    "    <method name='Clear'><arg type='s' name='caller' direction='in'/>"
    "      <arg type='b' name='cleared' direction='out'/></method>"
    "  </interface>"
    "</node>";

static void handle(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    const gchar *app = NULL, *key = NULL, *value = NULL;
    GVariant *out = NULL;
    if (g_strcmp0(method, "SetItem") == 0) {
        g_variant_get(params, "(&s&s&s)", &app, &key, &value);
        out = g_variant_new("(b)", set_item(app, key, value));
    } else if (g_strcmp0(method, "GetItem") == 0) {
        g_variant_get(params, "(&s&s)", &app, &key);
        char *v = get_item(app, key);
        out = g_variant_new("(s)", v);
        g_free(v);
    } else if (g_strcmp0(method, "RemoveItem") == 0) {
        g_variant_get(params, "(&s&s)", &app, &key);
        out = g_variant_new("(b)", remove_item(app, key));
    } else if (g_strcmp0(method, "Keys") == 0) {
        g_variant_get(params, "(&s)", &app);
        out = g_variant_new("(@as)", keys(app));
    } else if (g_strcmp0(method, "Clear") == 0) {
        g_variant_get(params, "(&s)", &app);
        out = g_variant_new("(b)", clear_all(app));
    }
    g_dbus_method_invocation_return_value(inv, out);
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(appdata_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/AppData", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.AppData", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
