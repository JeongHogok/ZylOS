/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 앱스토어 핵심 규칙 단위 테스트 (runtime/services/appstore)
 * 수행범위: 카탈로그 검증(잘못된 항목·https 아닌 주소·같은 id 건너뜀),
 *           버전 비교, 패키지 풀기(.. ·절대 경로·링크·크기 거부), app.json 이
 *           카탈로그와 같은 앱·버전인지, 교체 설치, 삭제 시 패키지와 컨테이너가
 *           모두 사라짐, 시스템 앱 판별
 * 의존방향: appstore.c (ZYL_TEST), store_catalog.c, store_package.c,
 *           container.c, ota_verify.c, libzip
 * SOLID: SRP — 스토어 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-store-test"
#define ZYL_STORE_CONFIG ROOT_ "/store.json"
#define ZYL_APPS_DIR ROOT_ "/apps"
#define ZYL_SYSTEM_APPS_DIR ROOT_ "/system"
#define ZYL_CONTAINERS_ROOT ROOT_ "/appdata"
#include "../runtime/services/appstore/appstore.c"
#include <assert.h>
#include <zip.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); assert(system("rm -rf " ROOT_) == 0); \
    g_mkdir_with_parents(ROOT_, 0755); fn(); tests_pass++; printf("PASS\n"); } while (0)

#define HEX64 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

static void put(const char *path, const char *text) {
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    assert(g_file_set_contents(path, text, -1, NULL));
    g_free(dir);
}

/* A zip with the given names (each holding "x"); mode for the last, 0 = plain */
static void make_zip(const char *path, const char *const *names, const char *app_json, mode_t last_mode) {
    int err;
    zip_t *z = zip_open(path, ZIP_CREATE | ZIP_TRUNCATE, &err);
    assert(z);
    for (int i = 0; names[i]; i++) {
        const char *body = strcmp(names[i], "app.json") == 0 ? app_json : "x";
        zip_source_t *s = zip_source_buffer(z, body, strlen(body), 0);
        zip_int64_t idx = zip_file_add(z, names[i], s, ZIP_FL_ENC_UTF_8);
        assert(idx >= 0);
        if (!names[i + 1] && last_mode)
            zip_file_set_external_attributes(z, (zip_uint64_t)idx, 0, ZIP_OPSYS_UNIX, (zip_uint32_t)last_mode << 16);
    }
    assert(zip_close(z) == 0);
}

static void test_catalog_keeps_only_valid_entries(void) {
    const char *json =
        "{\"apps\":["
        "{\"id\":\"com.example.ok\",\"name\":\"OK\",\"version\":\"1.2.0\",\"package\":\"p/ok.zpkg\","
        " \"size\":10,\"sha256\":\"" HEX64 "\",\"signature\":\"c2ln\",\"permissions\":[\"network\",3]},"
        "{\"id\":\"com.example.ok\",\"name\":\"Dup\",\"version\":\"9\",\"package\":\"d.zpkg\","
        " \"size\":10,\"sha256\":\"" HEX64 "\",\"signature\":\"c2ln\"},"
        "{\"id\":\"../evil\",\"name\":\"E\",\"version\":\"1\",\"package\":\"e.zpkg\",\"size\":1,\"sha256\":\"" HEX64 "\",\"signature\":\"c2ln\"},"
        "{\"id\":\"com.example.http\",\"name\":\"H\",\"version\":\"1\",\"package\":\"http://x/p\",\"size\":1,\"sha256\":\"" HEX64 "\",\"signature\":\"c2ln\"},"
        "{\"id\":\"com.example.ver\",\"name\":\"V\",\"version\":\"1.x\",\"package\":\"v.zpkg\",\"size\":1,\"sha256\":\"" HEX64 "\",\"signature\":\"c2ln\"},"
        "{\"id\":\"com.example.big\",\"name\":\"B\",\"version\":\"1\",\"package\":\"b.zpkg\",\"size\":999999999,\"sha256\":\"" HEX64 "\",\"signature\":\"c2ln\"},"
        "{\"id\":\"nodot\",\"name\":\"N\",\"version\":\"1\",\"package\":\"n.zpkg\",\"size\":1,\"sha256\":\"" HEX64 "\",\"signature\":\"c2ln\"}"
        "]}";
    GPtrArray *c = zyl_store_catalog_parse(json, -1, "https://store.example/v1/catalog.json");
    assert(c && c->len == 1);
    ZylStoreApp *a = g_ptr_array_index(c, 0);
    assert(strcmp(a->name, "OK") == 0);                                   /* the first of two */
    assert(strcmp(a->package_url, "https://store.example/v1/p/ok.zpkg") == 0);
    assert(g_strv_length(a->permissions) == 1);
    g_ptr_array_unref(c);
    assert(!zyl_store_catalog_parse("not json", -1, "https://s/c.json"));
    assert(!zyl_store_catalog_parse("{\"apps\":3}", -1, "https://s/c.json"));
}

