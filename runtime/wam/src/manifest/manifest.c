/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Repository
 *
 * 역할: 앱 매니페스트(app.json) 파싱 및 검증
 * 수행범위: 시스템/사용자 앱 디렉토리에서 app.json 읽기, ZylAppManifest 구조체 채움
 * 의존방향: manifest.h
 * SOLID: SRP — 매니페스트 파싱과 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include "manifest.h"

#include <dirent.h>
#include <stdio.h>
#include <json-glib/json-glib.h>

/* ─── Parse a single app.json ─── */
ZylAppManifest *zyl_manifest_parse(const char *app_dir) {
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/app.json", app_dir);

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, manifest_path, &error)) {
        g_warning("Failed to parse %s: %s", manifest_path, error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_warning("Manifest %s: JSON root is not an object", manifest_path);
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *obj = json_node_get_object(root);

    /* Validate required fields before strdup */
    if (!json_object_has_member(obj, "id") ||
        !json_object_has_member(obj, "name")) {
        g_warning("Manifest %s: missing required 'id' or 'name' field",
                  manifest_path);
        g_object_unref(parser);
        return NULL;
    }

    const char *raw_id   = json_object_get_string_member(obj, "id");
    const char *raw_name = json_object_get_string_member(obj, "name");
    if (!raw_id || raw_id[0] == '\0' || !raw_name || raw_name[0] == '\0') {
        g_warning("Manifest %s: 'id' or 'name' is empty", manifest_path);
        g_object_unref(parser);
        return NULL;
    }

    ZylAppManifest *m = g_new0(ZylAppManifest, 1);
    m->id        = g_strdup(raw_id);
    m->name      = g_strdup(raw_name);
    m->version   = g_strdup(json_object_get_string_member_with_default(
                       obj, "version", "1.0.0"));
    m->entry     = g_strdup(json_object_get_string_member_with_default(
                       obj, "entry", "index.html"));
    m->icon      = g_strdup(json_object_get_string_member_with_default(
                       obj, "icon", "icon.png"));
    m->base_path = g_strdup(app_dir);
    m->launcher  = TRUE;
    if (json_object_has_member(obj, "launcher") &&
        json_node_get_value_type(json_object_get_member(obj, "launcher")) == G_TYPE_BOOLEAN)
        m->launcher = json_object_get_boolean_member(obj, "launcher");

    /* Parse permissions array */
    if (json_object_has_member(obj, "permissions")) {
        JsonArray *perms = json_object_get_array_member(obj, "permissions");
        m->n_permissions = json_array_get_length(perms);
        m->permissions = g_new0(char *, m->n_permissions + 1);
        for (int i = 0; i < m->n_permissions; i++) {
            m->permissions[i] = g_strdup(
                json_array_get_string_element(perms, i));
        }
    }

    /* What the app opens: [{ action, mimeType }] — malformed entries are skipped */
    GPtrArray *actions = g_ptr_array_new(), *mimes = g_ptr_array_new();
    JsonNode *in = json_object_has_member(obj, "intents") ? json_object_get_member(obj, "intents") : NULL;
    JsonArray *intents = in && JSON_NODE_HOLDS_ARRAY(in) ? json_node_get_array(in) : NULL;
    for (guint i = 0; intents && i < json_array_get_length(intents); i++) {
        JsonNode *e = json_array_get_element(intents, i);
        JsonObject *f = JSON_NODE_HOLDS_OBJECT(e) ? json_node_get_object(e) : NULL;
        const char *action = f ? json_object_get_string_member_with_default(f, "action", NULL) : NULL;
        if (!action) continue;
        g_ptr_array_add(actions, g_strdup(action));
        g_ptr_array_add(mimes, g_strdup(json_object_get_string_member_with_default(f, "mimeType", "*/*")));
    }
    g_ptr_array_add(actions, NULL);
    g_ptr_array_add(mimes, NULL);
    m->intent_actions = (char **)g_ptr_array_free(actions, FALSE);
    m->intent_mimes = (char **)g_ptr_array_free(mimes, FALSE);

    g_object_unref(parser);
    return m;
}

/* ─── Free a manifest ─── */
static GVariant *string_array(JsonObject *obj, const char *member) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
    JsonArray *arr = obj && json_object_has_member(obj, member) &&
                     JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, member))
                     ? json_object_get_array_member(obj, member) : NULL;
    for (guint i = 0; arr && i < json_array_get_length(arr); i++) {
        JsonNode *n = json_array_get_element(arr, i);
        if (JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING)
            g_variant_builder_add(&b, "s", json_node_get_string(n));
    }
    return g_variant_builder_end(&b);
}

