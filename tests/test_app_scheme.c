/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 앱 출처 zyl-app:// 해석(runtime/wam/src/scheme) 단위 테스트
 * 수행범위: 앱 파일·OS 코드(/shared/·/keyboard/), .. ·절대 경로·밖을 가리키는 심볼릭
 *           링크·디렉터리·없는 파일 거부, MIME, 진입 URI, 권한에 따른 CSP
 *           (apps/system/sandbox.js 와 같은 문자열 — test_services.js 가 같은 기대값)
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
#define OS ROOT_ "/os"
#define SHARED OS "/shared"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void put(const char *path) { assert(g_file_set_contents(path, "x", -1, NULL)); }

static bool names(const char *path, const char *want) {
    char *f = zyl_app_scheme_file(APP, OS, path);
    bool ok = want ? f && strcmp(f, want) == 0 : f == NULL;
    if (!ok) printf("\n    %s → %s (want %s)\n", path, f ? f : "NULL", want ? want : "NULL");
    g_free(f);
    return ok;
}

static void test_app_and_shared_files(void) {
    assert(names("/index.html", APP "/index.html"));
    assert(names("/js/app.js", APP "/js/app.js"));
    assert(names("/shared/bridge.js", SHARED "/bridge.js"));     /* ../shared/ from the app's root */
    /* The on-screen keyboard bridge.js loads into every page: OS code, not the app's */
    assert(names("/keyboard/js/inpage.js", OS "/keyboard/js/inpage.js"));
    assert(zyl_app_scheme_is_os("/keyboard/js/inpage.js") && zyl_app_scheme_is_os("/shared/x.js"));
    assert(!zyl_app_scheme_is_os("/keyboards/x.js") && !zyl_app_scheme_is_os("/sharedx/y") &&
           !zyl_app_scheme_is_os("/index.html") && !zyl_app_scheme_is_os("/shared"));
    assert(names("/settings/x.js", NULL));                       /* other image apps stay out */
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

/* The same strings test_services.js expects of sandbox.js getContentPolicy */
static void same_csp(gboolean network, gboolean webview, const char *want) {
    char *got = zyl_app_scheme_csp(network, webview);
    if (strcmp(got, want) != 0) printf("\n    got  %s\n    want %s\n", got, want);
    assert(strcmp(got, want) == 0);
    g_free(got);
}

/* The app's view loads its own origin; the web only in a frame of an app
   allowed it (its top document is checked with is_own as it loads) */
static void test_the_view_stays_the_apps(void) {
    const char *id = "com.example.x";
    assert(zyl_app_scheme_is_own(id, "zyl-app://com.example.x/index.html"));
    assert(zyl_app_scheme_is_own(id, "zyl-app://com.example.x"));
    assert(!zyl_app_scheme_is_own(id, "zyl-app://com.example.xy/index.html"));   /* another app */
    assert(!zyl_app_scheme_is_own(id, "zyl-app://com.zylos.settings/index.html"));
    assert(!zyl_app_scheme_is_own(id, "https://example.com/"));
    assert(!zyl_app_scheme_is_own("", "zyl-app:///index.html") && !zyl_app_scheme_is_own(id, NULL));
    for (int webview = 0; webview <= 1; webview++) {
        assert(zyl_app_scheme_may_load(id, "zyl-app://com.example.x/b.html", webview));
        assert(zyl_app_scheme_may_load(id, "about:blank", webview));
        assert(zyl_app_scheme_may_load(id, "blob:zyl-app://com.example.x/5f1c", webview));
        assert(!zyl_app_scheme_may_load(id, "blob:zyl-app://com.example.other/5f1c", webview));
        assert(!zyl_app_scheme_may_load(id, "zyl-app://com.example.other/", webview));
        assert(!zyl_app_scheme_may_load(id, "http://example.com/", webview));        /* never plain http */
        assert(!zyl_app_scheme_may_load(id, "file:///etc/passwd", webview));
        assert(!zyl_app_scheme_may_load(id, "data:text/html,x", webview));
        assert(!zyl_app_scheme_may_load(id, NULL, webview));
    }
    assert(!zyl_app_scheme_may_load(id, "https://example.com/", FALSE));
    assert(zyl_app_scheme_may_load(id, "https://example.com/", TRUE));
}

static void test_csp_follows_permissions(void) {
    /* Without the network: its own files, nothing out */
    same_csp(FALSE, FALSE,
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data: blob:; media-src 'self' blob:; connect-src 'self'; "
        "font-src 'self'; frame-src 'self'; object-src 'none'");
    /* With it: https/wss out, still never plain http, never remote or inline code */
    same_csp(TRUE, FALSE,
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data: blob: https:; media-src 'self' blob: https:; connect-src 'self' https: wss:; "
        "font-src 'self' https:; frame-src 'self'; object-src 'none'");
    /* Both (a system app holds every permission): https pages in frames */
    same_csp(TRUE, TRUE,
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data: blob: https:; media-src 'self' blob: https:; connect-src 'self' https: wss:; "
        "font-src 'self' https:; frame-src 'self' https:; object-src 'none'");
    /* Whatever the permissions: no eval, no remote and no inline script */
    char *all = zyl_app_scheme_csp(TRUE, TRUE);
    assert(!strstr(all, "eval"));
    assert(strstr(all, "script-src 'self';"));
    g_free(all);
}

int main(void) {
    printf("=== app scheme tests ===\n");
    assert(system("rm -rf " ROOT_) == 0);
    g_mkdir_with_parents(APP "/js", 0755);
    g_mkdir_with_parents(SHARED, 0755);
    g_mkdir_with_parents(OS "/keyboard/js", 0755);
    g_mkdir_with_parents(OS "/settings", 0755);
    put(OS "/keyboard/js/inpage.js");
    put(OS "/settings/x.js");
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
    RUN_TEST(test_csp_follows_permissions);
    RUN_TEST(test_the_view_stays_the_apps);
    assert(system("rm -rf " ROOT_) == 0);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
