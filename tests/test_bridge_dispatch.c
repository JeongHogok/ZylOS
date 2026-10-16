/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: bridge.c 디스패치/응답 라우팅 단위 테스트
 * 수행범위: callbackId 전달, unknown type error 응답, malformed JSON reject,
 *          service.request 인자 마샬링(인트로스펙션 시그니처) + 사설 버스 종단 검증
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

/* Every app can post to the bridge, so a message type that reaches a service
   without the permission check is a way around it (wifi.scan handed any app
   the nearby networks). Only service.request reaches services. */
static void test_no_unchecked_service_shortcut(void)
{
    const char *gone[] = { "wifi.scan", "battery.getLevel" };
    for (size_t i = 0; i < sizeof(gone) / sizeof(gone[0]); i++) {
        WebKitWebView view = {0};
        ZylAppManifest manifest = make_manifest();
        char msg[96];

        zyl_bridge_init();
        snprintf(msg, sizeof(msg), "{\"type\":\"%s\",\"callbackId\":7}", gone[i]);
        zyl_bridge_dispatch(&view, &manifest, msg);

        assert(view.last_script != NULL);
        assert(strstr(view.last_script, "unknown message type") != NULL);

        zyl_bridge_cleanup();
        reset_webview(&view);
        free_manifest_members(&manifest);
    }
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

/* Below the include: app_may_call is static to bridge.c */
/* ── Device permission policy (app_may_call) ─────────────────
   The device bridge used to route every service.request to D-Bus unchecked. */
static ZylAppManifest perm_manifest(const char *id, gboolean is_system,
                                    const char **perms) {
    ZylAppManifest m = {0};
    m.id = (char *)id;
    m.is_system = is_system;
    m.permissions = (char **)perms;
    int n = 0;
    while (perms && perms[n]) n++;
    m.n_permissions = n;
    return m;
}

static void test_policy_unknown_app_is_denied(void)
{
    assert(app_may_call(NULL, "device") == FALSE);
}

static void test_policy_system_app_may_call_everything(void)
{
    const char *none[] = { NULL };
    ZylAppManifest m = perm_manifest("com.zylos.settings", TRUE, none);
    assert(app_may_call(&m, "settings"));
    assert(app_may_call(&m, "credential"));
    assert(app_may_call(&m, "telemetry"));
}

static void test_policy_declared_permission_grants_its_service(void)
{
    const char *perms[] = { "camera", "network", NULL };
    ZylAppManifest m = perm_manifest("com.example.cam", FALSE, perms);
    assert(app_may_call(&m, "camera"));
    assert(app_may_call(&m, "network"));
    assert(app_may_call(&m, "telephony") == FALSE);
    assert(app_may_call(&m, "contacts") == FALSE);
}

static void test_policy_reserved_permissions_cannot_be_self_declared(void)
{
    const char *perms[] = { "system", "credential", "app.manage",
                            "notification.read", NULL };
    ZylAppManifest m = perm_manifest("com.evil.app", FALSE, perms);
    assert(app_may_call(&m, "settings") == FALSE);
    assert(app_may_call(&m, "telemetry") == FALSE);
    assert(app_may_call(&m, "credential") == FALSE);
}

static void test_policy_services_without_requirement_are_open(void)
{
    const char *none[] = { NULL };
    ZylAppManifest m = perm_manifest("com.example.any", FALSE, none);
    assert(app_may_call(&m, "device"));
    assert(app_may_call(&m, "display"));
}

static void test_policy_unreviewed_service_fails_closed(void)
{
    const char *perms[] = { "system", NULL };
    ZylAppManifest m = perm_manifest("com.example.any", FALSE, perms);
    assert(app_may_call(&m, "not-a-service") == FALSE);
}

/* The user switched a permission off in Settings (the settings service's
   app_permissions): the device used to ignore it entirely. */
static void test_policy_user_revocation_takes_the_service_away(void)
{
    const char *perms[] = { "camera", "contacts", NULL };
    ZylAppManifest m = perm_manifest("com.example.cam", FALSE, perms);
    assert(app_may_call(&m, "camera"));

    revocations_set("com.example.cam", "camera");
    assert(app_may_call(&m, "camera") == FALSE);
    assert(app_may_call(&m, "contacts"));           /* only what was switched off */
    assert(app_may_call(&m, "device"));             /* no permission required */

    /* Another app's revocation is not this app's */
    revocations_set("com.example.other", "contacts");
    assert(app_may_call(&m, "contacts"));

    /* Switching it back on in Settings rewrites the list */
    revocations_set("com.example.cam", "");
    assert(app_may_call(&m, "camera"));

    /* A system app is never revocable — the same rule as permissions.js */
    ZylAppManifest sys = perm_manifest("com.zylos.camera", TRUE, perms);
    revocations_set("com.zylos.camera", "camera");
    assert(app_may_call(&sys, "camera"));
}

/* Several permissions at once, as Settings writes them */
static void test_policy_revocation_list_is_comma_separated(void)
{
    const char *perms[] = { "camera", "contacts", "messaging", NULL };
    ZylAppManifest m = perm_manifest("com.example.many", FALSE, perms);
    revocations_set("com.example.many", "camera, messaging");
    assert(app_may_call(&m, "camera") == FALSE);
    assert(app_may_call(&m, "messaging") == FALSE);
    assert(app_may_call(&m, "contacts"));
}

/* What the WAM gives an app's documents (CSP, CORS) asks this: system apps
   hold everything, a user app what it declared, minus reserved and revoked */
static void test_app_holds_follows_declaration_and_revocation(void)
{
    const char *perms[] = { "network", "system", NULL };
    ZylAppManifest m = perm_manifest("com.example.net", FALSE, perms);
    assert(zyl_bridge_app_holds(&m, "network"));
    assert(!zyl_bridge_app_holds(&m, "webview"));       /* not declared */
    assert(!zyl_bridge_app_holds(&m, "system"));        /* reserved */
    revocations_set("com.example.net", "network");
    assert(!zyl_bridge_app_holds(&m, "network"));
    revocations_set("com.example.net", "");

    const char *none[] = { NULL };
    ZylAppManifest sys = perm_manifest("com.zylos.browser", TRUE, none);
    revocations_set("com.zylos.browser", "network");
    assert(zyl_bridge_app_holds(&sys, "network"));
    assert(zyl_bridge_app_holds(&sys, "webview"));
    assert(!zyl_bridge_app_holds(NULL, "network"));
}

static GVariant *settings_dict(const char *const *pairs)
{
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    for (int i = 0; pairs[i]; i += 2)
        g_variant_builder_add(&b, "{sv}", pairs[i], g_variant_new_string(pairs[i + 1]));
    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static gboolean names(GPtrArray *ids, const char *const *want)
{
    guint n = 0;
    for (; want[n]; n++)
        if (!g_ptr_array_find_with_equal_func(ids, want[n], g_str_equal, NULL)) return FALSE;
    return ids->len == n;
}

/* Settings rewrote app_permissions: exactly the apps whose list changed are
   named — the WAM closes those, their documents carry the old rules */
static void test_revocation_reload_names_only_changed_apps(void)
{
    const char *empty[] = { NULL };
    GVariant *d = settings_dict(empty);
    g_ptr_array_unref(revocations_load(d));                /* what earlier tests left */
    g_variant_unref(d);

    const char *start[] = { "com.a", "network", "com.b", "camera", "com.c", "", NULL };
    d = settings_dict(start);
    GPtrArray *ids = revocations_load(d);
    const char *first[] = { "com.a", "com.b", NULL };    /* "" is no revocation */
    assert(names(ids, first));
    g_ptr_array_unref(ids);
    g_variant_unref(d);

    const char *next[] = { "com.a", "network", "com.b", "camera,contacts", "com.d", "location", NULL };
    d = settings_dict(next);
    ids = revocations_load(d);
    const char *changed[] = { "com.b", "com.d", NULL };   /* a unchanged, c still none */
    assert(names(ids, changed));
    g_ptr_array_unref(ids);
    g_variant_unref(d);

    d = settings_dict(empty);
    ids = revocations_load(d);                             /* switched all back on */
    const char *all[] = { "com.a", "com.b", "com.d", NULL };
    assert(names(ids, all));
    assert(!is_revoked("com.a", "network"));
    g_ptr_array_unref(ids);
    g_variant_unref(d);
}


/* ── service.request argument marshalling (params_to_dbus_args) ──
   The bridge used to send every call as one JSON string, which no typed
   D-Bus method accepts. Arguments now follow the service's introspection. */
static const char *MARSHAL_XML =
    "<node><interface name='org.zylos.T'>"
    "<method name='GetState'/>"
    "<method name='Dial'><arg type='s' name='number' direction='in'/></method>"
    "<method name='Post'><arg type='s' name='app_id' direction='in'/>"
    "<arg type='s' name='title' direction='in'/><arg type='i' name='priority' direction='in'/></method>"
    "<method name='SetEnabled'><arg type='b' name='enabled' direction='in'/></method>"
    "<method name='SetLevel'><arg type='d' name='level' direction='in'/></method>"
    "<method name='Update'><arg type='a{sv}' name='params' direction='in'/></method>"
    "<method name='Copy'><arg type='s' name='caller' direction='in'/>"
    "<arg type='a{sv}' name='params' direction='in'/></method>"
    "<method name='Read'><arg type='s' name='container' direction='in'/>"
    "<arg type='s' name='path' direction='in'/></method>"
    "</interface></node>";

/* third_party: the caller is not a system app */
static char *marshal_as(const char *method, const char *params_json,
                        const char *app_id, gboolean third_party, GError **err) {
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(MARSHAL_XML, NULL);
    GDBusMethodInfo *m = g_dbus_interface_info_lookup_method(
        g_dbus_node_info_lookup_interface(node, "org.zylos.T"), method);
    JsonNode *root = json_from_string(params_json, NULL);
    GVariant *v = params_to_dbus_args(m, json_node_get_object(root), app_id, !third_party, err);
    char *out = v ? g_variant_print(g_variant_ref_sink(v), FALSE) : NULL;
    if (v) g_variant_unref(v);
    json_node_unref(root);
    g_dbus_node_info_unref(node);
    return out;
}

static char *marshal(const char *method, const char *params_json, GError **err) {
    return marshal_as(method, params_json, "com.zylos.settings", FALSE, err);
}

static void expect_args(const char *method, const char *params, const char *want) {
    GError *err = NULL;
    char *got = marshal(method, params, &err);
    if (!got || strcmp(got, want) != 0) {
        printf("\n    %s(%s): want %s, got %s (%s)\n", method, params, want,
               got ? got : "NULL", err ? err->message : "");
        assert(0);
    }
    g_free(got);
}

static void test_args_no_input_is_empty_tuple(void) {
    expect_args("GetState", "{}", "()");
}

static void test_args_follow_signature_not_one_json_string(void) {
    expect_args("Dial", "{\"number\":\"0101234\"}", "('0101234',)");
    expect_args("SetEnabled", "{\"enabled\":true}", "(true,)");
}

static void test_args_camel_case_fills_snake_case(void) {
    expect_args("Post", "{\"title\":\"t\",\"appId\":\"a\",\"priority\":2}", "('a', 't', 2)");
}

static void test_args_integer_fills_double(void) {
    expect_args("SetLevel", "{\"level\":1}", "(1.0,)");
}

static void test_args_missing_or_wrong_type_names_the_argument(void) {
    GError *err = NULL;
    assert(marshal("Dial", "{}", &err) == NULL);
    assert(err && strstr(err->message, "number"));
    g_clear_error(&err);
    assert(marshal("SetEnabled", "{\"enabled\":\"yes\"}", &err) == NULL);
    assert(err && strstr(err->message, "enabled"));
    g_clear_error(&err);
}

/* ── service.request end to end over a private system bus ──
   A real GDBus service with a typed method, called through dispatch exactly
   as an app would. Before the fix this returned InvalidArgs for '(s)'. */
static gboolean e2e_seen_enabled;

static void e2e_method(GDBusConnection *c, const char *sender, const char *path,
                       const char *iface, const char *method, GVariant *params,
                       GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)method; (void)data;
    gboolean on = FALSE;
    g_variant_get(params, "(b)", &on);
    e2e_seen_enabled = on;
    g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", on));
}

static void e2e_wait_reply(WebKitWebView *view) {
    gint64 deadline = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
    while (!view->last_script && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, FALSE);
}

static void test_service_request_reaches_typed_dbus_method(void) {
    if (!g_find_program_in_path("dbus-daemon")) {
        printf("SKIP (no dbus-daemon) ");
        return;
    }
    GTestDBus *bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    /* Zyl services live on the system bus; the private bus plays that part */
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address(bus), TRUE);

    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    assert(conn);
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.zylos.Telemetry'><method name='SetEnabled'>"
        "<arg type='b' name='enabled' direction='in'/>"
        "<arg type='b' name='enabled' direction='out'/></method></interface></node>", NULL);
    static const GDBusInterfaceVTable vt = { e2e_method, NULL, NULL, {0} };
    guint reg = g_dbus_connection_register_object(conn, "/org/zylos/Telemetry",
        info->interfaces[0], &vt, NULL, NULL, NULL);
    assert(reg > 0);
    GVariant *r = g_dbus_connection_call_sync(conn, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", "org.zylos.Telemetry", 0), NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    assert(r);
    g_variant_unref(r);

    WebKitWebView view = {0};
    ZylAppManifest manifest = make_manifest();
    manifest.is_system = TRUE;
    zyl_bridge_init();

    /* First call introspects, second uses the cache — both must arrive typed */
    for (int round = 0; round < 2; round++) {
        e2e_seen_enabled = FALSE;
        reset_webview(&view);
        zyl_bridge_dispatch(&view, &manifest,
            "{\"type\":\"service.request\",\"service\":\"telemetry\","
            "\"method\":\"setEnabled\",\"params\":{\"enabled\":true},\"callbackId\":9}");
        e2e_wait_reply(&view);
        assert(view.last_script && strstr(view.last_script, "_zylCb_9"));
        assert(!strstr(view.last_script, "error"));
        assert(e2e_seen_enabled);
    }

    zyl_bridge_cleanup();
    reset_webview(&view);
    free_manifest_members(&manifest);
    g_dbus_connection_unregister_object(conn, reg);
    g_dbus_node_info_unref(info);
    g_object_unref(conn);
    g_test_dbus_down(bus);
    g_object_unref(bus);
}

