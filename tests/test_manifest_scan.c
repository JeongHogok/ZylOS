/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 실기기 앱 디렉토리 스캔 테스트 — 시스템 앱 ID 섀도잉 방지
 * 수행범위: zyl_manifest_scan_dir()·zyl_manifest_parse() (실제 manifest.c), 시스템/사용자
 *           디렉토리, 잘못된 app.json 거부
 * 의존방향: runtime/wam/src/manifest/manifest.c, glib, json-glib
 * SOLID: SRP — 매니페스트 등록 정책만 검증
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../runtime/wam/src/manifest/manifest.c"

#define ROOT "/tmp/zyl-manifest-scan-test"

static void write_app(const char *dir, const char *name, const char *id,
                      const char *perms) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/%s/app.json", dir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "{\"id\":\"%s\",\"name\":\"%s\",\"version\":\"1.0.0\","
               "\"entry\":\"index.html\",\"permissions\":[%s]}", id, name, perms);
    fclose(f);
}

static void reset(void) {
    (void)system("rm -rf " ROOT);
    mkdir(ROOT, 0755);
    mkdir(ROOT "/system", 0755);
    mkdir(ROOT "/user", 0755);
}

static GHashTable *scan(void) {
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                          zyl_manifest_free);
    /* Same order as wam.c: system partition first, user directory second */
    zyl_manifest_scan_dir(t, ROOT "/system", TRUE);
    zyl_manifest_scan_dir(t, ROOT "/user", FALSE);
    return t;
}

/* A user-installed app reusing a system app's id must not replace it. Before
   the fix the later insert won, leaving com.zylos.settings pointing at the
   impostor (is_system FALSE) and the real settings app unreachable. */
static void test_user_app_cannot_shadow_a_system_app(void) {
    reset();
    write_app(ROOT "/system", "settings", "com.zylos.settings", "");
    write_app(ROOT "/user", "evil", "com.zylos.settings", "\"camera\"");
    GHashTable *t = scan();
    ZylAppManifest *m = g_hash_table_lookup(t, "com.zylos.settings");
    assert(m != NULL);
    assert(m->is_system == TRUE);
    assert(strstr(m->base_path, "/system/") != NULL);
    g_hash_table_destroy(t);
}

static void test_ordinary_user_apps_still_register(void) {
    reset();
    write_app(ROOT "/system", "settings", "com.zylos.settings", "");
    write_app(ROOT "/user", "notes2", "com.example.notes", "\"storage\"");
    GHashTable *t = scan();
    ZylAppManifest *m = g_hash_table_lookup(t, "com.example.notes");
    assert(m != NULL);
    assert(m->is_system == FALSE);
    assert(g_hash_table_size(t) == 2);
    g_hash_table_destroy(t);
}

/* apps.getInstalled on a device: the app's own presentation fields, and
   "system" as the OS decided it — an app's app.json cannot claim it */
static void test_app_info_is_what_apps_see(void) {
    reset();
    write_app(ROOT "/system", "lockscreen", "com.zylos.lockscreen", "\"system\"");
    char path[256];
    snprintf(path, sizeof(path), ROOT "/user/notes");
    mkdir(path, 0755);
    FILE *f = fopen(ROOT "/user/notes/app.json", "w");
    fprintf(f, "{\"id\":\"com.example.notes\",\"name\":\"Notes\",\"version\":\"2.1.0\","
               "\"entry\":\"index.html\",\"type\":\"system\",\"color\":\"icon-teal\","
               "\"formFactors\":[\"phone\",\"tablet\"],\"permissions\":[\"storage\"]}");
    fclose(f);
    GHashTable *t = scan();
    GVariant *notes = g_variant_ref_sink(zyl_manifest_app_info(g_hash_table_lookup(t, "com.example.notes")));
    gboolean sys = TRUE, launcher = FALSE;
    const gchar *color = NULL, *key = NULL;
    assert(g_variant_lookup(notes, "system", "b", &sys) && !sys);          /* "type": "system" ignored */
    assert(g_variant_lookup(notes, "launcher", "b", &launcher) && launcher);
    assert(g_variant_lookup(notes, "color", "&s", &color) && strcmp(color, "icon-teal") == 0);
    assert(g_variant_lookup(notes, "nameKey", "&s", &key) && strcmp(key, "app.notes") == 0);
    GVariant *ff = g_variant_lookup_value(notes, "formFactors", G_VARIANT_TYPE("as"));
    assert(ff && g_variant_n_children(ff) == 2);
    g_variant_unref(ff);
    g_variant_unref(notes);
    /* System UI says so in its manifest and stays out of the launcher (read at
       scan time: the WAM's app switching uses it too) */
    g_hash_table_destroy(t);
    f = fopen(ROOT "/system/lockscreen/app.json", "w");
    fprintf(f, "{\"id\":\"com.zylos.lockscreen\",\"name\":\"Lock\",\"version\":\"1.0.0\","
               "\"entry\":\"index.html\",\"launcher\":false}");
    fclose(f);
    t = scan();
    assert(!((ZylAppManifest *)g_hash_table_lookup(t, "com.zylos.lockscreen"))->launcher);
    assert(((ZylAppManifest *)g_hash_table_lookup(t, "com.example.notes"))->launcher);
    GVariant *lock = g_variant_ref_sink(zyl_manifest_app_info(g_hash_table_lookup(t, "com.zylos.lockscreen")));
    assert(g_variant_lookup(lock, "launcher", "b", &launcher) && !launcher);
    assert(g_variant_lookup(lock, "system", "b", &sys) && sys);
    g_variant_unref(lock);
    g_hash_table_destroy(t);

    /* A third-party app saying it would hide from home, settings and recents —
       everywhere the user could remove it or turn its permissions off */
    f = fopen(ROOT "/user/notes/app.json", "w");
    fprintf(f, "{\"id\":\"com.example.notes\",\"name\":\"Notes\",\"version\":\"2.1.0\","
               "\"entry\":\"index.html\",\"launcher\":false}");
    fclose(f);
    t = scan();
    ZylAppManifest *hidden = g_hash_table_lookup(t, "com.example.notes");
    assert(zyl_manifest_in_launcher(hidden));
    notes = g_variant_ref_sink(zyl_manifest_app_info(hidden));
    assert(g_variant_lookup(notes, "launcher", "b", &launcher) && launcher);
    g_variant_unref(notes);
    assert(!zyl_manifest_in_launcher(g_hash_table_lookup(t, "com.zylos.lockscreen")));
    g_hash_table_destroy(t);
}

