/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Service
 *
 * 역할: 앱 봉인 판정 (app_seal.h)
 * 수행범위: 봉인·인증서·프로필 서명, 개발 빌드 허가(앱·기기·만료·폐기),
 *           파일 집합과 해시
 * 의존방향: app_seal.h, ota_verify.h, json-glib
 * SOLID: SRP — 봉인 판정만
 * ────────────────────────────────────────────────────────── */

#include "app_seal.h"
#include "ota_verify.h"

#include <string.h>
#include <sys/stat.h>
#include <json-glib/json-glib.h>

struct ZylSeal {
    char       *id;
    GHashTable *files;       /* relative path → sha256 hex */
    char       *developer;   /* NULL: the store's own */
};

static char *seal_path(const char *app_dir, const char *name) {
    return g_build_filename(app_dir, ZYL_SEAL_DIR, name, NULL);
}

static char *sha256_hex(const void *data, gsize len) {
    return g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, len);
}

static char *sha256_file(const char *path) {
    gchar *data = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &data, &len, NULL)) return NULL;
    char *hex = sha256_hex(data, len);
    g_free(data);
    return hex;
}

/* <name>.json signed (<name>.sig) by pem */
static bool signed_by(const char *app_dir, const char *name, const char *pem) {
    char *json_name = g_strconcat(name, ".json", NULL), *sig_name = g_strconcat(name, ".sig", NULL);
    char *json = seal_path(app_dir, json_name), *sigf = seal_path(app_dir, sig_name);
    gchar *sig = NULL;
    bool ok = pem && g_file_get_contents(sigf, &sig, NULL, NULL) &&
              zyl_ota_verify_pem(json, NULL, g_strstrip(sig), pem) == ZYL_OTA_TRUSTED;
    g_free(sig);
    g_free(json);
    g_free(sigf);
    g_free(json_name);
    g_free(sig_name);
    return ok;
}

static JsonObject *load_object(const char *path, JsonParser **keep) {
    *keep = json_parser_new();
    if (json_parser_load_from_file(*keep, path, NULL) && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(*keep)))
        return json_node_get_object(json_parser_get_root(*keep));
    return NULL;
}

static const char *text(JsonObject *o, const char *name) {
    JsonNode *n = o && json_object_has_member(o, name) ? json_object_get_member(o, name) : NULL;
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
           ? json_node_get_string(n) : NULL;
}

static gint64 number(JsonObject *o, const char *name) {
    JsonNode *n = o && json_object_has_member(o, name) ? json_object_get_member(o, name) : NULL;
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_INT64
           ? json_node_get_int(n) : 0;
}

static JsonArray *array(JsonObject *o, const char *name) {
    JsonNode *n = o && json_object_has_member(o, name) ? json_object_get_member(o, name) : NULL;
    return n && JSON_NODE_HOLDS_ARRAY(n) ? json_node_get_array(n) : NULL;
}

static bool array_has(JsonArray *a, const char *want) {
    for (guint i = 0; a && i < json_array_get_length(a); i++) {
        JsonNode *e = json_array_get_element(a, i);
        if (JSON_NODE_HOLDS_VALUE(e) && json_node_get_value_type(e) == G_TYPE_STRING &&
            strcmp(json_node_get_string(e), want) == 0) return true;
    }
    return false;
}

/* An exact id, or a prefix "com.acme.*" (any id under com.acme.) */
static bool app_allowed(JsonArray *patterns, const char *id) {
    for (guint i = 0; patterns && i < json_array_get_length(patterns); i++) {
        JsonNode *e = json_array_get_element(patterns, i);
        if (!JSON_NODE_HOLDS_VALUE(e) || json_node_get_value_type(e) != G_TYPE_STRING) continue;
        const char *p = json_node_get_string(e);
        size_t n = strlen(p);
        if (strcmp(p, id) == 0) return true;
        if (n > 2 && strcmp(p + n - 2, ".*") == 0 && strncmp(id, p, n - 1) == 0 && id[n - 1]) return true;
    }
    return false;
}

