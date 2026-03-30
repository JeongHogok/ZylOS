/* ----------------------------------------------------------
 * [Clean Architecture] Infrastructure Layer - Adapter
 *
 * 역할: JS-Native 브릿지 -- JS 주입, 핸들러 레지스트리, 메시지 디스패치, 에러 처리
 * 수행범위: bridge.js 로드/주입, 타입별 핸들러 테이블, JSON 유효성 검사, 응답 라우팅
 * 의존방향: bridge.h -> manifest.h
 * SOLID: OCP -- 핸들러 등록으로 확장, SRP -- 디스패치/검증/응답 분리
 * ---------------------------------------------------------- */

#include "bridge.h"

#include <stdio.h>
#include <string.h>
#include <json-glib/json-glib.h>

/* == Handler Registry Entry ================================= */

typedef struct {
    ZylBridgeHandler handler;
    gpointer         user_data;
} BridgeHandlerEntry;

/* == Global Handler Registry ================================ */

static GHashTable *handler_registry = NULL;  /* key: char*, value: BridgeHandlerEntry* */

static void handler_entry_free(gpointer data)
{
    g_free(data);
}

/* == Registry Init / Cleanup ================================ */

/* Forward declarations for default handlers */
static void handle_app_close(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest, gpointer data);
static void handle_app_launch(const char *type, gpointer msg_obj,
                              ZylAppManifest *manifest, gpointer data);
static void handle_notification_create(const char *type, gpointer msg_obj,
                                       ZylAppManifest *manifest, gpointer data);
static void handle_battery_get_level(const char *type, gpointer msg_obj,
                                     ZylAppManifest *manifest, gpointer data);
static void handle_wifi_scan(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest, gpointer data);
static void handle_settings_get(const char *type, gpointer msg_obj,
                                ZylAppManifest *manifest, gpointer data);
static void handle_settings_update(const char *type, gpointer msg_obj,
                                   ZylAppManifest *manifest, gpointer data);

void zyl_bridge_init(void)
{
    if (handler_registry) return;

    handler_registry = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, handler_entry_free);

    /* Register default handlers */
    zyl_bridge_register_handler("app.close",           handle_app_close, NULL);
    zyl_bridge_register_handler("app.launch",          handle_app_launch, NULL);
    zyl_bridge_register_handler("notification.create", handle_notification_create, NULL);
    zyl_bridge_register_handler("battery.getLevel",    handle_battery_get_level, NULL);
    zyl_bridge_register_handler("wifi.scan",           handle_wifi_scan, NULL);
    zyl_bridge_register_handler("settings.get",        handle_settings_get, NULL);
    zyl_bridge_register_handler("settings.update",     handle_settings_update, NULL);
    zyl_bridge_register_handler("service.request",     handle_service_request, NULL);
}

void zyl_bridge_cleanup(void)
{
    if (handler_registry) {
        g_hash_table_destroy(handler_registry);
        handler_registry = NULL;
    }
}

/* == Handler Registration =================================== */

int zyl_bridge_register_handler(const char       *type,
                                ZylBridgeHandler  handler,
                                gpointer          data)
{
    if (!type || !handler) return -1;
    if (!handler_registry) return -1;

    BridgeHandlerEntry *entry = g_new(BridgeHandlerEntry, 1);
    if (!entry) return -1;

    entry->handler   = handler;
    entry->user_data = data;

    /* g_hash_table_replace frees old key+value if present */
    g_hash_table_replace(handler_registry, g_strdup(type), entry);
    return 0;
}

int zyl_bridge_unregister_handler(const char *type)
{
    if (!type || !handler_registry) return -1;

    gboolean removed = g_hash_table_remove(handler_registry, type);
    return removed ? 0 : -1;
}

/* == Load and inject the JS bridge ========================== */