static ZylAppManifest *parse_raw(const char *json) {
    char path[256];
    reset();
    mkdir(ROOT "/user/raw", 0755);
    snprintf(path, sizeof(path), ROOT "/user/raw/app.json");
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(json, f);
    fclose(f);
    return zyl_manifest_parse(ROOT "/user/raw");
}

/* What the WAM refuses to register: no app without an id and a name */
static void test_broken_manifests_are_refused(void) {
    const char *bad[] = {
        "{not json", "", "[]", "\"just a string\"",
        "{\"name\":\"x\",\"version\":\"1\"}",           /* no id */
        "{\"id\":\"com.example.x\",\"version\":\"1\"}",  /* no name */
        "{\"id\":\"\",\"name\":\"x\"}",                  /* empty id */
        NULL };
    for (int i = 0; bad[i]; i++) {
        ZylAppManifest *m = parse_raw(bad[i]);
        if (m) { printf("\n    accepted: %s\n", bad[i]); assert(0); }
    }
}

static void test_unknown_fields_ignored_unicode_kept(void) {
    ZylAppManifest *m = parse_raw("{\"id\":\"com.example.memo\",\"name\":\"메모 帳\","
                                  "\"version\":\"1.0.0\",\"entry\":\"index.html\","
                                  "\"future\":{\"x\":1}}");
    assert(m);
    assert(strcmp(m->id, "com.example.memo") == 0);
    assert(strcmp(m->name, "메모 帳") == 0);
    zyl_manifest_free(m);
}

/* An app's app.json with members of another type: the defaults, not NULL —
   a NULL mimeType ended the NULL-terminated list early, out of step with the
   actions read beside it (runtime/lib/zyl_json.h) */
static void test_members_of_another_type_are_the_defaults(void) {
    ZylAppManifest *m = parse_raw("{\"id\":\"com.example.odd\",\"name\":\"Odd\",\"version\":2,"
                                  "\"entry\":[\"x\"],\"icon\":{},"
                                  "\"intents\":[{\"action\":\"VIEW\",\"mimeType\":3},"
                                  "{\"action\":7},{\"action\":\"SEND\",\"mimeType\":\"text/plain\"}]}");
    assert(m);
    assert(strcmp(m->version, "1.0.0") == 0 && strcmp(m->entry, "index.html") == 0 &&
           strcmp(m->icon, "icon.png") == 0);
    assert(m->intent_actions[0] && strcmp(m->intent_actions[0], "VIEW") == 0 &&
           strcmp(m->intent_mimes[0], "*/*") == 0);
    assert(m->intent_actions[1] && strcmp(m->intent_actions[1], "SEND") == 0 &&
           strcmp(m->intent_mimes[1], "text/plain") == 0);
    assert(!m->intent_actions[2] && !m->intent_mimes[2]);
    zyl_manifest_free(m);
    assert(!parse_raw("{\"id\":5,\"name\":\"x\"}"));          /* an id of another type is none */
}

int main(void) {
    printf("=== Zyl OS Manifest Scan Tests ===\n");
    test_user_app_cannot_shadow_a_system_app();
    printf("  test_user_app_cannot_shadow_a_system_app     PASS\n");
    test_ordinary_user_apps_still_register();
    printf("  test_ordinary_user_apps_still_register       PASS\n");
    test_app_info_is_what_apps_see();
    printf("  test_app_info_is_what_apps_see               PASS\n");
    test_broken_manifests_are_refused();
    printf("  test_broken_manifests_are_refused            PASS\n");
    test_unknown_fields_ignored_unicode_kept();
    printf("  test_unknown_fields_ignored_unicode_kept     PASS\n");
    test_members_of_another_type_are_the_defaults();
    printf("  test_members_of_another_type_are_the_defaults PASS\n");
    (void)system("rm -rf " ROOT);
    printf("\nResults: 6/6 passed\n");
    return 0;
}
