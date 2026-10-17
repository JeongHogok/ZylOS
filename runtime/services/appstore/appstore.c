/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 앱스토어 서비스 (org.zylos.AppStore) — 스토어 서버에서 앱을 받아
 *       설치·업데이트하고, 지울 때 앱이 만든 것을 남기지 않는다
 * 수행범위: 카탈로그(signing.json — 루트 키가 서명한 서명 키 인증서, 폐기되지 않은
 *           일련번호 → catalog.json + .sig — 그 서명 키 서명 검증, 앞서 본 것보다 오래된
 *           카탈로그는 거부(되돌리기), 설치 상태·업데이트 표시), 설치(패키지 다운로드 →
 *           패키지 전체의 SHA-256 과
 *           서명 → 안전하게 풀기 → app.json 이 카탈로그와 같은 id·버전인지 →
 *           /data/apps/<id> 로 교체 → AppInstalled), 삭제(패키지 + 앱 컨테이너
 *           /data/appdata/<id> 통째로 → AppRemoved: WAM·알림·알람이 각자 정리).
 *           설치되는 앱은 봉인돼 있어야 한다(app_seal.c): 카탈로그 앱은 스토어 서명 키,
 *           InstallFile(사용자 파일의 .zpkg)은 그것 또는 스토어가 이 기기에 허락한
 *           개발자. 회수(폐기된 서명 키·개발자 인증서·앱)를 /data/apps/.revocations.json
 *           에 두고, 바뀌면 RevocationsChanged — WAM 이 앱을 다시 판정한다.
 *           시스템 앱은 대상이 아니다. 설정: /etc/zyl-os/store.json { url, rootKey }
 *           — 없으면 "unconfigured". 한 번에 작업 하나
 * 의존방향: gio (D-Bus, GSubprocess → curl), store_catalog.h, store_package.h,
 *           app_seal.h, container.h, ota_verify.h (서명: tools/build-store.py 가 만든 것),
 *           hal.h (기기 ID)
 * SOLID: SRP — 앱 설치·삭제 흐름만. 해석·풀기·검증은 각 모듈
 * ────────────────────────────────────────────────────────── */

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include "zyl_json.h"

#include "store_catalog.h"
#include "store_package.h"
#include "app_seal.h"
#include "hal.h"
#include "container.h"
#include "ota_verify.h"

#ifndef ZYL_STORE_CONFIG
#define ZYL_STORE_CONFIG "/etc/zyl-os/store.json"
#endif
#ifndef ZYL_APPS_DIR
#define ZYL_APPS_DIR "/data/apps"              /* WAM_USER_DIR */
#endif
#ifndef ZYL_SYSTEM_APPS_DIR
#define ZYL_SYSTEM_APPS_DIR "/usr/share/zyl-os/apps"
#endif
#ifndef ZYL_CONTAINERS_ROOT
#define ZYL_CONTAINERS_ROOT ZYL_CONTAINERS
#endif
#include "users.h"
#ifndef ZYL_FILES_ROOT
#define ZYL_FILES_ROOT ZYL_OWNER_FILES   /* where InstallFile's packages come from */
#endif
#define REVOCATIONS_FILE ZYL_APPS_DIR "/" ZYL_SEAL_REVOCATIONS
#define CATALOG_MAX (1024 * 1024)

/* ─── Config and what is on the device ─── */

typedef struct { char *url; char *root_pem; } StoreConfig;

static bool load_config(StoreConfig *c) {
    JsonParser *p = json_parser_new();
    c->url = c->root_pem = NULL;
    if (json_parser_load_from_file(p, ZYL_STORE_CONFIG, NULL) && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))) {
        JsonObject *o = json_node_get_object(json_parser_get_root(p));
        const char *url = zyl_json_str(o, "url", "");
        const char *key = zyl_json_str(o, "rootKey", "");
        if (url && g_str_has_prefix(url, "https://") && key && *key) {
            c->url = g_strdup(url);
            c->root_pem = g_strdup(key);
        }
    }
    g_object_unref(p);
    return c->url != NULL;
}