gboolean zyl_bridge_inject(const char      *bridge_js_path,
                           WebKitWebView   *webview,
                           ZylAppManifest  *manifest)
{
    gchar *template = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!bridge_js_path || !webview || !manifest) {
        g_warning("Bridge: inject called with NULL argument");
        return FALSE;
    }

    if (!g_file_get_contents(bridge_js_path, &template, &length, &error)) {
        g_warning("Failed to load bridge script %s: %s",
                  bridge_js_path, error->message);
        g_error_free(error);
        return FALSE;
    }

    /* Substitute tokens: {{APP_ID}}, {{APP_NAME}}, {{APP_VERSION}} */
    gchar *s1 = g_strdup(template);
    if (!s1) {
        g_free(template);
        return FALSE;
    }
    gchar *s2, *s3;

    s2 = g_strjoinv(manifest->id,      g_strsplit(s1, "{{APP_ID}}", -1));
    g_free(s1);
    if (!s2) { g_free(template); return FALSE; }

    s3 = g_strjoinv(manifest->name,    g_strsplit(s2, "{{APP_NAME}}", -1));
    g_free(s2);
    if (!s3) { g_free(template); return FALSE; }

    s1 = g_strjoinv(manifest->version, g_strsplit(s3, "{{APP_VERSION}}", -1));
    g_free(s3);
    if (!s1) { g_free(template); return FALSE; }

    /* Inject via WebKit user-content manager */
    WebKitUserContentManager *ucm =
        webkit_web_view_get_user_content_manager(webview);
    WebKitUserScript *user_script = webkit_user_script_new(
        s1,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, user_script);
    webkit_user_script_unref(user_script);

    g_free(s1);
    g_free(template);
    return TRUE;
}

/* == H13: JSON Pre-validation =============================== */

/*
 * Quick structural check: verify that braces/brackets are balanced.
 * This prevents feeding obviously malformed strings to the full parser.
 */
static gboolean json_prevalidate(const char *str)
{
    if (!str || str[0] == '\0') return FALSE;

    int brace_depth   = 0;
    int bracket_depth = 0;
    gboolean in_string = FALSE;
    gboolean escaped   = FALSE;

    for (const char *p = str; *p; p++) {
        if (escaped) {
            escaped = FALSE;
            continue;
        }
        if (*p == '\\' && in_string) {
            escaped = TRUE;
            continue;
        }
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;

        switch (*p) {
        case '{': brace_depth++;   break;
        case '}': brace_depth--;   break;
        case '[': bracket_depth++; break;
        case ']': bracket_depth--; break;
        }

        if (brace_depth < 0 || bracket_depth < 0) return FALSE;
    }

    return (brace_depth == 0 && bracket_depth == 0 && !in_string);
}

/* == Send response back to JavaScript ======================= */

int zyl_bridge_respond(WebKitWebView *webview,
                       int            callback_id,
                       const char    *json_data)
{
    if (!webview || !json_data) return -1;

    /* Build JS: window._zylCb_{id}({data}) — dynamic buffer for large responses */
    size_t needed = strlen(json_data) + 128; /* overhead for JS wrapper */
    char *script = malloc(needed);
    if (!script) return -1;

    int written = snprintf(script, needed,
                           "if(window._zylCb_%d){window._zylCb_%d(%s);delete window._zylCb_%d;}",
                           callback_id, callback_id, json_data, callback_id);

    if (written < 0 || (size_t)written >= needed) {
        g_warning("Bridge: response script too large for callback_id=%d", callback_id);
        free(script);
        return -1;
    }

    webkit_web_view_evaluate_javascript(webview, script, -1, NULL, NULL, NULL, NULL, NULL);
    free(script);
    return 0;
}

/* == Helper: Send error response to JS ====================== */

static void send_error_response(WebKitWebView *webview,
                                int            callback_id,
                                const char    *error_msg)
{
    if (!webview || callback_id < 0) return;

    char json_buf[512];
    int written = snprintf(json_buf, sizeof(json_buf),
                           "{\"error\":true,\"message\":\"%s\"}", error_msg);

    if (written < 0 || (size_t)written >= sizeof(json_buf)) {
        /* Fallback: generic error */
        snprintf(json_buf, sizeof(json_buf),
                 "{\"error\":true,\"message\":\"internal error\"}");
    }

    zyl_bridge_respond(webview, callback_id, json_buf);
}

/* == Dispatch an incoming bridge message ==================== */