static void test_versions_and_urls(void) {
    assert(zyl_store_version_cmp("1.2.0", "1.10") < 0 && zyl_store_version_cmp("2", "1.9.9") > 0);
    assert(zyl_store_version_cmp("1.0", "1") == 0 && zyl_store_version_cmp(NULL, "0") == 0);
    char *u = zyl_store_resolve_url("https://s.example/a/catalog.json", "https://cdn.example/x.zpkg");
    assert(u && strcmp(u, "https://cdn.example/x.zpkg") == 0);
    g_free(u);
    assert(!zyl_store_resolve_url("https://s.example/c.json", "file:///etc/passwd"));
    assert(!zyl_store_resolve_url("https://s.example/c.json", ""));
}

static void test_unpack_refuses_what_could_escape(void) {
    const char *app = "{\"id\":\"com.example.hello\",\"version\":\"1.0\",\"entry\":\"index.html\"}";
    const char *good[] = { "app.json", "index.html", "js/app.js", NULL };
    const char *dotdot[] = { "app.json", "../evil", NULL };
    const char *abs[] = { "app.json", "/etc/evil", NULL };
    const char *link[] = { "app.json", "index.html", "ln", NULL };
    const char *why = NULL;
    make_zip(ROOT_ "/good.zip", good, app, 0);
    assert(zyl_store_unpack(ROOT_ "/good.zip", ROOT_ "/out", &why));
    assert(g_file_test(ROOT_ "/out/js/app.js", G_FILE_TEST_IS_REGULAR));
    assert(zyl_store_check_manifest(ROOT_ "/out", "com.example.hello", "1.0", &why));
    assert(!zyl_store_check_manifest(ROOT_ "/out", "com.example.other", "1.0", &why));   /* a different app */
    assert(!zyl_store_check_manifest(ROOT_ "/out", "com.example.hello", "2.0", &why));   /* a different version */

    make_zip(ROOT_ "/dd.zip", dotdot, app, 0);
    assert(!zyl_store_unpack(ROOT_ "/dd.zip", ROOT_ "/o2", &why) && !g_file_test(ROOT_ "/evil", G_FILE_TEST_EXISTS));
    make_zip(ROOT_ "/abs.zip", abs, app, 0);
    assert(!zyl_store_unpack(ROOT_ "/abs.zip", ROOT_ "/o3", &why));
    make_zip(ROOT_ "/ln.zip", link, app, S_IFLNK | 0777);
    assert(!zyl_store_unpack(ROOT_ "/ln.zip", ROOT_ "/o4", &why));
    put(ROOT_ "/not.zip", "plain text");
    assert(!zyl_store_unpack(ROOT_ "/not.zip", ROOT_ "/o5", &why));

    /* A package's app.json with members of another type is refused, not a crash
       (json-glib's getter gave NULL for them — runtime/lib/zyl_json.h) */
    put(ROOT_ "/out/app.json", "{\"id\":1,\"version\":[\"1.0\"],\"entry\":{}}");
    assert(!zyl_store_check_manifest(ROOT_ "/out", "com.example.hello", "1.0", &why));
}