static void config_clear(StoreConfig *c) {
    g_clear_pointer(&c->url, g_free);
    g_clear_pointer(&c->root_pem, g_free);
}

/* The id an app.json in dir declares, or NULL */
static char *manifest_field(const char *dir, const char *field) {
    char *path = g_build_filename(dir, "app.json", NULL);
    JsonParser *p = json_parser_new();
    char *out = NULL;
    if (json_parser_load_from_file(p, path, NULL) && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))) {
        const char *v = zyl_json_str(
            json_node_get_object(json_parser_get_root(p)), field, NULL);
        out = g_strdup(v);
    }
    g_object_unref(p);
    g_free(path);
    return out;
}

/* System apps are the image's (their folder names differ from their ids) */
static bool is_system_app(const char *id) {
    GDir *d = g_dir_open(ZYL_SYSTEM_APPS_DIR, 0, NULL);
    const char *name;
    bool found = false;
    while (d && !found && (name = g_dir_read_name(d))) {
        char *dir = g_build_filename(ZYL_SYSTEM_APPS_DIR, name, NULL);
        char *mid = manifest_field(dir, "id");
        found = mid && strcmp(mid, id) == 0;
        g_free(mid);
        g_free(dir);
    }
    if (d) g_dir_close(d);
    return found;
}

/* com.zylos.* is the OS image's: an app taking such an id from the store would
   hand its container to the system app an update later ships under that id */
static bool reserved_id(const char *id) {
    return g_str_has_prefix(id, "com.zylos.");
}

static char *installed_version(const char *id) {
    char *dir = zyl_container_dir(ZYL_APPS_DIR, id, NULL, false);
    char *v = dir ? manifest_field(dir, "version") : NULL;
    g_free(dir);
    return v;
}

/* ─── Removal: the package and everything the app made ─── */

static bool remove_app(const char *id) {
    char *pkg = zyl_container_dir(ZYL_APPS_DIR, id, NULL, false);
    char *box = zyl_container_dir(ZYL_CONTAINERS_ROOT, id, NULL, false);
    bool ok = pkg && box && zyl_remove_tree(pkg) && zyl_remove_tree(box);
    g_free(pkg);
    g_free(box);
    return ok;
}

/* The unpacked app replaces what is installed (an update keeps the container) */
static bool swap_in(const char *work, const char *id) {
    char *target = zyl_container_dir(ZYL_APPS_DIR, id, NULL, false);
    char *fresh = g_build_filename(work, "app", NULL);
    char *old = g_build_filename(work, "old", NULL);
    bool had = g_file_test(target, G_FILE_TEST_EXISTS);
    bool ok = (!had || g_rename(target, old) == 0) && g_mkdir_with_parents(ZYL_APPS_DIR, 0750) == 0 &&
              g_rename(fresh, target) == 0;
    if (!ok && had && !g_file_test(target, G_FILE_TEST_EXISTS)) g_rename(old, target);
    g_free(target);
    g_free(fresh);
    g_free(old);
    return ok;
}

/* ─── The daemon's: installing and the D-Bus jobs (tests/store_e2e.sh runs
   them against the real daemon; the unit test includes what is above) ─── */
#ifndef ZYL_TEST
/* ─── Seals: only code signed as it is runs ─── */

/* NULL when the unpacked app in dir is sealed as required; else the reason.
   store_only: the catalog's apps must be the store's own, never a development build */
static const char *seal_reason(const char *dir, const char *id, const char *version,
                               const char *root_pem, bool store_only) {
    ZylBoardInfo board;
    memset(&board, 0, sizeof(board));
    zyl_hal_board()->get(&board);
    ZylRevocations *rev = zyl_seal_load_revocations(REVOCATIONS_FILE);
    ZylSeal *seal = NULL;
    ZylSealVerdict v = zyl_seal_open(dir, id, version, root_pem, board.device_id,
                                     rev, g_get_real_time() / G_USEC_PER_SEC, true, &seal);
    const char *why = NULL;
    if (v == ZYL_SEAL_NOT_PROVISIONED) why = "not_provisioned";
    else if (v == ZYL_SEAL_EXPIRED) why = "expired";
    else if (v == ZYL_SEAL_REVOKED) why = "revoked";
    else if (v != ZYL_SEAL_OK || (store_only && zyl_seal_developer(seal))) why = "bad_package";
    if (why) g_warning("[AppStore] %s: %s", id, zyl_seal_verdict_message(v));
    zyl_seal_free(seal);
    zyl_seal_revocations_free(rev);
    return why;
}

