/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 설정 서비스(runtime/services/settings) 단위 테스트
 * 수행범위: 기본값 위 사용자 값 겹치기, 변경분만 저장, 재시작 후 유지, 기본값
 *           변경 반영, 손상 파일 보존, 값 타입 왕복(JSON → v → a{sv} → JSON),
 *           없는 카테고리는 null, OS 기본값 파일이 유효한 객체인지
 * 의존방향: runtime/services/settings/settings.c (ZYL_TEST), gio, json-glib
 * SOLID: SRP — 설정 저장 계약 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-settings-test"
#define ZYL_SETTINGS_DEFAULTS DIR_ "/defaults.json"
#define ZYL_SETTINGS_FILE     DIR_ "/data/settings.json"
#include <assert.h>
#include "../runtime/services/settings/settings.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void reset(void) {
    g_assert_true(system("rm -rf " DIR_ " && mkdir -p " DIR_) == 0);
    g_file_set_contents(ZYL_SETTINGS_DEFAULTS,
        "{\"display\":{\"brightness\":80,\"darkMode\":true},\"wifi\":{\"enabled\":true}}", -1, NULL);
    settings_load();
}

static JsonNode *parse(const char *json) {
    JsonParser *p = json_parser_new();
    g_assert_true(json_parser_load_from_data(p, json, -1, NULL));
    JsonNode *n = json_node_copy(json_parser_get_root(p));
    g_object_unref(p);
    return n;
}

static char *dump(JsonObject *o) {
    JsonNode *n = json_node_init_object(json_node_alloc(), o);
    char *s = json_to_string(n, FALSE);
    json_node_unref(n);
    return s;
}

static void flush(void) {
    if (g_save_source) { g_source_remove(g_save_source); g_save_source = 0; }
    g_assert_true(settings_save());
}

static void test_defaults_are_served_unknown_is_null(void) {
    JsonObject *d = settings_get("display");
    assert(json_object_get_int_member(d, "brightness") == 80);
    json_object_unref(d);
    assert(settings_get("never_written") == NULL);
}

static void test_user_value_overrides_and_only_changes_are_saved(void) {
    json_object_unref(settings_update("display", "brightness", parse("35")));
    flush();
    char *saved = NULL;
    g_assert_true(g_file_get_contents(ZYL_SETTINGS_FILE, &saved, NULL, NULL));
    assert(strstr(saved, "35") && !strstr(saved, "darkMode"));   /* defaults stay out */
    g_free(saved);
    settings_load();                                             /* restart */
    JsonObject *d = settings_get("display");
    assert(json_object_get_int_member(d, "brightness") == 35);
    assert(json_object_get_boolean_member(d, "darkMode"));
    json_object_unref(d);
}

static void test_new_default_reaches_untouched_keys(void) {
    json_object_unref(settings_update("display", "brightness", parse("35")));
    flush();
    g_file_set_contents(ZYL_SETTINGS_DEFAULTS,
        "{\"display\":{\"brightness\":90,\"darkMode\":false}}", -1, NULL);   /* OS update */
    settings_load();
    JsonObject *d = settings_get("display");
    assert(json_object_get_int_member(d, "brightness") == 35);          /* user's choice */
    assert(!json_object_get_boolean_member(d, "darkMode"));             /* new default */
    json_object_unref(d);
}

static void test_corrupt_file_is_kept_and_defaults_continue(void) {
    g_mkdir_with_parents(DIR_ "/data", 0700);
    g_file_set_contents(ZYL_SETTINGS_FILE, "{ not json", -1, NULL);
    settings_load();
    JsonObject *d = settings_get("display");
    assert(json_object_get_int_member(d, "brightness") == 80);
    json_object_unref(d);
    assert(g_file_test(ZYL_SETTINGS_FILE ".corrupt", G_FILE_TEST_EXISTS));
}

/* What the WAM bridge does: JSON value → 'v' (json_gvariant_deserialize),
   the daemon stores it, and the reply a{sv} becomes JSON again */
static void test_values_round_trip_through_dbus_types(void) {
    const char *cases[] = { "\"default\"", "7", "2.5", "true", "[\"a\",\"b\"]",
                            "{\"list\":[1,2],\"name\":\"x\"}", NULL };
    for (int i = 0; cases[i]; i++) {
        JsonNode *in = parse(cases[i]);
        GVariant *v = json_gvariant_deserialize(in, "v", NULL);
        assert(v);
        GVariant *inner = g_variant_get_variant(v);
        JsonObject *cat = settings_update("roundtrip", "k", json_gvariant_serialize(inner));
        GVariant *reply = g_variant_ref_sink(object_variant(cat));
        JsonNode *back = json_gvariant_serialize(reply);
        JsonNode *got = json_object_get_member(json_node_get_object(back), "k");
        char *a = json_to_string(in, FALSE), *b = json_to_string(got, FALSE);
        if (strcmp(a, b) != 0) { printf("\n    %s came back as %s\n", a, b); assert(0); }
        g_free(a); g_free(b);
        json_node_unref(back); g_variant_unref(reply); json_object_unref(cat);
        g_variant_unref(inner); g_variant_unref(v); json_node_unref(in);
    }
}

static void test_os_defaults_file_is_a_settings_object(void) {
    JsonParser *p = json_parser_new();
    g_assert_true(json_parser_load_from_file(p, "system/settings-defaults.json", NULL));
    JsonNode *root = json_parser_get_root(p);
    assert(JSON_NODE_HOLDS_OBJECT(root));
    char *s = dump(json_node_get_object(root));
    assert(strstr(s, "\"display\""));
    g_free(s);
    g_object_unref(p);
}

int main(void) {
    printf("=== Zyl OS Settings Tests ===\n");
    RUN_TEST(test_defaults_are_served_unknown_is_null);
    RUN_TEST(test_user_value_overrides_and_only_changes_are_saved);
    RUN_TEST(test_new_default_reaches_untouched_keys);
    RUN_TEST(test_corrupt_file_is_kept_and_defaults_continue);
    RUN_TEST(test_values_round_trip_through_dbus_types);
    RUN_TEST(test_os_defaults_file_is_a_settings_object);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    (void)settings_vtable; (void)settings_introspection_xml;
    return tests_pass == tests_run ? 0 : 1;
}