/* An update changing only some fields gets the request as it came */
static void test_args_params_object_is_the_whole_request(void) {
    expect_args("Update", "{\"id\":\"c_1\",\"starred\":true}",
                "({'id': <'c_1'>, 'starred': <true>},)");
    expect_args("Update", "{}", "({},)");
}

static void test_args_third_party_app_id_is_the_caller(void) {
    /* A third-party app cannot post as another app — app_id is its own */
    char *got = marshal_as("Post",
        "{\"appId\":\"com.zylos.messages\",\"title\":\"t\",\"priority\":1}",
        "com.evil.app", TRUE, NULL);
    assert(got && strcmp(got, "('com.evil.app', 't', 1)") == 0);
    g_free(got);
    /* ...and needs not send one at all */
    got = marshal_as("Post", "{\"title\":\"t\",\"priority\":1}", "com.evil.app", TRUE, NULL);
    assert(got && strcmp(got, "('com.evil.app', 't', 1)") == 0);
    g_free(got);
    /* System apps (settings acting on another app) keep the param */
    expect_args("Post", "{\"appId\":\"com.x\",\"title\":\"t\",\"priority\":1}", "('com.x', 't', 1)");
}

static void test_args_container_is_the_app_unless_system(void) {
    /* A system app works in the shared files; any other app in its container,
       whatever the request says */
    expect_args("Read", "{\"container\":\"com.x\",\"path\":\"a\"}", "('', 'a')");
    char *got = marshal_as("Read", "{\"container\":\"\",\"path\":\"a\"}", "com.evil.app", TRUE, NULL);
    assert(got && strcmp(got, "('com.evil.app', 'a')") == 0);
    g_free(got);
}