static void test_install_swap_and_removal_leave_nothing(void) {
    /* What an update replaces, and what an app made */
    put(ZYL_APPS_DIR "/com.example.hello/app.json", "{\"id\":\"com.example.hello\",\"version\":\"1.0\"}");
    put(ZYL_CONTAINERS_ROOT "/com.example.hello/appdata", "data");
    put(ZYL_CONTAINERS_ROOT "/com.example.hello/files/Documents/n.txt", "mine");
    put(ZYL_CONTAINERS_ROOT "/com.example.hello/webkit/data/storage/ls", "ls");
    put(ZYL_CONTAINERS_ROOT "/com.example.other/appdata", "other's");
    char *v = installed_version("com.example.hello");
    assert(v && strcmp(v, "1.0") == 0);
    g_free(v);

    put(ROOT_ "/work/app/app.json", "{\"id\":\"com.example.hello\",\"version\":\"2.0\"}");
    assert(swap_in(ROOT_ "/work", "com.example.hello"));
    v = installed_version("com.example.hello");
    assert(v && strcmp(v, "2.0") == 0);                                   /* updated */
    g_free(v);
    assert(g_file_test(ZYL_CONTAINERS_ROOT "/com.example.hello/appdata", G_FILE_TEST_EXISTS));  /* data kept */

    assert(remove_app("com.example.hello"));
    assert(system("find " ROOT_ " -path '*com.example.hello*' | grep -q .") != 0);   /* nothing left */
    assert(g_file_test(ZYL_CONTAINERS_ROOT "/com.example.other/appdata", G_FILE_TEST_EXISTS));
    assert(!remove_app("../appdata"));
}

/* com.zylos.* is the OS image's: a store app under one would hand its
   container to the system app an update later ships with that id */
static void test_the_os_names_are_not_the_store_s(void) {
    assert(reserved_id("com.zylos.maps") && reserved_id("com.zylos.settings"));
    assert(!reserved_id("com.zylosx.app") && !reserved_id("com.example.zylos") && !reserved_id("com.zylos"));
}

static void test_system_apps_are_not_the_store_s(void) {
    put(ZYL_SYSTEM_APPS_DIR "/settings/app.json", "{\"id\":\"com.zylos.settings\"}");
    assert(is_system_app("com.zylos.settings") && !is_system_app("com.example.hello"));
}

static void test_config_needs_https_and_a_key(void) {
    StoreConfig c;
    assert(!load_config(&c));                                             /* none */
    put(ZYL_STORE_CONFIG, "{\"url\":\"http://s/c.json\",\"publicKey\":\"K\"}");
    assert(!load_config(&c));
    put(ZYL_STORE_CONFIG, "{\"url\":\"https://s/c.json\",\"publicKey\":\"\"}");
    assert(!load_config(&c));
    put(ZYL_STORE_CONFIG, "{\"url\":\"https://s/c.json\",\"publicKey\":\"K\"}");
    assert(load_config(&c) && strcmp(c.url, "https://s/c.json") == 0);
    config_clear(&c);
}

int main(void) {
    printf("=== app store tests ===\n");
    RUN_TEST(test_catalog_keeps_only_valid_entries);
    RUN_TEST(test_versions_and_urls);
    RUN_TEST(test_unpack_refuses_what_could_escape);
    RUN_TEST(test_install_swap_and_removal_leave_nothing);
    RUN_TEST(test_system_apps_are_not_the_store_s);
    RUN_TEST(test_the_os_names_are_not_the_store_s);
    RUN_TEST(test_config_needs_https_and_a_key);
    assert(system("rm -rf " ROOT_) == 0);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
