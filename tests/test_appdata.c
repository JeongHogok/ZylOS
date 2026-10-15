/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 앱 데이터 서비스(runtime/services/appdata) 단위 테스트
 * 수행범위: 앱마다 따로(같은 키라도 남의 값은 안 보임), 컨테이너 밖을 가리키는
 *           앱 id 거부, 없으면 '', 5 MiB 한도(넘으면 이전 값 유지), 키 길이,
 *           지우기·키 목록·전부 지우기, 파일 0600·디렉터리 0700
 * 의존방향: runtime/services/appdata/appdata.c (ZYL_TEST), glib
 * SOLID: SRP — 앱 데이터 격리 규칙 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-appdata-test"
#define ZYL_APPDATA_ROOT ROOT_
#include "../runtime/services/appdata/appdata.c"
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); assert(system("rm -rf " ROOT_) == 0); fn(); tests_pass++; printf("PASS\n"); } while (0)

static bool value_is(const char *app, const char *key, const char *want) {
    char *v = get_item(app, key);
    bool r = strcmp(v, want) == 0;
    g_free(v);
    return r;
}

static void test_each_app_sees_only_its_own(void) {
    assert(set_item("com.a", "k", "a's"));
    assert(set_item("com.b", "k", "b's"));
    assert(value_is("com.a", "k", "a's") && value_is("com.b", "k", "b's"));
    assert(value_is("com.c", "k", ""));                     /* absent */
    struct stat st;
    assert(stat(ROOT_ "/com.a/appdata", &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(stat(ROOT_ "/com.a", &st) == 0 && (st.st_mode & 0777) == 0700);
}

static void test_app_id_cannot_leave_the_container_root(void) {
    const char *bad[] = { "", ".", "..", "../etc", "com.a/../com.b", "a/b", "com a", NULL };
    for (int i = 0; bad[i]; i++) {
        assert(!set_item(bad[i], "k", "v"));
        assert(value_is(bad[i], "k", ""));
    }
    assert(!set_item(NULL, "k", "v"));
    assert(system("test ! -e /tmp/etc/appdata") == 0);
}

static void test_quota_and_key_limits(void) {
    char *big = g_malloc(QUOTA_BYTES);
    memset(big, 'x', QUOTA_BYTES - 1);
    big[QUOTA_BYTES - 1] = 0;
    assert(set_item("com.a", "small", "kept"));
    assert(!set_item("com.a", "big", big));                 /* over with the other key */
    assert(value_is("com.a", "small", "kept") && value_is("com.a", "big", ""));
    char key[KEY_MAX + 2];
    memset(key, 'k', KEY_MAX + 1);
    key[KEY_MAX + 1] = 0;
    assert(!set_item("com.a", key, "v") && !set_item("com.a", "", "v"));
    key[KEY_MAX] = 0;
    assert(set_item("com.a", key, "v"));
    g_free(big);
}

static void test_remove_keys_clear(void) {
    assert(set_item("com.a", "x", "1") && set_item("com.a", "y", "2"));
    GVariant *k = keys("com.a");
    assert(g_variant_n_children(k) == 2);
    g_variant_unref(k);
    assert(remove_item("com.a", "x") && !remove_item("com.a", "x"));
    assert(value_is("com.a", "x", "") && value_is("com.a", "y", "2"));
    assert(clear_all("com.a"));
    k = keys("com.a");
    assert(g_variant_n_children(k) == 0);
    g_variant_unref(k);
}

int main(void) {
    printf("=== appdata tests ===\n");
    RUN_TEST(test_each_app_sees_only_its_own);
    RUN_TEST(test_app_id_cannot_leave_the_container_root);
    RUN_TEST(test_quota_and_key_limits);
    RUN_TEST(test_remove_keys_clear);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    assert(system("rm -rf " ROOT_) == 0);
    return tests_pass == tests_run ? 0 : 1;
}