static void test_args_caller_is_always_the_calling_app(void) {
    /* System apps too, and a caller in the request is ignored */
    expect_args("Copy", "{\"caller\":\"com.x\",\"text\":\"hi\"}",
                "('com.zylos.settings', {'caller': <'com.x'>, 'text': <'hi'>})");
    char *got = marshal_as("Copy", "{\"text\":\"hi\"}", "com.evil.app", TRUE, NULL);
    assert(got && strcmp(got, "('com.evil.app', {'text': <'hi'>})") == 0);
    g_free(got);
}

/* ── Service events: D-Bus signals → apps ── */
static char *event_payload(const char *script) {
    /* window.postMessage("<json>",'*') → <json> */
    const char *a = strchr(script, '(') + 1, *b = strrchr(script, ',');
    char *lit = g_strndup(a, (gsize)(b - a));
    JsonNode *n = json_from_string(lit, NULL);
    char *json = g_strdup(json_node_get_string(n));
    json_node_unref(n);
    g_free(lit);
    return json;
}

static void test_event_reaches_only_apps_allowed_to_call_the_service(void) {
    GVariant *params = g_variant_ref_sink(g_variant_new("(sss)", "IDLE", "RINGING", "0101234"));
    const char *none[] = { NULL };
    const char *tel[] = { "telephony", NULL };
    ZylAppManifest plain = perm_manifest("com.example.game", FALSE, none);
    ZylAppManifest dialer = perm_manifest("com.example.dialer", FALSE, tel);
    ZylAppManifest sys = perm_manifest("com.zylos.phone", TRUE, none);
    assert(zyl_bridge_service_event_script(&plain, "telephony", "CallStateChanged", params) == NULL);
    char *script = zyl_bridge_service_event_script(&dialer, "telephony", "CallStateChanged", params);
    assert(script);
    g_free(script);
    script = zyl_bridge_service_event_script(&sys, "telephony", "CallStateChanged", params);
    char *json = event_payload(script);
    JsonObject *o = json_node_get_object(json_from_string(json, NULL));
    assert(strcmp(json_object_get_string_member(o, "type"), "service.event") == 0);
    assert(strcmp(json_object_get_string_member(o, "event"), "CallStateChanged") == 0);
    JsonArray *data = json_object_get_array_member(o, "data");
    assert(json_array_get_length(data) == 3 && strcmp(json_array_get_string_element(data, 1), "RINGING") == 0);
    assert(zyl_bridge_service_event_script(&sys, "nosuch", "X", params) == NULL);
    g_free(json);
    g_free(script);
    g_variant_unref(params);
}

