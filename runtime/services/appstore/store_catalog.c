/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Service
 *
 * 역할: 앱스토어 카탈로그 해석 (store_catalog.h)
 * 수행범위: catalog.json → 검증된 항목, URL 해석, 버전 비교
 * 의존방향: store_catalog.h, container.h (앱 id 규칙)
 * SOLID: SRP — 해석만
 * ────────────────────────────────────────────────────────── */

#include "store_catalog.h"
#include "../appdata/container.h"
#include <json-glib/json-glib.h>
#include <string.h>

static const char *text(JsonObject *o, const char *name) {
    JsonNode *n = json_object_has_member(o, name) ? json_object_get_member(o, name) : NULL;
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
           ? json_node_get_string(n) : NULL;
}

static gboolean is_version(const char *v) {
    return v && g_regex_match_simple("^[0-9]+(\\.[0-9]+)*$", v, 0, 0);
}

static gboolean is_hex64(const char *s) {
    if (!s || strlen(s) != 64) return FALSE;
    for (const char *p = s; *p; p++) if (!g_ascii_isxdigit(*p)) return FALSE;
    return TRUE;
}

static gboolean is_base64(const char *s) {
    if (!s || !*s) return FALSE;
    for (const char *p = s; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '+' && *p != '/' && *p != '=') return FALSE;
    return TRUE;
}

char *zyl_store_resolve_url(const char *base, const char *ref) {
    if (!ref || !*ref) return NULL;
    GError *err = NULL;
    char *abs = g_uri_resolve_relative(base, ref, G_URI_FLAGS_NONE, &err);
    g_clear_error(&err);
    if (abs && !g_str_has_prefix(abs, "https://")) g_clear_pointer(&abs, g_free);
    return abs;
}

int zyl_store_version_cmp(const char *a, const char *b) {
    gchar **x = g_strsplit(a ? a : "", ".", -1), **y = g_strsplit(b ? b : "", ".", -1);
    guint nx = g_strv_length(x), ny = g_strv_length(y);
    int r = 0;
    for (guint i = 0; !r && (i < nx || i < ny); i++) {
        gint64 m = i < nx ? g_ascii_strtoll(x[i], NULL, 10) : 0;
        gint64 n = i < ny ? g_ascii_strtoll(y[i], NULL, 10) : 0;
        r = m < n ? -1 : m > n;
    }
    g_strfreev(x);
    g_strfreev(y);
    return r;
}

void zyl_store_app_free(gpointer data) {
    ZylStoreApp *a = data;
    if (!a) return;
    g_free(a->id); g_free(a->name); g_free(a->version); g_free(a->description);
    g_free(a->developer); g_free(a->category); g_free(a->package_url);
    g_free(a->sha256); g_free(a->signature);
    g_strfreev(a->permissions);
    g_free(a);
}

static ZylStoreApp *entry(JsonObject *o, const char *catalog_url) {
    const char *id = text(o, "id"), *name = text(o, "name"), *version = text(o, "version");
    const char *sha = text(o, "sha256"), *sig = text(o, "signature"), *pkg = text(o, "package");
    JsonNode *size = json_object_has_member(o, "size") ? json_object_get_member(o, "size") : NULL;
    gint64 bytes = size && JSON_NODE_HOLDS_VALUE(size) && json_node_get_value_type(size) == G_TYPE_INT64
                   ? json_node_get_int(size) : 0;
    char *url = zyl_store_resolve_url(catalog_url, pkg);
    /* An id names a directory and has a dot (reverse domain) */
    if (!zyl_container_valid_id(id) || !strchr(id, '.') || !name || !*name || !is_version(version) ||
        !is_hex64(sha) || !is_base64(sig) || !url || bytes <= 0 || bytes > ZYL_STORE_MAX_PACKAGE) {
        g_free(url);
        return NULL;
    }
    ZylStoreApp *a = g_new0(ZylStoreApp, 1);
    a->id = g_strdup(id);
    a->name = g_strdup(name);
    a->version = g_strdup(version);
    a->description = g_strdup(text(o, "description") ? text(o, "description") : "");
    a->developer = g_strdup(text(o, "developer") ? text(o, "developer") : "");
    a->category = g_strdup(text(o, "category") ? text(o, "category") : "");
    a->package_url = url;
    a->sha256 = g_ascii_strdown(sha, -1);
    a->signature = g_strdup(sig);
    a->size = bytes;
    GPtrArray *perms = g_ptr_array_new();
    JsonNode *pn = json_object_has_member(o, "permissions") ? json_object_get_member(o, "permissions") : NULL;
    JsonArray *pa = pn && JSON_NODE_HOLDS_ARRAY(pn) ? json_node_get_array(pn) : NULL;
    for (guint i = 0; pa && i < json_array_get_length(pa); i++) {
        JsonNode *e = json_array_get_element(pa, i);
        if (JSON_NODE_HOLDS_VALUE(e) && json_node_get_value_type(e) == G_TYPE_STRING)
            g_ptr_array_add(perms, g_strdup(json_node_get_string(e)));
    }
    g_ptr_array_add(perms, NULL);
    a->permissions = (char **)g_ptr_array_free(perms, FALSE);
    return a;
}

GPtrArray *zyl_store_catalog_parse(const char *json, gssize len, const char *catalog_url) {
    JsonParser *p = json_parser_new();
    GPtrArray *out = NULL;
    if (json && json_parser_load_from_data(p, json, len, NULL) &&
        JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))) {
        JsonObject *root = json_node_get_object(json_parser_get_root(p));
        JsonNode *an = json_object_has_member(root, "apps") ? json_object_get_member(root, "apps") : NULL;
        if (an && JSON_NODE_HOLDS_ARRAY(an)) {
            JsonArray *apps = json_node_get_array(an);
            out = g_ptr_array_new_with_free_func(zyl_store_app_free);
            GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
            for (guint i = 0; i < json_array_get_length(apps); i++) {
                JsonNode *e = json_array_get_element(apps, i);
                ZylStoreApp *a = JSON_NODE_HOLDS_OBJECT(e) ? entry(json_node_get_object(e), catalog_url) : NULL;
                if (!a) continue;
                if (g_hash_table_contains(seen, a->id)) { zyl_store_app_free(a); continue; }   /* first wins */
                g_hash_table_add(seen, a->id);
                g_ptr_array_add(out, a);
            }
            g_hash_table_destroy(seen);
        }
    }
    g_object_unref(p);
    return out;
}
