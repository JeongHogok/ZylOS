/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: app.json 매니페스트 파싱 단위 테스트
 * 수행범위: 유효한 매니페스트, 필수 필드 누락, 잘못된 JSON, 미지 필드 무시 검증
 * 의존방향: gio/gio.h (JSON 파싱용 json-glib 대신 GLib 기반)
 * SOLID: SRP — 매니페스트 파싱 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <json-glib/json-glib.h>

/* ── Test helpers ─────────────────────────────────────────── */

static int tests_run  = 0;
static int tests_pass = 0;

#define RUN_TEST(fn)                                        \
    do {                                                    \
        tests_run++;                                        \
        printf("  %-50s ", #fn);                            \
        fn();                                               \
        tests_pass++;                                       \
        printf("PASS\n");                                   \
    } while (0)

/* ── Manifest parser (mirrors expected app.json schema) ───── */

typedef struct {
    char *id;
    char *name;
    char *version;
    char *entry;
    char *icon;
    char *type;
} AppManifest;

static void app_manifest_free(AppManifest *m)
{
    if (!m) return;
    g_free(m->id);
    g_free(m->name);
    g_free(m->version);
    g_free(m->entry);
    g_free(m->icon);
    g_free(m->type);
}

/**
 * Parse a JSON string as an app manifest.
 * Returns TRUE on success, FALSE on failure.
 * On failure, error_msg is set (caller must g_free).
 */
static gboolean parse_manifest(const char   *json_str,
                                AppManifest  *out,
                                char        **error_msg)
{
    memset(out, 0, sizeof(*out));
    *error_msg = NULL;

    JsonParser *parser = json_parser_new();
    GError *err = NULL;

    if (!json_parser_load_from_data(parser, json_str, -1, &err)) {
        *error_msg = g_strdup_printf("JSON parse error: %s", err->message);
        g_error_free(err);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        *error_msg = g_strdup("Root is not a JSON object");
        g_object_unref(parser);
        return FALSE;
    }

    JsonObject *obj = json_node_get_object(root);

    /* Required fields */
    const char *required[] = { "id", "name", "version", "entry" };
    for (size_t i = 0; i < G_N_ELEMENTS(required); i++) {
        if (!json_object_has_member(obj, required[i])) {
            *error_msg = g_strdup_printf("Missing required field: %s", required[i]);
            g_object_unref(parser);
            return FALSE;
        }
    }

    out->id      = g_strdup(json_object_get_string_member(obj, "id"));
    out->name    = g_strdup(json_object_get_string_member(obj, "name"));
    out->version = g_strdup(json_object_get_string_member(obj, "version"));
    out->entry   = g_strdup(json_object_get_string_member(obj, "entry"));

    /* Optional fields */
    if (json_object_has_member(obj, "icon"))
        out->icon = g_strdup(json_object_get_string_member(obj, "icon"));
    if (json_object_has_member(obj, "type"))
        out->type = g_strdup(json_object_get_string_member(obj, "type"));

    g_object_unref(parser);
    return TRUE;
}

/* ── Test: Valid manifest with all fields ─────────────────── */

static void test_valid_manifest_all_fields(void)
{
    const char *json =
        "{"
        "  \"id\": \"com.zylos.home\","
        "  \"name\": \"Home\","
        "  \"version\": \"1.0.0\","
        "  \"entry\": \"index.html\","
        "  \"icon\": \"assets/home-icon.png\","
        "  \"permissions\": [\"app.list\", \"app.launch\"],"
        "  \"type\": \"system\","
        "  \"role\": \"homescreen\""
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == TRUE);
    assert(err == NULL);
    assert(strcmp(m.id, "com.zylos.home") == 0);
    assert(strcmp(m.name, "Home") == 0);
    assert(strcmp(m.version, "1.0.0") == 0);
    assert(strcmp(m.entry, "index.html") == 0);
    assert(strcmp(m.icon, "assets/home-icon.png") == 0);
    assert(strcmp(m.type, "system") == 0);

    app_manifest_free(&m);
}

/* ── Test: Minimal valid manifest (required fields only) ──── */

static void test_valid_manifest_minimal(void)
{
    const char *json =
        "{"
        "  \"id\": \"com.zylos.test\","
        "  \"name\": \"Test\","
        "  \"version\": \"0.1.0\","
        "  \"entry\": \"main.html\""
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == TRUE);
    assert(err == NULL);
    assert(strcmp(m.id, "com.zylos.test") == 0);
    assert(m.icon == NULL);
    assert(m.type == NULL);

    app_manifest_free(&m);
}