/* Another app's notification (a message, a code) is not a third-party app's
   to read, dismiss or silence; posting and cancelling its own is (Cancel's
   app_id is the caller's, test_args_third_party_app_id_is_the_caller) */
static void test_notifications_are_the_apps_own(void) {
    const char *notif[] = { "notification", NULL };
    const char *read[] = { "notification", "notification.read", NULL };   /* reserved: not granted */
    ZylAppManifest app = perm_manifest("com.example.chat", FALSE, notif);
    ZylAppManifest greedy = perm_manifest("com.example.spy", FALSE, read);
    ZylAppManifest bar = perm_manifest("com.zylos.statusbar", TRUE, NULL);
    assert(app_may_call(&app, "notification") && app_may_call_method(&app, "notification", "post"));
    assert(app_may_call_method(&app, "notification", "cancel"));
    const char *others[] = { "getActive", "clearAll", "setDndMode", "setChannelEnabled", NULL };
    for (int i = 0; others[i]; i++) {
        assert(!app_may_call_method(&app, "notification", others[i]));
        char *dbus_form = capitalize_method(others[i]);   /* the same D-Bus method */
        assert(!app_may_call_method(&app, "notification", dbus_form));
        g_free(dbus_form);
        assert(!app_may_call_method(&greedy, "notification", others[i]));
        assert(app_may_call_method(&bar, "notification", others[i]));
    }
    assert(app_may_call_method(&app, "camera", "capture"));                 /* no method rule */

    GVariant *mine = g_variant_ref_sink(g_variant_new("(tssssi)", (guint64)1, "com.example.chat", "t", "b", "", 1));
    GVariant *theirs = g_variant_ref_sink(g_variant_new("(tssssi)", (guint64)2, "com.zylos.messages", "Mom", "code 1234", "", 1));
    GVariant *gone = g_variant_ref_sink(g_variant_new("(t)", (guint64)2));
    char *s = zyl_bridge_service_event_script(&app, "notification", "NotificationPosted", mine);
    assert(s);
    g_free(s);
    assert(!zyl_bridge_service_event_script(&app, "notification", "NotificationPosted", theirs));
    assert(!zyl_bridge_service_event_script(&greedy, "notification", "NotificationPosted", theirs));
    assert(!zyl_bridge_service_event_script(&app, "notification", "NotificationDismissed", gone));
    s = zyl_bridge_service_event_script(&bar, "notification", "NotificationPosted", theirs);   /* the lock screen's */
    assert(s);
    g_free(s);
    g_variant_unref(mine);
    g_variant_unref(theirs);
    g_variant_unref(gone);
}