/* An update only from whoever signed what is installed: another signer's build
   under the same id — a development build over the store's app, or another
   developer's — would take over the app's container (its web storage, files,
   push topic). Removing the app first (which empties the container) is the way */
static bool signer_changed(const char *id, const char *new_dir) {
    char *installed = zyl_container_dir(ZYL_APPS_DIR, id, NULL, false);
    if (!installed || !g_file_test(installed, G_FILE_TEST_IS_DIR)) {
        g_free(installed);
        return false;                           /* a first install */
    }
    char *was = zyl_seal_signer_key(installed), *now = zyl_seal_signer_key(new_dir);
    bool changed = g_strcmp0(was, now) != 0;
    if (changed) g_warning("[AppStore] %s: signed by someone else than the installed app", id);
    g_free(was);
    g_free(now);
    g_free(installed);
    return changed;
}


/* A package from the user's files — a development build provisioned for this
   device, or the store's own. NULL (and *out_id) when installed, else the reason */
static const char *install_file(const char *rel, const char *root_pem, char **out_id) {
    char root[PATH_MAX], real[PATH_MAX];
    char *full = rel ? g_build_filename(ZYL_FILES_ROOT, rel, NULL) : NULL;
    char *work = NULL, *dir = NULL, *id = NULL, *version = NULL;
    const char *why = NULL, *detail = NULL;
    *out_id = NULL;
    /* Only the user's own files: a path cannot reach the system or another app */
    if (!full || strstr(rel, "..") || !realpath(ZYL_FILES_ROOT, root) || !realpath(full, real) ||
        strncmp(real, root, strlen(root)) != 0 || real[strlen(root)] != '/')
        why = "invalid_package";
    if (!why) {
        g_mkdir_with_parents(ZYL_APPS_DIR, 0750);
        work = g_build_filename(ZYL_APPS_DIR, ".staging-XXXXXX", NULL);
        if (!g_mkdtemp(work)) { g_clear_pointer(&work, g_free); why = "io"; }
    }
    if (!why) {
        dir = g_build_filename(work, "app", NULL);
        if (!zyl_store_unpack(real, dir, &detail)) why = "invalid_package";
    }
    if (!why) {
        id = manifest_field(dir, "id");
        version = manifest_field(dir, "version");
        if (!id || !version || !zyl_container_valid_id(id) || !strchr(id, '.') ||
            !zyl_store_check_manifest(dir, id, version, &detail)) why = "invalid_package";
    }
    if (!why && (is_system_app(id) || reserved_id(id))) why = "system_app";
    if (!why) {
        char *have = installed_version(id);
        if (have && zyl_store_version_cmp(have, version) > 0) why = "up_to_date";   /* never a downgrade */
        g_free(have);
    }
    if (!why) why = seal_reason(dir, id, version, root_pem, false);
    if (!why && signer_changed(id, dir)) why = "signer_changed";
    if (!why && !swap_in(work, id)) why = "io";
    if (!why) *out_id = g_strdup(id);
    if (detail && why) g_warning("[AppStore] %s: %s", rel ? rel : "", detail);
    if (work) zyl_remove_tree(work);
    g_free(work);
    g_free(dir);
    g_free(id);
    g_free(version);
    g_free(full);
    return why;
}


/* ─── Jobs: one at a time, each a chain of downloads ─── */

typedef enum { OP_CATALOG, OP_INSTALL } Op;
typedef struct Job Job;
typedef void (*Step)(Job *j, bool downloaded);

