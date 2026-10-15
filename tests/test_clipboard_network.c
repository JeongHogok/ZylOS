/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 클립보드(runtime/services/clipboard)·네트워크(runtime/services/network)
 *       단위 테스트
 * 수행범위: 클립보드 — 복사·붙여넣기·출처 앱, 빈 상태, 너무 큰 값 거부,
 *           민감한 값은 시간이 지나면 지워짐. 네트워크 — https·허용 도메인만,
 *           사용자 정보·포트·IP·대소문자·공백으로 우회 불가
 * 의존방향: clipboard.c, network.c (ZYL_TEST), glib
 * SOLID: SRP — 두 서비스의 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include "../runtime/services/clipboard/clipboard.c"
#include "../runtime/services/network/network.c"
#include <assert.h>
#include <stdio.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void test_copy_paste_and_source(void) {
    Clip c = {0};
    GVariant *empty = clip_variant(&c);
    assert(g_variant_n_children(empty) == 0);
    g_variant_unref(empty);
    assert(clip_copy(&c, "com.zylos.notes", "hello", false, 42));
    GVariant *v = clip_variant(&c);
    const gchar *text = NULL, *src = NULL;
    gint64 ts = 0;
    assert(g_variant_lookup(v, "text", "&s", &text) && strcmp(text, "hello") == 0);
    assert(g_variant_lookup(v, "sourceApp", "&s", &src) && strcmp(src, "com.zylos.notes") == 0);
    assert(g_variant_lookup(v, "timestamp", "x", &ts) && ts == 42);
    g_variant_unref(v);
    char *big = g_malloc(MAX_TEXT + 2);
    memset(big, 'a', MAX_TEXT + 1);
    big[MAX_TEXT + 1] = 0;
    assert(!clip_copy(&c, "x", big, false, 1) && strcmp(c.text, "hello") == 0);   /* kept */
    assert(!clip_copy(&c, "x", NULL, false, 1));
    g_free(big);
    clip_clear(&c);
    assert(!c.text);
}

static gboolean quit_loop(gpointer loop) { g_main_loop_quit(loop); return G_SOURCE_REMOVE; }

static void test_sensitive_value_clears_itself(void) {
    Clip c = {0};
    assert(clip_copy(&c, "com.zylos.settings", "hunter22", true, 1));
    assert(c.clear_id);
    /* What the timer runs, without waiting a minute */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_source_remove(c.clear_id);
    c.clear_id = g_timeout_add(10, on_sensitive_timeout, &c);
    g_timeout_add(50, quit_loop, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    assert(!c.text && !c.clear_id);
    /* A later copy cancels the pending clear */
    assert(clip_copy(&c, "a", "secret", true, 1) && clip_copy(&c, "a", "plain", false, 2));
    assert(!c.clear_id && strcmp(c.text, "plain") == 0);
    clip_clear(&c);
}

static void test_only_https_to_allowed_hosts(void) {
    assert(url_allowed("https://api.open-meteo.com/v1/forecast?latitude=1"));
    assert(url_allowed("https://IPINFO.io/json"));
    assert(url_allowed("https://ipinfo.io"));
    assert(!url_allowed("http://api.open-meteo.com/v1"));             /* not https */
    assert(!url_allowed("https://evil.com/api.open-meteo.com"));
    assert(!url_allowed("https://api.open-meteo.com.evil.com/"));
    assert(!url_allowed("https://evil@api.open-meteo.com/"));         /* user info */
    assert(!url_allowed("https://api.open-meteo.com:8443/"));         /* port */
    assert(!url_allowed("https://127.0.0.1/") && !url_allowed("https://localhost/"));
    assert(!url_allowed("https://ipinfo.io/a b") && !url_allowed("https://ipinfo.io/\n"));
    assert(!url_allowed("") && !url_allowed(NULL));
}

int main(void) {
    printf("=== clipboard / network tests ===\n");
    RUN_TEST(test_copy_paste_and_source);
    RUN_TEST(test_sensitive_value_clears_itself);
    RUN_TEST(test_only_https_to_allowed_hosts);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