/* A geofence is the app's that set it: its enter/exit reaches only that app;
   the position itself reaches every app allowed location */
static void test_geofence_events_are_the_apps_own(void) {
    const char *loc[] = { "location", NULL };
    ZylAppManifest app = perm_manifest("com.example.run", FALSE, loc);
    GVariant *mine = g_variant_ref_sink(g_variant_new("(ss)", "park", "com.example.run"));
    GVariant *theirs = g_variant_ref_sink(g_variant_new("(ss)", "home", "com.example.other"));
    GVariant *fix = g_variant_ref_sink(g_variant_new("(ddd)", 37.5, 127.0, 10.0));
    char *s = zyl_bridge_service_event_script(&app, "location", "GeofenceEnter", mine);
    assert(s);
    g_free(s);
    assert(!zyl_bridge_service_event_script(&app, "location", "GeofenceExit", theirs));
    s = zyl_bridge_service_event_script(&app, "location", "LocationChanged", fix);
    assert(s);
    g_free(s);
    g_variant_unref(mine);
    g_variant_unref(theirs);
    g_variant_unref(fix);
}

/* An alarm going off is its app's: another app never hears its tag */
static void test_alarm_events_are_the_apps_own(void) {
    const char *perms[] = { "notification", NULL };
    ZylAppManifest app = perm_manifest("com.example.run", FALSE, perms);
    GVariant *mine = g_variant_ref_sink(g_variant_new("(ss)", "wake", "com.example.run"));
    GVariant *theirs = g_variant_ref_sink(g_variant_new("(ss)", "wake", "com.zylos.clock"));
    char *s = zyl_bridge_service_event_script(&app, "alarm", "AlarmTriggered", mine);
    assert(s);
    g_free(s);
    assert(!zyl_bridge_service_event_script(&app, "alarm", "AlarmTriggered", theirs));
    g_variant_unref(mine);
    g_variant_unref(theirs);
}