void zyl_bridge_dispatch(WebKitWebView    *webview,
                         ZylAppManifest   *manifest,
                         const char       *msg_str)
{
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";

    /* H13: Validate input is not NULL or empty */
    if (!msg_str || msg_str[0] == '\0') {
        g_warning("Bridge: empty message from %s", app_id);
        return;
    }

    /* H13: Pre-validate JSON structure */
    if (!json_prevalidate(msg_str)) {
        g_warning("Bridge: malformed JSON from %s (unbalanced braces/brackets)", app_id);
        return;
    }

    /* Parse JSON */
    JsonParser *parser = json_parser_new();
    GError *parse_error = NULL;

    if (!json_parser_load_from_data(parser, msg_str, -1, &parse_error)) {
        g_warning("Bridge: JSON parse failed from %s: %s",
                  app_id, parse_error ? parse_error->message : "unknown error");
        if (parse_error) g_error_free(parse_error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_warning("Bridge: JSON root is not an object from %s", app_id);
        g_object_unref(parser);
        return;
    }

    JsonObject *msg = json_node_get_object(root);

    /* H13: Validate required "type" field */
    if (!json_object_has_member(msg, "type")) {
        g_warning("Bridge: message missing 'type' field from %s", app_id);
        g_object_unref(parser);
        return;
    }

    const char *type = json_object_get_string_member(msg, "type");
    if (!type || type[0] == '\0') {
        g_warning("Bridge: empty 'type' field from %s", app_id);
        g_object_unref(parser);
        return;
    }

    /* Extract optional callback_id for async response */
    int callback_id = -1;
    if (json_object_has_member(msg, "callbackId")) {
        callback_id = (int)json_object_get_int_member(msg, "callbackId");
    }

    g_message("Bridge message from %s: type=%s callbackId=%d", app_id, type, callback_id);

    /* H5: Look up handler in registry */
    if (!handler_registry) {
        g_warning("Bridge: handler registry not initialized");
        if (callback_id >= 0 && webview) {
            send_error_response(webview, callback_id, "bridge not initialized");
        }
        g_object_unref(parser);
        return;
    }

    BridgeHandlerEntry *entry = g_hash_table_lookup(handler_registry, type);
    if (!entry) {
        g_warning("Bridge: no handler for type '%s' from %s", type, app_id);
        if (callback_id >= 0 && webview) {
            send_error_response(webview, callback_id, "unknown message type");
        }
        g_object_unref(parser);
        return;
    }

    /* H13: Call handler with error checking -- never crash on bad input */
    entry->handler(type, msg, manifest, entry->user_data);

    g_object_unref(parser);
}

/* == Default Handlers ======================================= */

static void handle_app_close(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)msg_obj;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    g_info("Bridge: app.close requested by %s", app_id);
    /* WAM lifecycle will handle actual close via D-Bus */
}

static void handle_app_launch(const char *type, gpointer msg_obj,
                              ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *target = NULL;
    if (json_object_has_member(msg, "appId")) {
        target = json_object_get_string_member(msg, "appId");
    }

    g_info("Bridge: app.launch from %s -> target=%s",
           app_id, target ? target : "(none)");
    /* WAM lifecycle will handle actual launch via D-Bus */
}

static void handle_notification_create(const char *type, gpointer msg_obj,
                                       ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *title = NULL;
    const char *body  = NULL;

    if (json_object_has_member(msg, "title"))
        title = json_object_get_string_member(msg, "title");
    if (json_object_has_member(msg, "body"))
        body = json_object_get_string_member(msg, "body");

    g_info("Bridge: notification.create from %s title='%s'",
           app_id, title ? title : "(none)");
    (void)body;
    /* Forward to notification service via D-Bus */
}

static void handle_battery_get_level(const char *type, gpointer msg_obj,
                                     ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)msg_obj;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    g_info("Bridge: battery.getLevel from %s", app_id);
    /* Forward to power service via D-Bus */
}

static void handle_wifi_scan(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)msg_obj;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    g_info("Bridge: wifi.scan from %s", app_id);
    /* Forward to connectivity service via D-Bus */
}

static void handle_settings_get(const char *type, gpointer msg_obj,
                                ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *key = NULL;
    if (json_object_has_member(msg, "key"))
        key = json_object_get_string_member(msg, "key");

    g_info("Bridge: settings.get from %s key='%s'",
           app_id, key ? key : "(all)");
    /* Forward to settings service via D-Bus */
}

static void handle_settings_update(const char *type, gpointer msg_obj,
                                   ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *key = NULL;
    if (json_object_has_member(msg, "key"))
        key = json_object_get_string_member(msg, "key");

    g_info("Bridge: settings.update from %s key='%s'",
           app_id, key ? key : "(none)");
    /* Forward to settings service via D-Bus */
}

/* == service.request → D-Bus dispatch ========================
 * This is the core IPC handler that routes app service requests
 * to the appropriate D-Bus system service on real hardware.
 * Maps: service name → D-Bus bus name + object path + method
 * ============================================================ */

typedef struct {
    const char *service;
    const char *dbus_name;
    const char *dbus_path;
    const char *dbus_iface;
} ServiceRoute;