static bool revoked_has(const char *const *revoked, const char *fp) {
    for (int i = 0; revoked && revoked[i]; i++)
        if (g_ascii_strcasecmp(revoked[i], fp) == 0) return true;
    return false;
}

/* Every regular file under dir (but the seal itself) is sealed with its hash */
static bool walk_matches(const char *root, const char *rel_dir, GHashTable *files, guint *seen) {
    char *dir = rel_dir ? g_build_filename(root, rel_dir, NULL) : g_strdup(root);
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *name;
    bool ok = d != NULL;
    while (ok && (name = g_dir_read_name(d))) {
        if (!rel_dir && strcmp(name, ZYL_SEAL_DIR) == 0) continue;
        char *rel = rel_dir ? g_strconcat(rel_dir, "/", name, NULL) : g_strdup(name);
        char *path = g_build_filename(root, rel, NULL);
        struct stat st;
        if (lstat(path, &st) != 0) ok = false;
        else if (S_ISDIR(st.st_mode)) ok = walk_matches(root, rel, files, seen);
        else if (!S_ISREG(st.st_mode)) ok = false;           /* a link could point anywhere */
        else {
            const char *want = g_hash_table_lookup(files, rel);
            char *have = want ? sha256_file(path) : NULL;
            ok = have && strcmp(have, want) == 0;
            if (ok) (*seen)++;
            g_free(have);
        }
        g_free(path);
        g_free(rel);
    }
    if (d) g_dir_close(d);
    g_free(dir);
    return ok;
}

ZylSealVerdict zyl_seal_open(const char *app_dir, const char *app_id, const char *version,
                             const char *store_pem, const char *device_id,
                             const char *const *revoked, gint64 now, bool full, ZylSeal **out) {
    if (out) *out = NULL;
    char *seal_json = seal_path(app_dir, "seal.json");
    JsonParser *sp = NULL, *cp = NULL, *pp = NULL;
    JsonObject *seal = load_object(seal_json, &sp);
    JsonObject *cert = NULL, *prof = NULL;
    const char *pem = store_pem;
    char *developer = NULL, *cert_json = seal_path(app_dir, "cert.json");
    ZylSealVerdict v = ZYL_SEAL_OK;

    JsonNode *fn = seal && json_object_has_member(seal, "files") ? json_object_get_member(seal, "files") : NULL;
    if (!seal || !fn || !JSON_NODE_HOLDS_OBJECT(fn)) v = ZYL_SEAL_MISSING;
    else if (g_strcmp0(text(seal, "id"), app_id) != 0 ||
             (version && g_strcmp0(text(seal, "appVersion"), version) != 0)) v = ZYL_SEAL_WRONG_APP;
    else if (g_file_test(cert_json, G_FILE_TEST_EXISTS)) {
        /* A development build: the store vouches for the developer and says where it may run */
        char *prof_json = seal_path(app_dir, "profile.json");
        char *fp = sha256_file(cert_json);
        cert = load_object(cert_json, &cp);
        prof = load_object(prof_json, &pp);
        if (!cert || !prof || !signed_by(app_dir, "cert", store_pem) || !signed_by(app_dir, "profile", store_pem))
            v = ZYL_SEAL_BAD_SIGNATURE;
        else if (revoked_has(revoked, fp)) v = ZYL_SEAL_REVOKED;
        else if (number(cert, "notAfter") < now || number(prof, "notAfter") < now) v = ZYL_SEAL_EXPIRED;
        else if (g_strcmp0(text(prof, "cert"), fp) != 0 || !app_allowed(array(prof, "apps"), app_id) ||
                 !device_id || !*device_id || !array_has(array(prof, "devices"), device_id))
            v = ZYL_SEAL_NOT_PROVISIONED;
        else {
            pem = text(cert, "publicKey");
            developer = g_strdup(text(cert, "developer"));
        }
        g_free(fp);
        g_free(prof_json);
    }
    if (v == ZYL_SEAL_OK && !signed_by(app_dir, "seal", pem)) v = ZYL_SEAL_BAD_SIGNATURE;

    ZylSeal *s = NULL;
    if (v == ZYL_SEAL_OK) {
        s = g_new0(ZylSeal, 1);
        s->id = g_strdup(app_id);
        s->developer = developer;
        developer = NULL;
        s->files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        JsonObject *files = json_node_get_object(fn);
        GList *names = json_object_get_members(files);
        for (GList *l = names; l; l = l->next) {
            const char *h = text(files, l->data);
            if (h && strlen(h) == 64 && !strstr(l->data, "..") && ((char *)l->data)[0] != '/')
                g_hash_table_insert(s->files, g_strdup(l->data), g_ascii_strdown(h, -1));
            else v = ZYL_SEAL_BAD_SIGNATURE;        /* malformed even though signed */
        }
        g_list_free(names);
        guint seen = 0;
        if (v == ZYL_SEAL_OK && full &&
            (!walk_matches(app_dir, NULL, s->files, &seen) || seen != g_hash_table_size(s->files)))
            v = ZYL_SEAL_FILES_CHANGED;
    }
    if (v != ZYL_SEAL_OK) g_clear_pointer(&s, zyl_seal_free);
    if (out) *out = s;
    else zyl_seal_free(s);

    g_free(developer);
    g_free(cert_json);
    g_free(seal_json);
    g_object_unref(sp);
    if (cp) g_object_unref(cp);
    if (pp) g_object_unref(pp);
    return v;
}