/* The bridge script: the manifest's strings go in as JS string literals, in
   one pass (a value is never read again for tokens), into the top frame only */
static void test_bridge_script_takes_manifest_strings_as_text(void) {
    const char *tpl = "/tmp/zyl-bridge-tpl.js";
    assert(g_file_set_contents(tpl, "id={{APP_ID}};n={{APP_NAME}};v={{APP_VERSION}};t={{BRIDGE_TOKEN}};", -1, NULL));
    ZylAppManifest m = { 0 };
    m.id = "com.example.app";
    m.name = "a\"+x+\"{{APP_VERSION}}</script>\\";
    m.version = "\"+evil()+\"";
    WebKitWebView view = { 0 };
    assert(zyl_bridge_inject(tpl, &view, &m, "0123456789abcdef0123456789abcdef"));
    assert(strcmp(mock_user_script_source,
        "id=\"com.example.app\";"
        "n=\"a\\\"+x+\\\"{{APP_VERSION}}\\u003c/script\\u003e\\\\\";"
        "v=\"\\\"+evil()+\\\"\";"
        "t=\"0123456789abcdef0123456789abcdef\";") == 0);
    assert(mock_user_script_frames == WEBKIT_USER_CONTENT_INJECT_TOP_FRAME);
    unlink(tpl);
}

