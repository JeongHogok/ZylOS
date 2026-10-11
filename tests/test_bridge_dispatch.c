/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: bridge.c 디스패치/응답 라우팅 단위 테스트
 * 수행범위: callbackId 전달, unknown type error 응답, malformed JSON reject 검증
 * 의존방향: GLib/JSON-GLib, runtime/wam/src/bridge/bridge.c
 * SOLID: SRP — 브릿지 디스패치 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "include/mock_webkit.h"
#include "../runtime/wam/src/bridge/bridge.h"

static int tests_run;
static int tests_pass;

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("  %-44s ", #fn); \
    fn(); \
    tests_pass++; \
    printf("PASS\n"); \
} while (0)

static void reset_webview(WebKitWebView *view)
{
    g_free(view->last_script);
    view->last_script = NULL;
}

static void test_handler(const char *type,
                         gpointer msg_obj,
                         ZylAppManifest *manifest,
                         ZylBridgeReplyCtx *reply_ctx,
                         gpointer user_data)
{
    (void)type;
    (void)msg_obj;
    (void)manifest;
    const char *payload = user_data ? (const char *)user_data : "{\"ok\":true}";
    zyl_bridge_respond(reply_ctx->webview, reply_ctx->callback_id, payload);
}

static ZylAppManifest make_manifest(void)
{
    ZylAppManifest m = {0};
    m.id = g_strdup("com.zylos.test");
    m.name = g_strdup("Bridge Test");
    m.version = g_strdup("1.0.0");
    return m;
}

static void free_manifest_members(ZylAppManifest *m)
{
    g_free(m->id);
    g_free(m->name);
    g_free(m->version);
}

static void test_dispatch_routes_callback_id(void)
{
    WebKitWebView view = {0};
    ZylAppManifest manifest = make_manifest();

    zyl_bridge_init();
    assert(zyl_bridge_register_handler("test.echo", test_handler,
                                       "{\"value\":42}") == 0);

    zyl_bridge_dispatch(&view, &manifest,
                        "{\"type\":\"test.echo\",\"callbackId\":7}");

    assert(view.last_script != NULL);
    assert(strstr(view.last_script, "_zylCb_7") != NULL);
    assert(strstr(view.last_script, "{\"value\":42}") != NULL);

    zyl_bridge_cleanup();
    reset_webview(&view);
    free_manifest_members(&manifest);
}

static void test_dispatch_supports_legacy_cbid(void)
{
    WebKitWebView view = {0};
    ZylAppManifest manifest = make_manifest();

    zyl_bridge_init();
    assert(zyl_bridge_register_handler("test.echo", test_handler,
                                       "{\"ok\":true}") == 0);

    zyl_bridge_dispatch(&view, &manifest,
                        "{\"type\":\"test.echo\",\"_cbId\":9}");

    assert(view.last_script != NULL);
    assert(strstr(view.last_script, "_zylCb_9") != NULL);

    zyl_bridge_cleanup();
    reset_webview(&view);
    free_manifest_members(&manifest);
}

static void test_unknown_type_returns_error(void)
{
    WebKitWebView view = {0};
    ZylAppManifest manifest = make_manifest();

    zyl_bridge_init();
    zyl_bridge_dispatch(&view, &manifest,
                        "{\"type\":\"unknown.type\",\"callbackId\":5}");

    assert(view.last_script != NULL);
    assert(strstr(view.last_script, "_zylCb_5") != NULL);
    assert(strstr(view.last_script, "unknown message type") != NULL);

    zyl_bridge_cleanup();
    reset_webview(&view);
    free_manifest_members(&manifest);
}

static void test_malformed_json_does_not_respond(void)
{
    WebKitWebView view = {0};
    ZylAppManifest manifest = make_manifest();

    zyl_bridge_init();
    zyl_bridge_dispatch(&view, &manifest,
                        "{\"type\":\"test.echo\",\"callbackId\":1");

    assert(view.last_script == NULL);

    zyl_bridge_cleanup();
    free_manifest_members(&manifest);
}

/* Regression: a bridge message must be exactly one JSON object. Anything with
   a value before or after it makes json-glib build the leading value and then
   fail without attaching it to the parser, so unreffing the parser cannot free
   it. The fuzzer reduced two such leaks to "{}{}" and "0{\"type\":\"x\"}".
   json_prevalidate() must reject both before the parser sees the data. */
static void test_only_one_json_object_accepted(void)
{
    WebKitWebView view = {0};
    ZylAppManifest manifest = make_manifest();

    zyl_bridge_init();
    assert(zyl_bridge_register_handler("test.echo", test_handler,
                                       "{\"value\":1}") == 0);

    /* The leading object alone is a valid message with a callbackId, so if
       prevalidation let this through the handler would have replied. */
    zyl_bridge_dispatch(&view, &manifest,
                        "{\"type\":\"test.echo\",\"callbackId\":3}{}");
    assert(view.last_script == NULL);

    /* The exact inputs the fuzzer reduced the two leaks to. */
    reset_webview(&view);
    zyl_bridge_dispatch(&view, &manifest, "{}{}");
    assert(view.last_script == NULL);

    reset_webview(&view);
    zyl_bridge_dispatch(&view, &manifest, "0{\"type\":\"te>cho\"}");
    assert(view.last_script == NULL);

    /* A value before the object is rejected even when the object is valid. */
    reset_webview(&view);
    zyl_bridge_dispatch(&view, &manifest,
                        "0{\"type\":\"test.echo\",\"callbackId\":6}");
    assert(view.last_script == NULL);

    /* A non-object top level never reaches the parser now. */
    reset_webview(&view);
    zyl_bridge_dispatch(&view, &manifest, "[1,2]");
    assert(view.last_script == NULL);

    /* Surrounding whitespace is still a single message and must be accepted. */
    reset_webview(&view);
    zyl_bridge_dispatch(&view, &manifest,
                        "  {\"type\":\"test.echo\",\"callbackId\":7}  ");
    assert(view.last_script != NULL);
    assert(strstr(view.last_script, "_zylCb_7") != NULL);

    reset_webview(&view);
    zyl_bridge_dispatch(&view, &manifest,
                        "{\"type\":\"test.echo\",\"callbackId\":4}  \n");
    assert(view.last_script != NULL);
    assert(strstr(view.last_script, "_zylCb_4") != NULL);

    /* A nested object closing mid-message must not be mistaken for the end. */
    reset_webview(&view);
    zyl_bridge_dispatch(&view, &manifest,
                        "{\"type\":\"test.echo\",\"d\":{\"k\":[1,2]},\"callbackId\":5}");
    assert(view.last_script != NULL);
    assert(strstr(view.last_script, "_zylCb_5") != NULL);

    zyl_bridge_cleanup();
    reset_webview(&view);
    free_manifest_members(&manifest);
}

/* Pull real implementation into this test binary. */
#include "../runtime/wam/src/bridge/bridge.c"

int main(void)
{
    printf("=== Zyl OS Bridge Dispatch Tests ===\n");
    RUN_TEST(test_dispatch_routes_callback_id);
    RUN_TEST(test_dispatch_supports_legacy_cbid);
    RUN_TEST(test_unknown_type_returns_error);
    RUN_TEST(test_malformed_json_does_not_respond);
    RUN_TEST(test_only_one_json_object_accepted);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