struct Job {
    Op                     op;
    GDBusMethodInvocation *inv;
    char                  *app_id;
    char                  *work;     /* staging dir under ZYL_APPS_DIR (same fs: rename) */
    StoreConfig            cfg;
    char                  *signing_pem;  /* the store key the root vouched for this time */
    char                 **revokes;      /* the signing serials its certificate revokes */
    GPtrArray             *catalog;      /* ZylStoreApp* */
    Step                   next;
};

static Job *g_job;
static GDBusConnection *g_conn;

static void job_end(Job *j) {
    if (j->work) zyl_remove_tree(j->work);
    g_free(j->work);
    g_free(j->app_id);
    config_clear(&j->cfg);
    g_free(j->signing_pem);
    g_strfreev(j->revokes);
    if (j->catalog) g_ptr_array_unref(j->catalog);
    g_free(j);
    g_job = NULL;
}

static GVariant *outcome(bool ok, const char *reason) {
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "ok", g_variant_new_boolean(ok));
    g_variant_builder_add(&d, "{sv}", "reason", g_variant_new_string(reason ? reason : ""));
    return g_variant_builder_end(&d);
}

/* A catalog request answers with a state; an install with ok/reason */
static void finish(Job *j, const char *reason) {
    if (j->op == OP_CATALOG) {
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "state", g_variant_new_string(reason));
        g_variant_builder_add(&d, "{sv}", "apps", g_variant_new_array(G_VARIANT_TYPE("a{sv}"), NULL, 0));
        g_dbus_method_invocation_return_value(j->inv, g_variant_new("(a{sv})", &d));
    } else {
        g_dbus_method_invocation_return_value(j->inv, g_variant_new("(@a{sv})", outcome(false, reason)));
    }
    job_end(j);
}

static void on_downloaded(GObject *src, GAsyncResult *res, gpointer data) {
    Job *j = data;
    bool ok = g_subprocess_wait_check_finish(G_SUBPROCESS(src), res, NULL);
    g_object_unref(src);
    j->next(j, ok);
}

/* https only, redirects only to https (integrity comes from the signatures) */
static void download(Job *j, const char *url, const char *dest, gint64 max, Step next) {
    char limit[32];
    snprintf(limit, sizeof(limit), "%" G_GINT64_FORMAT, max);
    j->next = next;
    GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
        "/usr/bin/curl", "-s", "-f", "-L", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "3",
        "--connect-timeout", "10", "-m", "300", "--max-filesize", limit, "-o", dest, "--", url, NULL);
    if (!p) { next(j, false); return; }
    g_subprocess_wait_check_async(p, NULL, on_downloaded, j);
}

static char *work_file(Job *j, const char *name) { return g_build_filename(j->work, name, NULL); }

static GVariant *app_variant(const ZylStoreApp *a) {
    char *have = installed_version(a->id);
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "id", g_variant_new_string(a->id));
    g_variant_builder_add(&d, "{sv}", "name", g_variant_new_string(a->name));
    g_variant_builder_add(&d, "{sv}", "version", g_variant_new_string(a->version));
    g_variant_builder_add(&d, "{sv}", "description", g_variant_new_string(a->description));
    g_variant_builder_add(&d, "{sv}", "developer", g_variant_new_string(a->developer));
    g_variant_builder_add(&d, "{sv}", "category", g_variant_new_string(a->category));
    g_variant_builder_add(&d, "{sv}", "size", g_variant_new_int64(a->size));
    g_variant_builder_add(&d, "{sv}", "permissions", g_variant_new_strv((const gchar *const *)a->permissions, -1));
    g_variant_builder_add(&d, "{sv}", "installed", g_variant_new_boolean(have != NULL));
    g_variant_builder_add(&d, "{sv}", "installedVersion", g_variant_new_string(have ? have : ""));
    g_variant_builder_add(&d, "{sv}", "update", g_variant_new_boolean(have && zyl_store_version_cmp(have, a->version) < 0));
    g_free(have);
    return g_variant_builder_end(&d);
}