/* Only a message carrying the view's token is the app's */
static void test_messages_need_the_views_token(void) {
    char token[ZYL_BRIDGE_TOKEN_LEN + 1], other[ZYL_BRIDGE_TOKEN_LEN + 1];
    assert(zyl_bridge_new_token(token) && zyl_bridge_new_token(other));
    assert(strlen(token) == ZYL_BRIDGE_TOKEN_LEN && strspn(token, "0123456789abcdef") == ZYL_BRIDGE_TOKEN_LEN);
    assert(strcmp(token, other) != 0);
    char *msg = g_strconcat(token, "{\"type\":\"app.close\"}", NULL);
    assert(strcmp(zyl_bridge_untoken(msg, token), "{\"type\":\"app.close\"}") == 0);
    assert(!zyl_bridge_untoken(msg, other));
    assert(!zyl_bridge_untoken("{\"type\":\"app.close\"}", token));   /* a frame's message */
    assert(!zyl_bridge_untoken(msg, ""));                              /* a view with no token */
    assert(!zyl_bridge_untoken("0123", token) && !zyl_bridge_untoken(NULL, token));
    g_free(msg);
}

/* The device's power: any app reads it, only system apps act on it */
static void test_power_actions_are_system_only(void) {
    const char *none[] = { NULL };
    ZylAppManifest app = perm_manifest("com.example.app", FALSE, none);
    ZylAppManifest settings = perm_manifest("com.zylos.settings", TRUE, none);
    assert(app_may_call(&app, "power"));
    assert(app_may_call_method(&app, "power", "getState"));
    assert(app_may_call_method(&app, "power", "getCapabilities"));
    const char *acts[] = { "reboot", "shutdown", "suspend", "screenOff", "screenOn", "userActivity",
                           "setBrightness", "acquireWakelock", "releaseWakelock", NULL };
    for (int i = 0; acts[i]; i++) {
        assert(!app_may_call_method(&app, "power", acts[i]));
        char *dbus_form = capitalize_method(acts[i]);
        assert(!app_may_call_method(&app, "power", dbus_form));
        g_free(dbus_form);
        assert(app_may_call_method(&settings, "power", acts[i]));
    }
}

/* Users: adding, switching and removing (with the user's data) is the OS's */
static void test_users_are_system_only(void) {
    const char *any[] = { "storage", "notification", NULL };
    ZylAppManifest app = perm_manifest("com.example.app", FALSE, any);
    ZylAppManifest settings = perm_manifest("com.zylos.settings", TRUE, NULL);
    assert(!app_may_call(&app, "user"));
    assert(app_may_call(&settings, "user"));
}

static int g_events;
static void count_event(const char *service, const char *signal, GVariant *params, gpointer d) {
    (void)params; (void)d;
    if (strcmp(service, "telephony") == 0 && strcmp(signal, "CallStateChanged") == 0) g_events++;
}

