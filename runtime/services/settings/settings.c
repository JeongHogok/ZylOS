/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 설정 서비스 — 카테고리별 설정을 기기에 영속 (org.zylos.Settings)
 * 수행범위: OS 기본값(settings-defaults.json) 위에 사용자 변경분을 겹쳐 제공,
 *           변경분만 /data/settings 에 원자적으로 저장 (기본값이 바뀌면 손대지
 *           않은 키는 새 기본값을 따른다), 손상된 파일은 보존하고 기본값으로 계속
 * 의존방향: gio/gio.h (D-Bus), json-glib (JSON ↔ GVariant)
 * SOLID: SRP — 설정 저장만. 계약은 apps/system/services/settings.js 와 같다
 *        (get → 카테고리 객체, update → 갱신된 카테고리 객체). 저장된 적 없는
 *        카테고리는 빈 객체 — D-Bus 에는 null 이 없고, 호출부는 둘 다
 *        "저장된 값 없음"으로 읽는다 (data && data.key)
 * ────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#ifndef ZYL_TEST
#include <glib-unix.h>
#endif

#ifndef ZYL_SETTINGS_DEFAULTS
#define ZYL_SETTINGS_DEFAULTS "/usr/share/zyl-os/settings-defaults.json"
#endif
#ifndef ZYL_SETTINGS_FILE
#define ZYL_SETTINGS_FILE     "/data/settings/settings.json"
#endif
#define SAVE_DELAY_MS 300   /* a dragged slider writes once, not per step */

static JsonObject *g_defaults;   /* category → object, from the OS image */
static JsonObject *g_user;       /* category → object, only what was changed */
static guint       g_save_source;

static JsonObject *load_object(const char *path, gboolean keep_corrupt) {
    JsonParser *p = json_parser_new();
    GError *err = NULL;
    JsonObject *obj = NULL;
    if (json_parser_load_from_file(p, path, &err)) {
        JsonNode *root = json_parser_get_root(p);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) obj = json_object_ref(json_node_get_object(root));
    } else if (!g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        g_warning("[Settings] %s unreadable: %s", path, err->message);
    }
    if (!obj && keep_corrupt && g_file_test(path, G_FILE_TEST_EXISTS)) {
        /* Keep what was there for recovery; starting over must not destroy it */
        char *aside = g_strconcat(path, ".corrupt", NULL);
        g_rename(path, aside);
        g_free(aside);
    }
    g_clear_error(&err);
    g_object_unref(p);
    return obj ? obj : json_object_new();
}

static void settings_load(void) {
    if (g_defaults) json_object_unref(g_defaults);
    if (g_user) json_object_unref(g_user);
    g_defaults = load_object(ZYL_SETTINGS_DEFAULTS, FALSE);
    g_user = load_object(ZYL_SETTINGS_FILE, TRUE);
}

static gboolean settings_save(void) {
    JsonNode *root = json_node_init_object(json_node_alloc(), g_user);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);
    json_generator_set_pretty(gen, TRUE);
    gsize len = 0;
    gchar *text = json_generator_to_data(gen, &len);
    char *dir = g_path_get_dirname(ZYL_SETTINGS_FILE);
    g_mkdir_with_parents(dir, 0700);
    GError *err = NULL;
    gboolean ok = g_file_set_contents_full(ZYL_SETTINGS_FILE, text, (gssize)len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, &err);
    if (!ok) {
        g_warning("[Settings] cannot save: %s", err->message);
        g_error_free(err);
    }
    g_free(dir);
    g_free(text);
    g_object_unref(gen);
    json_node_unref(root);
    return ok;
}

static gboolean on_save_timer(gpointer data) {
    (void)data;
    g_save_source = 0;
    settings_save();
    return G_SOURCE_REMOVE;
}

static void schedule_save(void) {
    if (g_save_source) g_source_remove(g_save_source);
    g_save_source = g_timeout_add(SAVE_DELAY_MS, on_save_timer, NULL);
}

static JsonObject *member_object(JsonObject *o, const char *key) {
    JsonNode *n = json_object_get_member(o, key);
    return n && JSON_NODE_HOLDS_OBJECT(n) ? json_node_get_object(n) : NULL;
}

/* Defaults with the user's values on top; NULL when neither has the category */
static JsonObject *settings_get(const char *category) {
    JsonObject *def = member_object(g_defaults, category);
    JsonObject *usr = member_object(g_user, category);
    if (!def && !usr) return NULL;
    JsonObject *out = json_object_new();
    JsonObject *layers[] = { def, usr };
    for (int i = 0; i < 2; i++) {
        if (!layers[i]) continue;
        GList *keys = json_object_get_members(layers[i]);
        for (GList *k = keys; k; k = k->next)
            json_object_set_member(out, k->data,
                json_node_copy(json_object_get_member(layers[i], k->data)));
        g_list_free(keys);
    }
    return out;
}