static const char *text_member(JsonObject *obj, const char *member, const char *fallback) {
    if (!obj || !json_object_has_member(obj, member)) return fallback;
    JsonNode *n = json_object_get_member(obj, member);
    return JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
           ? json_node_get_string(n) : fallback;
}

GVariant *zyl_manifest_app_info(const ZylAppManifest *m) {
    /* Presentation fields are read from app.json as shipped; the parsed
       manifest keeps only what the WAM itself needs */
    char *path = g_build_filename(m->base_path, "app.json", NULL);
    JsonParser *p = json_parser_new();
    JsonObject *obj = json_parser_load_from_file(p, path, NULL) &&
                      JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))
                      ? json_node_get_object(json_parser_get_root(p)) : NULL;
    const char *last = strrchr(m->id, '.');
    last = last ? last + 1 : m->id;
    char *name_key = g_strconcat("app.", last, NULL);

    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "id", g_variant_new_string(m->id));
    g_variant_builder_add(&d, "{sv}", "name", g_variant_new_string(m->name ? m->name : last));
    g_variant_builder_add(&d, "{sv}", "nameKey", g_variant_new_string(name_key));
    g_variant_builder_add(&d, "{sv}", "icon", g_variant_new_string(text_member(obj, "iconKey", last)));
    g_variant_builder_add(&d, "{sv}", "color", g_variant_new_string(text_member(obj, "color", "icon-blue")));
    g_variant_builder_add(&d, "{sv}", "version", g_variant_new_string(m->version ? m->version : "1.0.0"));
    g_variant_builder_add(&d, "{sv}", "description", g_variant_new_string(text_member(obj, "description", "")));
    g_variant_builder_add(&d, "{sv}", "system", g_variant_new_boolean(m->is_system));
    g_variant_builder_add(&d, "{sv}", "permissions", string_array(obj, "permissions"));
    g_variant_builder_add(&d, "{sv}", "formFactors", string_array(obj, "formFactors"));
    g_variant_builder_add(&d, "{sv}", "iconSvg", g_variant_new_string(text_member(obj, "iconSvg", "")));
    g_variant_builder_add(&d, "{sv}", "launcher", g_variant_new_boolean(m->launcher));
    g_free(name_key);
    g_object_unref(p);
    g_free(path);
    return g_variant_builder_end(&d);
}

void zyl_manifest_free(gpointer data) {
    ZylAppManifest *m = data;
    if (!m) return;

    g_free(m->id);
    g_free(m->name);
    g_free(m->version);
    g_free(m->entry);
    g_free(m->icon);
    g_free(m->base_path);
    if (m->permissions) g_strfreev(m->permissions);
    g_strfreev(m->intent_actions);
    g_strfreev(m->intent_mimes);
    g_free(m);
}

/* ─── Scan a directory for app subdirectories ─── */
void zyl_manifest_scan_dir(GHashTable *manifests,
                           const char *base_dir,
                           gboolean    is_system) {
    DIR *dir = opendir(base_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char app_dir[512];
        snprintf(app_dir, sizeof(app_dir), "%s/%s", base_dir, entry->d_name);
        /* shared/ and system/ hold the OS's code, not apps */
        char *json = g_build_filename(app_dir, "app.json", NULL);
        gboolean is_app = g_file_test(json, G_FILE_TEST_EXISTS);
        g_free(json);
        if (!is_app) continue;

        ZylAppManifest *manifest = zyl_manifest_parse(app_dir);
        if (manifest) {
            /* The user directory is scanned after the system one, and a plain
               insert would let a user-installed app replace a system app just
               by reusing its id — com.zylos.settings with is_system FALSE but
               the real settings app gone. A system entry is never replaced. */
            ZylAppManifest *existing = g_hash_table_lookup(manifests, manifest->id);
            if (existing && existing->is_system) {
                g_warning("Rejected app %s in %s: id belongs to a system app",
                          manifest->id, base_dir);
                zyl_manifest_free(manifest);
                continue;
            }
            manifest->is_system = is_system;
            g_hash_table_insert(manifests,
                                g_strdup(manifest->id), manifest);
            g_message("Registered app: %s (%s)",
                      manifest->name, manifest->id);
        }
    }
    closedir(dir);
}