static void test_events_only_from_the_services_own_name(void) {
    if (!g_find_program_in_path("dbus-daemon")) { printf("SKIP (no dbus-daemon) "); return; }
    GTestDBus *bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    /* Zyl services live on the system bus; the private bus plays that part */
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address(bus), TRUE);
    const char *addr = g_test_dbus_get_bus_address(bus);
    GDBusConnectionFlags f = G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                             G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION;
    GDBusConnection *wam = g_dbus_connection_new_for_address_sync(addr, f, NULL, NULL, NULL);
    GDBusConnection *svc = g_dbus_connection_new_for_address_sync(addr, f, NULL, NULL, NULL);
    GDBusConnection *rogue = g_dbus_connection_new_for_address_sync(addr, f, NULL, NULL, NULL);
    GVariant *r = g_dbus_connection_call_sync(svc, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.zylos.Telephony", 0),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_variant_unref(r);
    zyl_bridge_subscribe_service_events(wam, count_event, NULL);
    gint64 end = g_get_monotonic_time() + G_USEC_PER_SEC / 2;
    while (g_get_monotonic_time() < end) g_main_context_iteration(NULL, FALSE);   /* match rules in */

    /* A process that does not own org.zylos.Telephony fakes an incoming call */
    g_dbus_connection_emit_signal(rogue, NULL, "/org/zylos/Telephony", "org.zylos.Telephony",
        "CallStateChanged", g_variant_new("(sss)", "IDLE", "RINGING", "0"), NULL);
    g_dbus_connection_emit_signal(svc, NULL, "/org/zylos/Telephony", "org.zylos.Telephony",
        "CallStateChanged", g_variant_new("(sss)", "IDLE", "RINGING", "1"), NULL);
    g_dbus_connection_flush_sync(rogue, NULL, NULL);
    g_dbus_connection_flush_sync(svc, NULL, NULL);
    end = g_get_monotonic_time() + G_USEC_PER_SEC;
    while (g_get_monotonic_time() < end) g_main_context_iteration(NULL, FALSE);
    assert(g_events == 1);

    g_object_unref(rogue);
    g_object_unref(svc);
    g_object_unref(wam);
    g_test_dbus_down(bus);
    g_object_unref(bus);
}

int main(void)
{
    printf("=== Zyl OS Bridge Dispatch Tests ===\n");
    RUN_TEST(test_dispatch_routes_callback_id);
    RUN_TEST(test_unknown_type_returns_error);
    RUN_TEST(test_no_unchecked_service_shortcut);
    RUN_TEST(test_malformed_json_does_not_respond);
    RUN_TEST(test_only_one_json_object_accepted);
    RUN_TEST(test_policy_unknown_app_is_denied);
    RUN_TEST(test_policy_system_app_may_call_everything);
    RUN_TEST(test_policy_declared_permission_grants_its_service);
    RUN_TEST(test_policy_reserved_permissions_cannot_be_self_declared);
    RUN_TEST(test_policy_services_without_requirement_are_open);
    RUN_TEST(test_policy_unreviewed_service_fails_closed);
    RUN_TEST(test_policy_user_revocation_takes_the_service_away);
    RUN_TEST(test_policy_revocation_list_is_comma_separated);
    RUN_TEST(test_alarm_events_are_the_apps_own);
    RUN_TEST(test_power_actions_are_system_only);
    RUN_TEST(test_app_holds_follows_declaration_and_revocation);
    RUN_TEST(test_revocation_reload_names_only_changed_apps);
    RUN_TEST(test_args_no_input_is_empty_tuple);
    RUN_TEST(test_args_follow_signature_not_one_json_string);
    RUN_TEST(test_args_camel_case_fills_snake_case);
    RUN_TEST(test_args_integer_fills_double);
    RUN_TEST(test_args_missing_or_wrong_type_names_the_argument);
    RUN_TEST(test_args_third_party_app_id_is_the_caller);
    RUN_TEST(test_args_caller_is_always_the_calling_app);
    RUN_TEST(test_args_container_is_the_app_unless_system);
    RUN_TEST(test_event_reaches_only_apps_allowed_to_call_the_service);
    RUN_TEST(test_notifications_are_the_apps_own);
    RUN_TEST(test_geofence_events_are_the_apps_own);
    RUN_TEST(test_users_are_system_only);
    RUN_TEST(test_bridge_script_takes_manifest_strings_as_text);
    RUN_TEST(test_messages_need_the_views_token);
    RUN_TEST(test_events_only_from_the_services_own_name);
    RUN_TEST(test_args_params_object_is_the_whole_request);
    RUN_TEST(test_service_request_reaches_typed_dbus_method);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