static const ServiceRoute SERVICE_ROUTES[] = {
    {"notification", "org.zylos.Notification",      "/org/zylos/Notification",      "org.zylos.Notification"},
    {"power",        "org.zylos.PowerManager",       "/org/zylos/PowerManager",       "org.zylos.PowerManager"},
    {"display",      "org.zylos.DisplayManager",     "/org/zylos/DisplayManager",     "org.zylos.DisplayManager"},
    {"input",        "org.zylos.InputService",       "/org/zylos/InputService",       "org.zylos.InputService"},
    {"sensors",      "org.zylos.SensorService",      "/org/zylos/SensorService",      "org.zylos.SensorService"},
    {"location",     "org.zylos.LocationService",    "/org/zylos/LocationService",    "org.zylos.LocationService"},
    {"telephony",    "org.zylos.Telephony",           "/org/zylos/Telephony",           "org.zylos.Telephony"},
    {"usb",          "org.zylos.UsbManager",          "/org/zylos/UsbManager",          "org.zylos.UsbManager"},
    {"user",         "org.zylos.UserManager",         "/org/zylos/UserManager",         "org.zylos.UserManager"},
    {"credential",   "org.zylos.CredentialManager",   "/org/zylos/CredentialManager",   "org.zylos.CredentialManager"},
    {"accessibility","org.zylos.Accessibility",       "/org/zylos/Accessibility",       "org.zylos.Accessibility"},
    {"logger",       "org.zylos.Logger",              "/org/zylos/Logger",              "org.zylos.Logger"},
    {"camera",       "org.zylos.CameraService",       "/org/zylos/CameraService",       "org.zylos.CameraService"},
    {"audio",        "org.zylos.AudioService",        "/org/zylos/AudioService",        "org.zylos.AudioService"},
    {"bluetooth",    "org.zylos.BluetoothService",    "/org/zylos/BluetoothService",    "org.zylos.BluetoothService"},
    {"wifi",         "org.zylos.WifiService",         "/org/zylos/WifiService",         "org.zylos.WifiService"},
    {NULL, NULL, NULL, NULL}
};

static const ServiceRoute *find_route(const char *service) {
    for (int i = 0; SERVICE_ROUTES[i].service; i++) {
        if (strcmp(SERVICE_ROUTES[i].service, service) == 0)
            return &SERVICE_ROUTES[i];
    }
    return NULL;
}

/**
 * Capitalize first letter of method name for D-Bus convention.
 * "getState" → "GetState"
 */
static char *capitalize_method(const char *method) {
    if (!method || !method[0]) return g_strdup("");
    char *cap = g_strdup(method);
    if (cap[0] >= 'a' && cap[0] <= 'z') cap[0] -= 32;
    return cap;
}

static void handle_service_request(const char *type, gpointer msg_obj,
                                    ZylAppManifest *manifest, gpointer data)
{
    (void)type;
    (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    if (!json_object_has_member(msg, "service") ||
        !json_object_has_member(msg, "method")) {
        g_warning("Bridge: service.request missing service/method from %s",
                  app_id);
        return;
    }

    const char *service = json_object_get_string_member(msg, "service");
    const char *method  = json_object_get_string_member(msg, "method");

    const ServiceRoute *route = find_route(service);
    if (!route) {
        g_warning("Bridge: no D-Bus route for service '%s' from %s",
                  service, app_id);
        return;
    }

    char *dbus_method = capitalize_method(method);

    g_info("Bridge: service.request from %s → %s.%s → D-Bus %s.%s",
           app_id, service, method, route->dbus_name, dbus_method);

    /* Async D-Bus call to the target service.
     * Parameters are forwarded as a JSON string — the receiving service
     * parses them according to its D-Bus interface. */
    GError *err = NULL;
    GDBusConnection *session = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (err) {
        g_warning("Bridge: session bus error: %s", err->message);
        g_error_free(err);
        g_free(dbus_method);
        return;
    }

    /* Build D-Bus call parameters from the JSON params object */
    GVariant *call_params = NULL;
    if (json_object_has_member(msg, "params")) {
        JsonNode *params_node = json_object_get_member(msg, "params");
        JsonGenerator *gen = json_generator_new();
        json_generator_set_root(gen, params_node);
        gchar *params_json = json_generator_to_data(gen, NULL);
        call_params = g_variant_new("(s)", params_json);
        g_free(params_json);
        g_object_unref(gen);
    }

    /* Fire-and-forget async call */
    g_dbus_connection_call(session, route->dbus_name, route->dbus_path,
        route->dbus_iface, dbus_method, call_params,
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);

    g_object_unref(session);
    g_free(dbus_method);
}
