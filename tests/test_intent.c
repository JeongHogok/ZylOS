/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 인텐트 해석(runtime/wam/src/intent) 단위 테스트
 * 수행범위: app.json "intents" 파싱(잘못된 항목은 건너뜀), MIME 일치 규칙,
 *           처리 앱 찾기(시작한 앱 제외, 정렬, 명시적 대상), 넘길 내용
 *           (sourceApp 은 요청 값이 아닌 OS 의 것), 메시지 스크립트
 * 의존방향: intent.c, manifest.c, json-glib
 * SOLID: SRP — apps/system/intent.js 와 같은 규칙인지 검증
 * ────────────────────────────────────────────────────────── */

#include "../runtime/wam/src/intent/intent.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)
#define ROOT_ "/tmp/zyl-intent-test"

static void add_app(GHashTable *t, const char *dir, const char *json) {
    char *d = g_build_filename(ROOT_, dir, NULL);
    char *f = g_build_filename(d, "app.json", NULL);
    g_mkdir_with_parents(d, 0755);
    assert(g_file_set_contents(f, json, -1, NULL));
    ZylAppManifest *m = zyl_manifest_parse(d);
    assert(m);
    g_hash_table_insert(t, m->id, m);
    g_free(f);
    g_free(d);
}

static GHashTable *apps(void) {
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, zyl_manifest_free);
    add_app(t, "gallery", "{\"id\":\"com.zylos.gallery\",\"name\":\"Gallery\",\"intents\":["
            "{\"action\":\"zyl.intent.action.VIEW\",\"mimeType\":\"image/*\"},"
            "\"junk\",{\"mimeType\":\"x/y\"}]}");
    add_app(t, "photos", "{\"id\":\"com.example.photos\",\"name\":\"Photos\",\"intents\":["
            "{\"action\":\"zyl.intent.action.VIEW\",\"mimeType\":\"image/png\"}]}");
    add_app(t, "files", "{\"id\":\"com.zylos.files\",\"name\":\"Files\"}");
    add_app(t, "any", "{\"id\":\"com.example.any\",\"name\":\"Any\",\"intents\":["
            "{\"action\":\"zyl.intent.action.SEND\"}]}");
    return t;
}

static JsonObject *intent(const char *json) {
    JsonNode *n = json_from_string(json, NULL);
    JsonObject *o = json_object_ref(json_node_get_object(n));
    json_node_unref(n);
    return o;
}

static char *ids(GPtrArray *a) {
    GString *s = g_string_new("");
    for (guint i = 0; i < a->len; i++) g_string_append_printf(s, "%s%s", i ? "," : "", (char *)g_ptr_array_index(a, i));
    g_ptr_array_unref(a);
    return g_string_free(s, FALSE);
}

static void expect(GHashTable *t, const char *source, const char *json, const char *want) {
    JsonObject *it = intent(json);
    char *got = ids(zyl_intent_resolve(t, source, it));
    if (strcmp(got, want) != 0) { printf("\n    %s: want [%s] got [%s]\n", json, want, got); assert(0); }
    g_free(got);
    json_object_unref(it);
}

static void test_manifest_intents_parsed_junk_skipped(void) {
    GHashTable *t = apps();
    ZylAppManifest *g = g_hash_table_lookup(t, "com.zylos.gallery");
    assert(g->intent_actions[0] && strcmp(g->intent_mimes[0], "image/*") == 0 && !g->intent_actions[1]);
    ZylAppManifest *a = g_hash_table_lookup(t, "com.example.any");
    assert(strcmp(a->intent_mimes[0], "*/*") == 0);                 /* default */
    ZylAppManifest *f = g_hash_table_lookup(t, "com.zylos.files");
    assert(f->intent_actions && !f->intent_actions[0]);
    g_hash_table_destroy(t);
}

static void test_mime_rules(void) {
    assert(zyl_intent_mime_matches("*/*", "image/png"));
    assert(zyl_intent_mime_matches("image/*", "image/png") && zyl_intent_mime_matches("image/*", "image/*"));
    assert(!zyl_intent_mime_matches("image/*", "imagex/png") && !zyl_intent_mime_matches("image/*", "video/mp4"));
    assert(zyl_intent_mime_matches("image/png", "image/png") && !zyl_intent_mime_matches("image/png", "image/*"));
    assert(!zyl_intent_mime_matches("/*", "x/y") && !zyl_intent_mime_matches(NULL, "x/y"));
}

static void test_who_handles_it(void) {
    GHashTable *t = apps();
    expect(t, "com.zylos.files", "{\"action\":\"zyl.intent.action.VIEW\",\"mimeType\":\"image/png\"}",
           "com.example.photos,com.zylos.gallery");                /* several, sorted */
    expect(t, "com.zylos.files", "{\"action\":\"zyl.intent.action.VIEW\",\"mimeType\":\"image/*\"}",
           "com.zylos.gallery");
    expect(t, "com.zylos.gallery", "{\"action\":\"zyl.intent.action.VIEW\",\"mimeType\":\"image/*\"}", "");  /* not itself */
    expect(t, "com.zylos.files", "{\"action\":\"zyl.intent.action.VIEW\",\"mimeType\":\"text/plain\"}", "");
    expect(t, "com.zylos.files", "{\"action\":\"zyl.intent.action.SEND\"}", "com.example.any");   /* no mime */
    expect(t, "com.zylos.files", "{\"targetApp\":\"com.zylos.gallery\"}", "com.zylos.gallery");
    expect(t, "com.zylos.files", "{\"targetApp\":\"com.nope\"}", "");
    expect(t, "com.zylos.files", "{}", "");
    g_hash_table_destroy(t);
}

static void test_deliverable_and_script(void) {
    JsonObject *it = intent("{\"action\":\"A\",\"data\":\"Pictures/a.png\",\"extras\":{\"n\":1},"
                            "\"sourceApp\":\"com.zylos.settings\",\"evil\":true}");
    JsonObject *d = zyl_intent_deliverable(it, "com.example.x");
    assert(strcmp(json_object_get_string_member(d, "sourceApp"), "com.example.x") == 0);   /* the OS's */
    assert(!json_object_has_member(d, "evil") && strcmp(json_object_get_string_member(d, "mimeType"), "") == 0);
    assert(json_object_get_int_member(json_object_get_object_member(d, "extras"), "n") == 1);
    char *s = zyl_intent_post_script("intent", zyl_intent_object_node(d));
    assert(g_str_has_prefix(s, "window.postMessage(\"{\\\"type\\\":\\\"intent\\\""));
    assert(strstr(s, "Pictures/a.png") && g_str_has_suffix(s, ",'*')"));
    g_free(s);
    json_object_unref(it);
    JsonObject *bare = intent("{\"action\":\"A\",\"extras\":\"no\"}");
    JsonObject *d2 = zyl_intent_deliverable(bare, NULL);
    assert(json_object_get_size(json_object_get_object_member(d2, "extras")) == 0);
    json_object_unref(d2);
    json_object_unref(bare);
}

int main(void) {
    printf("=== intent tests ===\n");
    assert(system("rm -rf " ROOT_) == 0);
    RUN_TEST(test_manifest_intents_parsed_junk_skipped);
    RUN_TEST(test_mime_rules);
    RUN_TEST(test_who_handles_it);
    RUN_TEST(test_deliverable_and_script);
    assert(system("rm -rf " ROOT_) == 0);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