/* ── install: after the catalog ── */
static void on_package(Job *j, bool downloaded) {
    ZylStoreApp *a = NULL;
    for (guint i = 0; i < j->catalog->len && !a; i++) {
        ZylStoreApp *e = g_ptr_array_index(j->catalog, i);
        if (strcmp(e->id, j->app_id) == 0) a = e;
    }
    char *pkg = work_file(j, "package"), *dir = work_file(j, "app");
    const char *why = NULL;
    if (!downloaded) why = "download_failed";
    else if (zyl_ota_verify_pem(pkg, a->sha256, a->signature, j->signing_pem) != ZYL_OTA_TRUSTED)
        why = "bad_package";                       /* tampered, corrupt, or not the store's */
    else if (!zyl_store_unpack(pkg, dir, &why) || !zyl_store_check_manifest(dir, a->id, a->version, &why)) {
        g_warning("[AppStore] %s: %s", a->id, why);
        why = "invalid_package";
    } else if (!(why = seal_reason(dir, a->id, a->version, j->cfg.root_pem, true))) {
        if (signer_changed(a->id, dir)) why = "signer_changed";
        else if (!swap_in(j->work, a->id)) why = "io";
    }
    g_free(pkg);
    g_free(dir);
    if (why) { finish(j, why); return; }
    g_message("[AppStore] installed %s %s", a->id, a->version);
    g_dbus_connection_emit_signal(g_conn, NULL, "/org/zylos/AppStore", "org.zylos.AppStore",
                                  "AppInstalled", g_variant_new("(s)", a->id), NULL);
    g_dbus_method_invocation_return_value(j->inv, g_variant_new("(@a{sv})", outcome(true, "")));
    job_end(j);
}

static void with_catalog(Job *j) {
    if (j->op == OP_CATALOG) {
        GVariantBuilder apps, d;
        g_variant_builder_init(&apps, G_VARIANT_TYPE("aa{sv}"));
        for (guint i = 0; i < j->catalog->len; i++)
            if (!is_system_app(((ZylStoreApp *)g_ptr_array_index(j->catalog, i))->id))
                g_variant_builder_add_value(&apps, app_variant(g_ptr_array_index(j->catalog, i)));
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "state", g_variant_new_string("ok"));
        g_variant_builder_add(&d, "{sv}", "apps", g_variant_builder_end(&apps));
        g_dbus_method_invocation_return_value(j->inv, g_variant_new("(a{sv})", &d));
        job_end(j);
        return;
    }
    ZylStoreApp *a = NULL;
    for (guint i = 0; i < j->catalog->len && !a; i++) {
        ZylStoreApp *e = g_ptr_array_index(j->catalog, i);
        if (strcmp(e->id, j->app_id) == 0) a = e;
    }
    if (!a) { finish(j, "not_in_store"); return; }
    if (is_system_app(a->id) || reserved_id(a->id)) { finish(j, "system_app"); return; }
    char *have = installed_version(a->id);
    bool current = have && zyl_store_version_cmp(have, a->version) >= 0;   /* never a downgrade */
    g_free(have);
    if (current) { finish(j, "up_to_date"); return; }
    char *pkg = work_file(j, "package");
    download(j, a->package_url, pkg, a->size, on_package);
    g_free(pkg);
}

/* The catalog's revocations replace the kept ones — unless it is older than
   what was seen (a replayed catalog would bring a revoked app back); revoked
   signing keys are added, never dropped. FALSE: refuse this catalog */