/* ── Test: Missing required field 'id' (should fail) ──────── */

static void test_missing_id(void)
{
    const char *json =
        "{"
        "  \"name\": \"Test\","
        "  \"version\": \"0.1.0\","
        "  \"entry\": \"main.html\""
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == FALSE);
    assert(err != NULL);
    assert(strstr(err, "id") != NULL);

    g_free(err);
    app_manifest_free(&m);
}

/* ── Test: Missing required field 'name' (should fail) ────── */

static void test_missing_name(void)
{
    const char *json =
        "{"
        "  \"id\": \"com.zylos.test\","
        "  \"version\": \"0.1.0\","
        "  \"entry\": \"main.html\""
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == FALSE);
    assert(err != NULL);
    assert(strstr(err, "name") != NULL);

    g_free(err);
    app_manifest_free(&m);
}

/* ── Test: Missing required field 'version' (should fail) ─── */

static void test_missing_version(void)
{
    const char *json =
        "{"
        "  \"id\": \"com.zylos.test\","
        "  \"name\": \"Test\","
        "  \"entry\": \"main.html\""
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == FALSE);
    assert(err != NULL);
    assert(strstr(err, "version") != NULL);

    g_free(err);
    app_manifest_free(&m);
}

/* ── Test: Missing required field 'entry' (should fail) ───── */

static void test_missing_entry(void)
{
    const char *json =
        "{"
        "  \"id\": \"com.zylos.test\","
        "  \"name\": \"Test\","
        "  \"version\": \"0.1.0\""
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == FALSE);
    assert(err != NULL);
    assert(strstr(err, "entry") != NULL);

    g_free(err);
    app_manifest_free(&m);
}

/* ── Test: Invalid JSON (should fail gracefully) ──────────── */

static void test_invalid_json(void)
{
    const char *json = "{ this is not valid JSON }}}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == FALSE);
    assert(err != NULL);

    g_free(err);
    app_manifest_free(&m);
}

/* ── Test: Empty string (should fail gracefully) ──────────── */

static void test_empty_json(void)
{
    const char *json = "";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == FALSE);
    assert(err != NULL);

    g_free(err);
    app_manifest_free(&m);
}

/* ── Test: JSON array instead of object (should fail) ─────── */

static void test_json_array_root(void)
{
    const char *json = "[1, 2, 3]";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == FALSE);
    assert(err != NULL);
    assert(strstr(err, "not a JSON object") != NULL);

    g_free(err);
    app_manifest_free(&m);
}

/* ── Test: Extra unknown fields (should be ignored) ───────── */

static void test_extra_unknown_fields(void)
{
    const char *json =
        "{"
        "  \"id\": \"com.zylos.test\","
        "  \"name\": \"Test\","
        "  \"version\": \"0.1.0\","
        "  \"entry\": \"main.html\","
        "  \"unknown_field\": \"should be ignored\","
        "  \"another_unknown\": 42,"
        "  \"nested_unknown\": { \"a\": 1 }"
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == TRUE);
    assert(err == NULL);
    assert(strcmp(m.id, "com.zylos.test") == 0);
    assert(strcmp(m.name, "Test") == 0);

    app_manifest_free(&m);
}

/* ── Test: Unicode in string fields ───────────────────────── */

static void test_unicode_fields(void)
{
    const char *json =
        "{"
        "  \"id\": \"com.zylos.hangul\","
        "  \"name\": \"\xED\x95\x9C\xEA\xB8\x80 \xEC\x95\xB1\","
        "  \"version\": \"1.0.0\","
        "  \"entry\": \"index.html\""
        "}";

    AppManifest m;
    char *err = NULL;
    gboolean ok = parse_manifest(json, &m, &err);

    assert(ok == TRUE);
    assert(err == NULL);
    assert(strcmp(m.name, "\xED\x95\x9C\xEA\xB8\x80 \xEC\x95\xB1") == 0);

    app_manifest_free(&m);
}

/* ── Main ─────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Zyl OS Manifest Parsing Tests ===\n");

    RUN_TEST(test_valid_manifest_all_fields);
    RUN_TEST(test_valid_manifest_minimal);
    RUN_TEST(test_missing_id);
    RUN_TEST(test_missing_name);
    RUN_TEST(test_missing_version);
    RUN_TEST(test_missing_entry);
    RUN_TEST(test_invalid_json);
    RUN_TEST(test_empty_json);
    RUN_TEST(test_json_array_root);
    RUN_TEST(test_extra_unknown_fields);
    RUN_TEST(test_unicode_fields);

    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