bool zyl_seal_bytes_ok(const ZylSeal *s, const char *rel, const void *data, gsize len) {
    const char *want = s ? g_hash_table_lookup(s->files, rel) : NULL;
    if (!want) return false;
    char *have = sha256_hex(data, len);
    bool ok = strcmp(have, want) == 0;
    g_free(have);
    return ok;
}

const char *zyl_seal_developer(const ZylSeal *s) { return s ? s->developer : NULL; }

void zyl_seal_free(ZylSeal *s) {
    if (!s) return;
    g_free(s->id);
    g_free(s->developer);
    if (s->files) g_hash_table_unref(s->files);
    g_free(s);
}

const char *zyl_seal_verdict_message(ZylSealVerdict v) {
    switch (v) {
    case ZYL_SEAL_OK:              return "sealed";
    case ZYL_SEAL_MISSING:         return "not sealed";
    case ZYL_SEAL_BAD_SIGNATURE:   return "not signed by the store or an allowed developer";
    case ZYL_SEAL_WRONG_APP:       return "sealed for another app or version";
    case ZYL_SEAL_FILES_CHANGED:   return "files changed after sealing";
    case ZYL_SEAL_NOT_PROVISIONED: return "development build not provisioned for this app or device";
    case ZYL_SEAL_EXPIRED:         return "development certificate or profile expired";
    case ZYL_SEAL_REVOKED:         return "developer certificate revoked";
    }
    return "unknown";
}

char *zyl_seal_store_key(const char *config_path) {
    JsonParser *p = NULL;
    JsonObject *o = load_object(config_path, &p);
    const char *key = text(o, "publicKey");
    char *out = key && *key ? g_strdup(key) : NULL;
    g_object_unref(p);
    return out;
}

char **zyl_seal_load_revoked(const char *path) {
    gchar *data = NULL;
    GPtrArray *out = g_ptr_array_new();
    if (g_file_get_contents(path, &data, NULL, NULL)) {
        gchar **lines = g_strsplit(data, "\n", -1);
        for (gchar **l = lines; *l; l++) {
            char *t = g_strstrip(*l);
            if (strlen(t) == 64) g_ptr_array_add(out, g_ascii_strdown(t, -1));
        }
        g_strfreev(lines);
        g_free(data);
    }
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}