static bool keep_revocations(Job *j, const char *json, gsize len) {
    ZylRevocations *rev = zyl_seal_load_revocations(REVOCATIONS_FILE);
    gint64 seen = rev->issued;
    char **certs = g_strdupv(rev->certs), **apps = g_strdupv(rev->apps), **keys = g_strdupv(rev->keys);
    bool ok = zyl_store_catalog_revocations(json, (gssize)len, rev) && rev->issued >= seen;
    if (ok) {
        GPtrArray *all = g_ptr_array_new();
        for (int i = 0; rev->keys[i]; i++) g_ptr_array_add(all, g_strdup(rev->keys[i]));
        for (int i = 0; j->revokes && j->revokes[i]; i++)
            if (!g_strv_contains((const gchar *const *)rev->keys, j->revokes[i]))
                g_ptr_array_add(all, g_strdup(j->revokes[i]));
        g_ptr_array_add(all, NULL);
        g_strfreev(rev->keys);
        rev->keys = (char **)g_ptr_array_free(all, FALSE);
        g_mkdir_with_parents(ZYL_APPS_DIR, 0750);
        bool changed = !g_strv_equal((const gchar *const *)certs, (const gchar *const *)rev->certs) ||
                       !g_strv_equal((const gchar *const *)apps, (const gchar *const *)rev->apps) ||
                       !g_strv_equal((const gchar *const *)keys, (const gchar *const *)rev->keys);
        if (zyl_seal_save_revocations(REVOCATIONS_FILE, rev) && changed)
            g_dbus_connection_emit_signal(g_conn, NULL, "/org/zylos/AppStore", "org.zylos.AppStore",
                                          "RevocationsChanged", NULL, NULL);
    } else {
        g_warning("[AppStore] the catalog is older than one already seen, or has no issue time");
    }
    g_strfreev(certs);
    g_strfreev(apps);
    g_strfreev(keys);
    zyl_seal_revocations_free(rev);
    return ok;
}

static void on_catalog_sig(Job *j, bool downloaded) {
    char *cat = work_file(j, "catalog.json"), *sigf = work_file(j, "catalog.sig");
    gchar *sig = NULL, *json = NULL;
    gsize len = 0;
    const char *why = NULL;
    if (!downloaded || !g_file_get_contents(sigf, &sig, NULL, NULL)) why = "unreachable";
    else if (zyl_ota_verify_pem(cat, NULL, g_strstrip(sig), j->signing_pem) != ZYL_OTA_TRUSTED)
        why = "untrusted";                          /* not signed by the store's key */
    else if (!g_file_get_contents(cat, &json, &len, NULL) ||
             !(j->catalog = zyl_store_catalog_parse(json, (gssize)len, j->cfg.url)))
        why = "invalid";
    else if (!keep_revocations(j, json, len)) why = "untrusted";
    g_free(sig);
    g_free(json);
    g_free(cat);
    g_free(sigf);
    if (why) finish(j, why);
    else with_catalog(j);
}

static void on_catalog(Job *j, bool downloaded) {
    if (!downloaded) { finish(j, "unreachable"); return; }
    char *url = g_strconcat(j->cfg.url, ".sig", NULL);
    char *sigf = work_file(j, "catalog.sig");
    download(j, url, sigf, 4096, on_catalog_sig);
    g_free(url);
    g_free(sigf);
}

/* The root vouched for the store key that signs the catalog, and has not revoked it */
static void on_signing_sig(Job *j, bool downloaded) {
    if (!downloaded) { finish(j, "unreachable"); return; }
    ZylRevocations *rev = zyl_seal_load_revocations(REVOCATIONS_FILE);
    j->signing_pem = zyl_seal_signing_key(j->work, j->cfg.root_pem, rev, &j->revokes);
    zyl_seal_revocations_free(rev);
    if (!j->signing_pem) { finish(j, "untrusted"); return; }
    char *cat = work_file(j, "catalog.json");
    download(j, j->cfg.url, cat, CATALOG_MAX, on_catalog);
    g_free(cat);
}

static void on_signing(Job *j, bool downloaded) {
    char *url = zyl_store_resolve_url(j->cfg.url, "signing.json.sig");
    char *sigf = work_file(j, "signing.sig");
    if (!downloaded || !url) finish(j, "unreachable");
    else download(j, url, sigf, 4096, on_signing_sig);
    g_free(url);
    g_free(sigf);
}

static void start(Op op, const char *app_id, GDBusMethodInvocation *inv) {
    if (g_job) {
        if (op == OP_CATALOG)
            g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_BUSY, "busy");
        else
            g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{sv})", outcome(false, "busy")));
        return;
    }
    Job *j = g_new0(Job, 1);
    j->op = op;
    j->inv = inv;
    j->app_id = g_strdup(app_id);
    g_job = j;
    if (!load_config(&j->cfg)) { finish(j, "unconfigured"); return; }
    g_mkdir_with_parents(ZYL_APPS_DIR, 0750);
    j->work = g_build_filename(ZYL_APPS_DIR, ".staging-XXXXXX", NULL);
    if (!g_mkdtemp(j->work)) { g_clear_pointer(&j->work, g_free); finish(j, "io"); return; }
    char *url = zyl_store_resolve_url(j->cfg.url, "signing.json");
    char *signing = work_file(j, "signing.json");
    if (!url) finish(j, "unconfigured");
    else download(j, url, signing, 16384, on_signing);
    g_free(url);
    g_free(signing);
}

