/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Service
 *
 * 역할: 앱 패키지 풀기와 확인 (store_package.h)
 * 수행범위: zip → 디렉터리(안전하게), app.json 확인
 * 의존방향: store_package.h, libzip, json-glib
 * SOLID: SRP — 패키지 내용 처리만
 * ────────────────────────────────────────────────────────── */

#include "store_package.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include "zyl_json.h"
#include <string.h>
#include <sys/stat.h>
#include <zip.h>

/* A relative path of plain names: no leading /, no . or .. parts, no \ */
static bool safe_name(const char *name) {
    if (!name || !*name || name[0] == '/' || strchr(name, '\\')) return false;
    gchar **parts = g_strsplit(name, "/", -1);
    bool ok = true;
    guint n = g_strv_length(parts);
    for (guint i = 0; i < n && ok; i++) {
        bool last = i + 1 == n;
        if (strcmp(parts[i], "..") == 0 || strcmp(parts[i], ".") == 0) ok = false;
        else if (!*parts[i] && !last) ok = false;          /* a//b */
    }
    g_strfreev(parts);
    return ok;
}

bool zyl_store_unpack(const char *zip_path, const char *dir, const char **why) {
    int err = 0;
    zip_t *za = zip_open(zip_path, ZIP_RDONLY | ZIP_CHECKCONS, &err);
    if (!za) { *why = "not a package"; return false; }
    zip_int64_t n = zip_get_num_entries(za, 0);
    bool ok = n > 0 && n <= ZYL_STORE_MAX_FILES;
    if (!ok) *why = n > 0 ? "too many files" : "empty package";
    guint64 total = 0;
    for (zip_int64_t i = 0; ok && i < n; i++) {
        zip_stat_t st;
        zip_uint8_t opsys;
        zip_uint32_t attr;
        if (zip_stat_index(za, (zip_uint64_t)i, 0, &st) != 0 || !(st.valid & ZIP_STAT_NAME) ||
            !safe_name(st.name)) { ok = false; *why = "unsafe path in package"; break; }
        /* Only plain files and folders: a link could point anywhere once unpacked */
        if (zip_file_get_external_attributes(za, (zip_uint64_t)i, 0, &opsys, &attr) == 0 &&
            opsys == ZIP_OPSYS_UNIX) {
            mode_t mode = (mode_t)(attr >> 16);
            if (mode && !S_ISREG(mode) && !S_ISDIR(mode)) { ok = false; *why = "link or device in package"; break; }
        }
        char *path = g_build_filename(dir, st.name, NULL);
        if (g_str_has_suffix(st.name, "/")) {
            ok = g_mkdir_with_parents(path, 0755) == 0;
            if (!ok) *why = "cannot unpack";
            g_free(path);
            continue;
        }
        total += st.size;
        if (total > ZYL_STORE_MAX_UNPACKED) { ok = false; *why = "package too large"; g_free(path); break; }
        char *parent = g_path_get_dirname(path);
        zip_file_t *zf = g_mkdir_with_parents(parent, 0755) == 0 ? zip_fopen_index(za, (zip_uint64_t)i, 0) : NULL;
        FILE *out = zf ? fopen(path, "wbx") : NULL;   /* never through an existing file */
        char buf[16384];
        zip_int64_t got;
        guint64 written = 0;
        while (out && (got = zip_fread(zf, buf, sizeof(buf))) > 0) {
            written += (guint64)got;
            if (written > st.size || fwrite(buf, 1, (size_t)got, out) != (size_t)got) { got = -1; break; }
        }
        ok = out && got == 0 && written == st.size;
        if (out) ok = (fclose(out) == 0) && ok;
        if (zf) zip_fclose(zf);
        if (ok) g_chmod(path, 0644);
        else *why = "cannot unpack";
        g_free(parent);
        g_free(path);
    }
    zip_discard(za);
    return ok;
}

bool zyl_store_check_manifest(const char *dir, const char *id, const char *version, const char **why) {
    char *path = g_build_filename(dir, "app.json", NULL);
    JsonParser *p = json_parser_new();
    const char *reason = "no app.json";
    if (json_parser_load_from_file(p, path, NULL) && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))) {
        JsonObject *o = json_node_get_object(json_parser_get_root(p));
        const char *mid = zyl_json_str(o, "id", "");
        const char *mver = zyl_json_str(o, "version", "");
        const char *entry = zyl_json_str(o, "entry", "index.html");
        char *entry_path = g_build_filename(dir, entry, NULL);
        if (strcmp(mid, id) != 0) reason = "package is a different app";
        else if (strcmp(mver, version) != 0) reason = "package is a different version";
        else if (!safe_name(entry) || !g_file_test(entry_path, G_FILE_TEST_IS_REGULAR)) reason = "no entry page";
        else reason = NULL;
        g_free(entry_path);
    }
    g_object_unref(p);
    g_free(path);
    if (reason) *why = reason;     /* only on failure: callers keep their own on success */
    return reason == NULL;
}
