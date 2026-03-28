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
    JsonObject *obj = json_node_get_object(root);

    ZylAppManifest *m = g_new0(ZylAppManifest, 1);
    m->id        = g_strdup(json_object_get_string_member(obj, "id"));
    m->name      = g_strdup(json_object_get_string_member(obj, "name"));
    m->version   = g_strdup(json_object_get_string_member_with_default(
                       obj, "version", "1.0.0"));
    m->entry     = g_strdup(json_object_get_string_member_with_default(
                       obj, "entry", "index.html"));
    m->icon      = g_strdup(json_object_get_string_member_with_default(
                       obj, "icon", "icon.png"));
    m->base_path = g_strdup(app_dir);

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

    g_object_unref(parser);
    return m;
}

/* ─── Free a manifest ─── */
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

        ZylAppManifest *manifest = zyl_manifest_parse(app_dir);
        if (manifest) {
            manifest->is_system = is_system;
            g_hash_table_insert(manifests,
                                g_strdup(manifest->id), manifest);
            g_message("Registered app: %s (%s)",
                      manifest->name, manifest->id);
        }
    }
    closedir(dir);
}