/* ─── D-Bus ─── */

static const char *appstore_xml =
    "<node>"
    "  <interface name='org.zylos.AppStore'>"
    "    <method name='GetCatalog'><arg type='a{sv}' name='catalog' direction='out'/></method>"
    "    <method name='Install'><arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='outcome' direction='out'/></method>"
    "    <method name='Uninstall'><arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{sv}' name='outcome' direction='out'/></method>"
    "    <method name='InstallFile'><arg type='s' name='path' direction='in'/>"
    "      <arg type='a{sv}' name='outcome' direction='out'/></method>"
    "    <signal name='AppInstalled'><arg type='s' name='app_id'/></signal>"
    "    <signal name='AppRemoved'><arg type='s' name='app_id'/></signal>"
    "    <signal name='RevocationsChanged'/>"
    "  </interface>"
    "</node>";

static void uninstall(const char *id, GDBusMethodInvocation *inv) {
    const char *why = NULL;
    char *have = zyl_container_valid_id(id) ? installed_version(id) : NULL;
    if (!zyl_container_valid_id(id) || is_system_app(id)) why = "system_app";
    else if (!have) why = "not_installed";
    else if (g_job) why = "busy";
    else {
        /* Closed first, so nothing of it is still writing into its container */
        GVariant *r = g_dbus_connection_call_sync(g_conn, "org.zylos.WebAppManager", "/org/zylos/WebAppManager",
            "org.zylos.WebAppManager", "Close", g_variant_new("(s)", id), NULL,
            G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL);
        if (r) g_variant_unref(r);
        if (!remove_app(id)) why = "io";
    }
    g_free(have);
    if (!why) {
        g_message("[AppStore] removed %s and its container", id);
        g_dbus_connection_emit_signal(g_conn, NULL, "/org/zylos/AppStore", "org.zylos.AppStore",
                                      "AppRemoved", g_variant_new("(s)", id), NULL);
    }
    g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{sv})", outcome(!why, why)));
}

/* A package the user has (Files → open): installed only if sealed as allowed */
static void install_from_file(const char *rel, GDBusMethodInvocation *inv) {
    StoreConfig cfg;
    char *id = NULL;
    const char *why = g_job ? "busy" : !load_config(&cfg) ? "unconfigured" : NULL;
    if (!why) {
        why = install_file(rel, cfg.root_pem, &id);
        config_clear(&cfg);
    }
    if (!why) {
        g_message("[AppStore] installed %s from a file", id);
        g_dbus_connection_emit_signal(g_conn, NULL, "/org/zylos/AppStore", "org.zylos.AppStore",
                                      "AppInstalled", g_variant_new("(s)", id), NULL);
    }
    GVariant *o = g_variant_ref_sink(outcome(!why, why));
    GVariantDict d;
    g_variant_dict_init(&d, o);
    g_variant_unref(o);
    g_variant_dict_insert(&d, "appId", "s", id ? id : "");
    g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{sv})", g_variant_dict_end(&d)));
    g_free(id);
}

static void handle(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    const gchar *id = NULL;
    if (g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) g_variant_get(params, "(&s)", &id);
    if (g_strcmp0(method, "GetCatalog") == 0) start(OP_CATALOG, NULL, inv);
    else if (g_strcmp0(method, "Install") == 0) start(OP_INSTALL, id, inv);
    else if (g_strcmp0(method, "Uninstall") == 0) uninstall(id, inv);
    else if (g_strcmp0(method, "InstallFile") == 0) install_from_file(id, inv);
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    g_conn = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(appstore_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/AppStore", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.AppStore", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