static JsonObject *settings_update(const char *category, const char *key, JsonNode *value) {
    JsonObject *cat = member_object(g_user, category);
    if (!cat) {
        cat = json_object_new();
        json_object_set_object_member(g_user, category, cat);
    }
    json_object_set_member(cat, key, value);
    schedule_save();
    return settings_get(category);
}

/* An app was removed (org.zylos.AppStore AppRemoved): what Settings keeps for
   it goes too — the permissions the user turned off for it (rule 12: nothing
   of a removed app stays). TRUE when there was something to drop */
static gboolean settings_forget_app(const char *app_id) {
    JsonObject *perms = member_object(g_user, "app_permissions");
    if (!perms || !app_id || !json_object_has_member(perms, app_id)) return FALSE;
    json_object_remove_member(perms, app_id);
    schedule_save();
    return TRUE;
}

/* ─── D-Bus ─── */
static const char *settings_introspection_xml =
    "<node>"
    "  <interface name='org.zylos.Settings'>"
    "    <method name='Get'>"
    "      <arg type='s' name='category' direction='in'/>"
    "      <arg type='a{sv}' name='value' direction='out'/>"
    "    </method>"
    "    <method name='Update'>"
    "      <arg type='s' name='category' direction='in'/>"
    "      <arg type='s' name='key' direction='in'/>"
    "      <arg type='v' name='value' direction='in'/>"
    "      <arg type='a{sv}' name='category_value' direction='out'/>"
    "    </method>"
    /* The WAM follows app_permissions through this: a permission the user
       switches off in Settings must take effect without a restart */
    "    <signal name='CategoryChanged'>"
    "      <arg type='s' name='category'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusConnection *g_settings_bus;

static void emit_category_changed(const char *category) {
    if (!g_settings_bus || !category || !category[0]) return;
    g_dbus_connection_emit_signal(g_settings_bus, NULL, "/org/zylos/Settings",
        "org.zylos.Settings", "CategoryChanged",
        g_variant_new("(s)", category), NULL);
}

/* A category as a{sv}: what the WAM bridge turns back into a JSON object */
static GVariant *object_variant(JsonObject *obj) {
    JsonNode *n = json_node_init_object(json_node_alloc(), obj);
    GVariant *v = json_gvariant_deserialize(n, "a{sv}", NULL);
    json_node_unref(n);
    return v;
}

static void handle_settings_method(GDBusConnection *conn, const gchar *sender,
                                   const gchar *path, const gchar *iface,
                                   const gchar *method, GVariant *params,
                                   GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    if (g_strcmp0(method, "Get") == 0) {
        const gchar *category = NULL;
        g_variant_get(params, "(&s)", &category);
        JsonObject *obj = settings_get(category);
        if (!obj) obj = json_object_new();
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(@a{sv})", object_variant(obj)));
        json_object_unref(obj);
    } else if (g_strcmp0(method, "Update") == 0) {
        const gchar *category = NULL, *key = NULL;
        GVariant *value = NULL;
        g_variant_get(params, "(&s&sv)", &category, &key, &value);
        if (!*category || !*key) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_INVALID_ARGS, "category and key are required");
        } else if (strcmp(category, "security") == 0 && strcmp(key, "pin") == 0) {
            /* The PIN lives in the lock service as a hash, never here */
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_ACCESS_DENIED, "the PIN belongs to the lock service");
        } else {
            JsonObject *obj = settings_update(category, key, json_gvariant_serialize(value));
            g_dbus_method_invocation_return_value(inv,
                g_variant_new("(@a{sv})", object_variant(obj)));
            json_object_unref(obj);
            emit_category_changed(category);
        }
        g_variant_unref(value);
    }
}

static const GDBusInterfaceVTable settings_vtable = { .method_call = handle_settings_method };

#ifndef ZYL_TEST
static void on_app_removed(GDBusConnection *conn, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *signal, GVariant *params,
                           gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)signal; (void)data;
    const gchar *app_id = NULL;
    g_variant_get(params, "(&s)", &app_id);
    if (settings_forget_app(app_id)) emit_category_changed("app_permissions");
}

static void on_settings_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    g_settings_bus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(settings_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0])
        g_dbus_connection_register_object(conn, "/org/zylos/Settings",
            info->interfaces[0], &settings_vtable, NULL, NULL, NULL);
    if (info) g_dbus_node_info_unref(info);
    g_dbus_connection_signal_subscribe(conn, "org.zylos.AppStore", "org.zylos.AppStore",
        "AppRemoved", "/org/zylos/AppStore", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_app_removed, NULL, NULL);
}

static gboolean on_term(gpointer loop) {
    if (g_save_source) {           /* a pending write must not be lost at shutdown */
        g_source_remove(g_save_source);
        g_save_source = 0;
        settings_save();
    }
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    settings_load();
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.Settings", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_settings_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_unix_signal_add(SIGINT, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
