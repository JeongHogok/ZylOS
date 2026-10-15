/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 앱 출처 zyl-app:// 해석(runtime/wam/src/scheme) 단위 테스트
 * 수행범위: 앱 파일·/shared/ 공용 코드, .. ·절대 경로·밖을 가리키는 심볼릭
 *           링크·디렉터리·없는 파일 거부, MIME, 진입 URI
 * 의존방향: app_scheme.c, glib
 * SOLID: SRP — 앱이 자기 디렉터리와 OS 공용 코드 밖을 읽지 못함을 검증
 * ────────────────────────────────────────────────────────── */

#include "../runtime/wam/src/scheme/app_scheme.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ROOT_ "/tmp/zyl-scheme-test"
#define APP ROOT_ "/apps/com.example.x"
#define SHARED ROOT_ "/os/shared"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void put(const char *path) { assert(g_file_set_contents(path, "x", -1, NULL)); }

static bool names(const char *path, const char *want) {
    char *f = zyl_app_scheme_file(APP, SHARED, path);
    bool ok = want ? f && strcmp(f, want) == 0 : f == NULL;
    if (!ok) printf("\n    %s → %s (want %s)\n", path, f ? f : "NULL", want ? want : "NULL");
    g_free(f);
    return ok;
}

static void test_app_and_shared_files(void) {
    assert(names("/index.html", APP "/index.html"));
    assert(names("/js/app.js", APP "/js/app.js"));
    assert(names("/shared/bridge.js", SHARED "/bridge.js"));     /* ../shared/ from the app's root */
}

static void test_nothing_outside(void) {
    assert(names("/../secret", NULL));
    assert(names("/shared/../secret", NULL));
    assert(names("/js/../../secret", NULL));
    assert(names("index.html", NULL));                           /* not absolute */
    assert(names("/js", NULL));                                  /* a directory */
    assert(names("/missing.js", NULL));
    assert(names("/out-link", NULL));                            /* a link out of the app */
    assert(names("/shared/out-link", NULL));
    assert(names("/in-link.js", APP "/js/app.js"));              /* a link inside is fine */
}

static void test_mime_and_uri(void) {
    assert(strcmp(zyl_app_scheme_mime("a/b.JS"), "text/javascript") == 0);
    assert(strcmp(zyl_app_scheme_mime("x.html"), "text/html") == 0);
    assert(strcmp(zyl_app_scheme_mime("f.woff2"), "font/woff2") == 0);
    assert(strcmp(zyl_app_scheme_mime("noext"), "application/octet-stream") == 0);
    assert(strcmp(zyl_app_scheme_mime("dir.d/noext"), "application/octet-stream") == 0);
    char *u = zyl_app_scheme_uri("com.example.x", "index.html");
    assert(strcmp(u, "zyl-app://com.example.x/index.html") == 0);
    g_free(u);
}

int main(void) {
    printf("=== app scheme tests ===\n");
    assert(system("rm -rf " ROOT_) == 0);
    g_mkdir_with_parents(APP "/js", 0755);
    g_mkdir_with_parents(SHARED, 0755);
    put(APP "/index.html");
    put(APP "/js/app.js");
    put(SHARED "/bridge.js");
    put(ROOT_ "/secret");
    assert(symlink(ROOT_ "/secret", APP "/out-link") == 0);
    assert(symlink(ROOT_ "/secret", SHARED "/out-link") == 0);
    assert(symlink("js/app.js", APP "/in-link.js") == 0);
    RUN_TEST(test_app_and_shared_files);
    RUN_TEST(test_nothing_outside);
    RUN_TEST(test_mime_and_uri);
    assert(system("rm -rf " ROOT_) == 0);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
