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
#include <stdlib.h>
#include <string.h>
#include <json-glib/json-glib.h>

/* == Handler Registry Entry ================================= */

typedef struct {
    ZylBridgeHandler handler;
    gpointer         user_data;
} BridgeHandlerEntry;

/* == Global Handler Registry ================================ */

static GHashTable *handler_registry = NULL;

static void handler_entry_free(gpointer data)
{
    g_free(data);
}

/* == Forward declarations for default handlers ============== */
static void handle_app_close(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest,
                             ZylBridgeReplyCtx *reply_ctx, gpointer data);
static void handle_app_launch(const char *type, gpointer msg_obj,
                              ZylAppManifest *manifest,
                              ZylBridgeReplyCtx *reply_ctx, gpointer data);
static void handle_notification_create(const char *type, gpointer msg_obj,
                                       ZylAppManifest *manifest,
                                       ZylBridgeReplyCtx *reply_ctx, gpointer data);
static void handle_battery_get_level(const char *type, gpointer msg_obj,
                                     ZylAppManifest *manifest,
                                     ZylBridgeReplyCtx *reply_ctx, gpointer data);
static void handle_wifi_scan(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest,
                             ZylBridgeReplyCtx *reply_ctx, gpointer data);
static void handle_settings_get(const char *type, gpointer msg_obj,
                                ZylAppManifest *manifest,
                                ZylBridgeReplyCtx *reply_ctx, gpointer data);
static void handle_settings_update(const char *type, gpointer msg_obj,
                                   ZylAppManifest *manifest,
                                   ZylBridgeReplyCtx *reply_ctx, gpointer data);
static void handle_service_request(const char *type, gpointer msg_obj,
                                   ZylAppManifest *manifest,
                                   ZylBridgeReplyCtx *reply_ctx, gpointer data);

/* == Registry Init / Cleanup ================================ */

void zyl_bridge_init(void)
{
    if (handler_registry) return;

    handler_registry = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, handler_entry_free);

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

    g_hash_table_replace(handler_registry, g_strdup(type), entry);
    return 0;
}

int zyl_bridge_unregister_handler(const char *type)
{
    if (!type || !handler_registry) return -1;
    return g_hash_table_remove(handler_registry, type) ? 0 : -1;
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

    gchar *s1 = g_strdup(template);
    if (!s1) { g_free(template); return FALSE; }
    gchar *s2, *s3;
    gchar **parts;

    parts = g_strsplit(s1, "{{APP_ID}}", -1);
    s2 = g_strjoinv(manifest->id, parts);
    g_strfreev(parts);
    g_free(s1);
    if (!s2) { g_free(template); return FALSE; }

    parts = g_strsplit(s2, "{{APP_NAME}}", -1);
    s3 = g_strjoinv(manifest->name, parts);
    g_strfreev(parts);
    g_free(s2);
    if (!s3) { g_free(template); return FALSE; }

    parts = g_strsplit(s3, "{{APP_VERSION}}", -1);
    s1 = g_strjoinv(manifest->version, parts);
    g_strfreev(parts);
    g_free(s3);
    if (!s1) { g_free(template); return FALSE; }

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

/* == JSON Pre-validation ==================================== */

static gboolean json_prevalidate(const char *str)
{
    if (!str || str[0] == '\0') return FALSE;

    int brace_depth   = 0;
    int bracket_depth = 0;
    gboolean in_string = FALSE;
    gboolean escaped   = FALSE;

    for (const char *p = str; *p; p++) {
        if (escaped) { escaped = FALSE; continue; }
        if (*p == '\\' && in_string) { escaped = TRUE; continue; }
        if (*p == '"') { in_string = !in_string; continue; }
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
    if (callback_id < 0) return 0; /* caller did not request a response */

    size_t needed = strlen(json_data) + 128;
    char *script = malloc(needed);
    if (!script) return -1;

    int written = snprintf(script, needed,
                           "if(window._zylCb_%d){window._zylCb_%d(%s);"
                           "delete window._zylCb_%d;}",
                           callback_id, callback_id, json_data, callback_id);

    if (written < 0 || (size_t)written >= needed) {
        g_warning("Bridge: response script too large for callback_id=%d", callback_id);
        free(script);
        return -1;
    }

    webkit_web_view_evaluate_javascript(webview, script, -1,
                                        NULL, NULL, NULL, NULL, NULL);
    free(script);
    return 0;
}

void zyl_bridge_reply_error(const ZylBridgeReplyCtx *ctx,
                             const char              *message)
{
    if (!ctx || ctx->callback_id < 0) return;

    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "error");
    json_builder_add_boolean_value(builder, TRUE);
    json_builder_set_member_name(builder, "message");
    json_builder_add_string_value(builder,
                                  message ? message : "internal error");
    json_builder_end_object(builder);

    JsonNode *node = json_builder_get_root(builder);
    json_generator_set_root(gen, node);
    gchar *json_str = json_generator_to_data(gen, NULL);
    zyl_bridge_respond(ctx->webview, ctx->callback_id, json_str);

    g_free(json_str);
    json_node_free(node);
    g_object_unref(gen);
    g_object_unref(builder);
}

/* == Dispatch an incoming bridge message ==================== */

void zyl_bridge_dispatch(WebKitWebView    *webview,
                         ZylAppManifest   *manifest,
                         const char       *msg_str)
{
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";

    if (!msg_str || msg_str[0] == '\0') {
        g_warning("Bridge: empty message from %s", app_id);
        return;
    }

    if (!json_prevalidate(msg_str)) {
        g_warning("Bridge: malformed JSON from %s (unbalanced braces/brackets)", app_id);
        return;
    }

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

    /* Extract optional callbackId for async response routing */
    int callback_id = -1;
    if (json_object_has_member(msg, "callbackId")) {
        callback_id = (int)json_object_get_int_member(msg, "callbackId");
    }
    /* Also support legacy _cbId from bridge.js */
    if (callback_id < 0 && json_object_has_member(msg, "_cbId")) {
        callback_id = (int)json_object_get_int_member(msg, "_cbId");
    }

    g_message("Bridge message from %s: type=%s callbackId=%d",
              app_id, type, callback_id);

    if (!handler_registry) {
        g_warning("Bridge: handler registry not initialized");
        if (callback_id >= 0 && webview) {
            ZylBridgeReplyCtx ctx = { webview, callback_id };
            zyl_bridge_reply_error(&ctx, "bridge not initialized");
        }
        g_object_unref(parser);
        return;
    }

    BridgeHandlerEntry *entry = g_hash_table_lookup(handler_registry, type);
    if (!entry) {
        g_warning("Bridge: no handler for type '%s' from %s", type, app_id);
        if (callback_id >= 0 && webview) {
            ZylBridgeReplyCtx ctx = { webview, callback_id };
            zyl_bridge_reply_error(&ctx, "unknown message type");
        }
        g_object_unref(parser);
        return;
    }

    ZylBridgeReplyCtx reply_ctx = { webview, callback_id };
    entry->handler(type, msg, manifest, &reply_ctx, entry->user_data);

    g_object_unref(parser);
}

/* == D-Bus async callback context ========================== */

typedef struct {
    WebKitWebView *webview;
    int            callback_id;
} DbusReplyCtx;

static JsonNode *json_from_integral_variant(GVariant *value);
static JsonNode *json_from_variant(GVariant *value)
{
    if (!value)
        return json_node_init_null(json_node_alloc());

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
        GVariant *inner = g_variant_get_variant(value);
        JsonNode *n = json_from_variant(inner);
        g_variant_unref(inner);
        return n;
    }

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_SIGNATURE)) {
        JsonNode *n = json_node_alloc();
        json_node_init_string(n, g_variant_get_string(value, NULL));
        return n;
    }

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
        JsonNode *n = json_node_alloc();
        json_node_init_boolean(n, g_variant_get_boolean(value));
        return n;
    }

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BYTE) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_INT16) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_UINT16) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_INT32) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_INT64) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE_HANDLE)) {
        return json_from_integral_variant(value);
    }

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
        JsonNode *n = json_node_alloc();
        json_node_init_double(n, g_variant_get_double(value));
        return n;
    }

    if (g_variant_is_of_type(value, G_VARIANT_TYPE("ay"))) {
        gsize len = 0;
        const guint8 *bytes = g_variant_get_fixed_array(value, &len, sizeof(guint8));
        gchar *b64 = g_base64_encode(bytes, len);
        JsonNode *n = json_node_alloc();
        json_node_init_string(n, b64);
        g_free(b64);
        return n;
    }

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_VARDICT) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}")) ||
        g_variant_is_of_type(value, G_VARIANT_TYPE("a{ss}"))) {
        JsonObject *obj = json_object_new();
        gsize n_entries = g_variant_n_children(value);
        for (gsize i = 0; i < n_entries; i++) {
            GVariant *entry = g_variant_get_child_value(value, i);
            GVariant *k = g_variant_get_child_value(entry, 0);
            GVariant *v = g_variant_get_child_value(entry, 1);
            const gchar *key = g_variant_get_string(k, NULL);
            json_object_set_member(obj, key, json_from_variant(v));
            g_variant_unref(v);
            g_variant_unref(k);
            g_variant_unref(entry);
        }
        JsonNode *n = json_node_alloc();
        json_node_init_object(n, obj);
        return n;
    }

    if (g_variant_is_container(value)) {
        gsize n_children = g_variant_n_children(value);
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_TUPLE) && n_children == 1) {
            GVariant *child = g_variant_get_child_value(value, 0);
            JsonNode *n = json_from_variant(child);
            g_variant_unref(child);
            return n;
        }

        JsonArray *arr = json_array_new();
        for (gsize i = 0; i < n_children; i++) {
            GVariant *child = g_variant_get_child_value(value, i);
            json_array_add_element(arr, json_from_variant(child));
            g_variant_unref(child);
        }
        JsonNode *n = json_node_alloc();
        json_node_init_array(n, arr);
        return n;
    }

    /* Fallback: textual serialisation for unsupported/rare variant types. */
    gchar *printed = g_variant_print(value, TRUE);
    JsonNode *n = json_node_alloc();
    json_node_init_string(n, printed);
    g_free(printed);
    return n;
}

static JsonNode *json_from_integral_variant(GVariant *value)
{
    JsonNode *n = json_node_alloc();
    gint64 out = 0;

    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BYTE))
        out = g_variant_get_byte(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT16))
        out = g_variant_get_int16(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT16))
        out = g_variant_get_uint16(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
        out = g_variant_get_int32(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32))
        out = g_variant_get_uint32(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64))
        out = g_variant_get_int64(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64))
        out = (gint64)g_variant_get_uint64(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_HANDLE))
        out = g_variant_get_handle(value);

    json_node_init_int(n, out);
    return n;
}

static void on_dbus_call_done(GObject      *source,
                              GAsyncResult *res,
                              gpointer      user_data)
{
    DbusReplyCtx *ctx = user_data;
    GError *err = NULL;

    GVariant *reply = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), res, &err);

    if (ctx->callback_id >= 0 && ctx->webview) {
        if (err) {
            char json_buf[512];
            char safe_msg[256];
            size_t j = 0;
            for (const char *p = err->message;
                 *p && j < sizeof(safe_msg) - 1; p++) {
                if (*p != '"' && *p != '\\') safe_msg[j++] = *p;
            }
            safe_msg[j] = '\0';
            snprintf(json_buf, sizeof(json_buf),
                     "{\"error\":true,\"message\":\"%s\"}", safe_msg);
            zyl_bridge_respond(ctx->webview, ctx->callback_id, json_buf);
        } else if (reply) {
            JsonNode *node;
            JsonGenerator *gen = json_generator_new();

            /* D-Bus methods with no payload return the unit tuple `()`. */
            if (g_variant_is_of_type(reply, G_VARIANT_TYPE_TUPLE) &&
                g_variant_n_children(reply) == 0) {
                JsonObject *obj = json_object_new();
                json_object_set_boolean_member(obj, "ok", TRUE);
                node = json_node_alloc();
                json_node_init_object(node, obj);
            } else if (g_variant_is_of_type(reply, G_VARIANT_TYPE_BYTE) ||
                       g_variant_is_of_type(reply, G_VARIANT_TYPE_INT16) ||
                       g_variant_is_of_type(reply, G_VARIANT_TYPE_UINT16) ||
                       g_variant_is_of_type(reply, G_VARIANT_TYPE_INT32) ||
                       g_variant_is_of_type(reply, G_VARIANT_TYPE_UINT32) ||
                       g_variant_is_of_type(reply, G_VARIANT_TYPE_INT64) ||
                       g_variant_is_of_type(reply, G_VARIANT_TYPE_UINT64) ||
                       g_variant_is_of_type(reply, G_VARIANT_TYPE_HANDLE)) {
                node = json_from_integral_variant(reply);
            } else {
                node = json_from_variant(reply);
            }

            json_generator_set_root(gen, node);
            gchar *json_str = json_generator_to_data(gen, NULL);
            zyl_bridge_respond(ctx->webview, ctx->callback_id, json_str);
            g_free(json_str);
            json_node_free(node);
            g_object_unref(gen);
            g_variant_unref(reply);
        }
    } else if (reply) {
        g_variant_unref(reply);
    }

    if (err) g_error_free(err);
    g_object_unref(source);
    g_free(ctx);
}

/* == Default Handlers ======================================= */

static void handle_app_close(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest,
                             ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)msg_obj; (void)data; (void)reply_ctx;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    g_info("Bridge: app.close requested by %s", app_id);
    /* WAM lifecycle handles actual close via wam_handle_app_close */
}

static void handle_app_launch(const char *type, gpointer msg_obj,
                              ZylAppManifest *manifest,
                              ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)data; (void)reply_ctx;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *target = NULL;
    if (json_object_has_member(msg, "appId"))
        target = json_object_get_string_member(msg, "appId");

    g_info("Bridge: app.launch from %s -> target=%s",
           app_id, target ? target : "(none)");
}

static void handle_notification_create(const char *type, gpointer msg_obj,
                                       ZylAppManifest *manifest,
                                       ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *title = NULL;
    const char *body  = NULL;
    if (json_object_has_member(msg, "title"))
        title = json_object_get_string_member(msg, "title");
    if (json_object_has_member(msg, "body"))
        body  = json_object_get_string_member(msg, "body");

    g_info("Bridge: notification.create from %s title='%s'",
           app_id, title ? title : "(none)");
    (void)body;

    /* If a callback was requested, acknowledge immediately */
    if (reply_ctx->callback_id >= 0)
        zyl_bridge_respond(reply_ctx->webview, reply_ctx->callback_id,
                           "{\"ok\":true}");
}

/* -- Battery: async D-Bus query to PowerManager ------------ */
static void handle_battery_get_level(const char *type, gpointer msg_obj,
                                     ZylAppManifest *manifest,
                                     ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)msg_obj; (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    g_info("Bridge: battery.getLevel from %s", app_id);

    if (reply_ctx->callback_id < 0) return; /* no response needed */

    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (err) {
        zyl_bridge_reply_error(reply_ctx, "dbus unavailable");
        g_error_free(err);
        return;
    }

    DbusReplyCtx *ctx = g_new(DbusReplyCtx, 1);
    ctx->webview     = reply_ctx->webview;
    ctx->callback_id = reply_ctx->callback_id;
    /* Transfer conn ownership to the callback via g_object_ref */
    g_object_ref(conn);

    g_dbus_connection_call(conn,
        "org.zylos.PowerManager", "/org/zylos/PowerManager",
        "org.zylos.PowerManager", "GetBatteryLevel",
        NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL,
        on_dbus_call_done, ctx);

    g_object_unref(conn);
}

/* -- WiFi scan: async D-Bus query to WifiService ----------- */
static void handle_wifi_scan(const char *type, gpointer msg_obj,
                             ZylAppManifest *manifest,
                             ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)msg_obj; (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    g_info("Bridge: wifi.scan from %s", app_id);

    if (reply_ctx->callback_id < 0) return;

    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (err) {
        zyl_bridge_reply_error(reply_ctx, "dbus unavailable");
        g_error_free(err);
        return;
    }

    DbusReplyCtx *ctx = g_new(DbusReplyCtx, 1);
    ctx->webview     = reply_ctx->webview;
    ctx->callback_id = reply_ctx->callback_id;
    g_object_ref(conn);

    g_dbus_connection_call(conn,
        "org.zylos.WifiService", "/org/zylos/WifiService",
        "org.zylos.WifiService", "Scan",
        NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL,
        on_dbus_call_done, ctx);

    g_object_unref(conn);
}

/* -- Settings get: async D-Bus query ----------------------- */
static void handle_settings_get(const char *type, gpointer msg_obj,
                                ZylAppManifest *manifest,
                                ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *key = NULL;
    if (json_object_has_member(msg, "key"))
        key = json_object_get_string_member(msg, "key");

    g_info("Bridge: settings.get from %s key='%s'",
           app_id, key ? key : "(all)");

    if (reply_ctx->callback_id < 0) return;

    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (err) {
        zyl_bridge_reply_error(reply_ctx, "dbus unavailable");
        g_error_free(err);
        return;
    }

    GVariant *params = key
        ? g_variant_new("(s)", key)
        : g_variant_new("(s)", "");

    DbusReplyCtx *ctx = g_new(DbusReplyCtx, 1);
    ctx->webview     = reply_ctx->webview;
    ctx->callback_id = reply_ctx->callback_id;
    g_object_ref(conn);

    g_dbus_connection_call(conn,
        "org.zylos.Settings", "/org/zylos/Settings",
        "org.zylos.Settings", "Get",
        params, NULL,
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL,
        on_dbus_call_done, ctx);

    g_object_unref(conn);
}

/* -- Settings update --------------------------------------- */
static void handle_settings_update(const char *type, gpointer msg_obj,
                                   ZylAppManifest *manifest,
                                   ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    const char *key = NULL;
    if (json_object_has_member(msg, "key"))
        key = json_object_get_string_member(msg, "key");

    g_info("Bridge: settings.update from %s key='%s'",
           app_id, key ? key : "(none)");

    if (reply_ctx->callback_id < 0) return;

    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (err) {
        zyl_bridge_reply_error(reply_ctx, "dbus unavailable");
        g_error_free(err);
        return;
    }

    /* Build params: (key, value_json) */
    const char *value_str = "";
    gchar *value_json = NULL;
    if (json_object_has_member(msg, "value")) {
        JsonNode *val_node = json_object_get_member(msg, "value");
        JsonGenerator *gen = json_generator_new();
        json_generator_set_root(gen, val_node);
        value_json = json_generator_to_data(gen, NULL);
        value_str = value_json;
        g_object_unref(gen);
    }

    GVariant *params = g_variant_new("(ss)", key ? key : "", value_str);
    g_free(value_json);

    DbusReplyCtx *ctx = g_new(DbusReplyCtx, 1);
    ctx->webview     = reply_ctx->webview;
    ctx->callback_id = reply_ctx->callback_id;
    g_object_ref(conn);

    g_dbus_connection_call(conn,
        "org.zylos.Settings", "/org/zylos/Settings",
        "org.zylos.Settings", "Set",
        params, NULL,
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL,
        on_dbus_call_done, ctx);

    g_object_unref(conn);
}

/* == service.request → D-Bus dispatch ====================== */

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
    {"telephony",    "org.zylos.Telephony",          "/org/zylos/Telephony",          "org.zylos.Telephony"},
    {"usb",          "org.zylos.UsbManager",         "/org/zylos/UsbManager",         "org.zylos.UsbManager"},
    {"user",         "org.zylos.UserManager",        "/org/zylos/UserManager",        "org.zylos.UserManager"},
    {"credential",   "org.zylos.CredentialManager",  "/org/zylos/CredentialManager",  "org.zylos.CredentialManager"},
    {"accessibility","org.zylos.Accessibility",      "/org/zylos/Accessibility",      "org.zylos.Accessibility"},
    {"logger",       "org.zylos.Logger",             "/org/zylos/Logger",             "org.zylos.Logger"},
    {"camera",       "org.zylos.CameraService",      "/org/zylos/CameraService",      "org.zylos.CameraService"},
    {"audio",        "org.zylos.AudioService",       "/org/zylos/AudioService",       "org.zylos.AudioService"},
    {"bluetooth",    "org.zylos.BluetoothService",   "/org/zylos/BluetoothService",   "org.zylos.BluetoothService"},
    {"wifi",         "org.zylos.WifiService",        "/org/zylos/WifiService",        "org.zylos.WifiService"},
    {"settings",     "org.zylos.Settings",           "/org/zylos/Settings",           "org.zylos.Settings"},
    {NULL, NULL, NULL, NULL}
};

static const ServiceRoute *find_route(const char *service) {
    for (int i = 0; SERVICE_ROUTES[i].service; i++) {
        if (strcmp(SERVICE_ROUTES[i].service, service) == 0)
            return &SERVICE_ROUTES[i];
    }
    return NULL;
}

static char *capitalize_method(const char *method) {
    if (!method || !method[0]) return g_strdup("");
    char *cap = g_strdup(method);
    if (cap[0] >= 'a' && cap[0] <= 'z') cap[0] = (char)(cap[0] - 32);
    return cap;
}

static void handle_service_request(const char *type, gpointer msg_obj,
                                    ZylAppManifest *manifest,
                                    ZylBridgeReplyCtx *reply_ctx, gpointer data)
{
    (void)type; (void)data;
    const char *app_id = (manifest && manifest->id) ? manifest->id : "unknown";
    JsonObject *msg = msg_obj;

    if (!json_object_has_member(msg, "service") ||
        !json_object_has_member(msg, "method")) {
        g_warning("Bridge: service.request missing service/method from %s", app_id);
        zyl_bridge_reply_error(reply_ctx, "missing service or method");
        return;
    }

    const char *service = json_object_get_string_member(msg, "service");
    const char *method  = json_object_get_string_member(msg, "method");

    if (!service || !method || service[0] == '\0' || method[0] == '\0') {
        zyl_bridge_reply_error(reply_ctx, "empty service or method");
        return;
    }

    const ServiceRoute *route = find_route(service);
    if (!route) {
        g_warning("Bridge: no D-Bus route for service '%s' from %s",
                  service, app_id);
        zyl_bridge_reply_error(reply_ctx, "unknown service");
        return;
    }

    char *dbus_method = capitalize_method(method);

    g_info("Bridge: service.request from %s → %s.%s → D-Bus %s.%s",
           app_id, service, method, route->dbus_name, dbus_method);

    GError *err = NULL;
    GDBusConnection *session = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (err) {
        g_warning("Bridge: session bus error: %s", err->message);
        zyl_bridge_reply_error(reply_ctx, "dbus unavailable");
        g_error_free(err);
        g_free(dbus_method);
        return;
    }

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

    if (reply_ctx->callback_id >= 0) {
        /* Async with response routing */
        DbusReplyCtx *ctx = g_new(DbusReplyCtx, 1);
        ctx->webview     = reply_ctx->webview;
        ctx->callback_id = reply_ctx->callback_id;
        g_object_ref(session);

        g_dbus_connection_call(session,
            route->dbus_name, route->dbus_path,
            route->dbus_iface, dbus_method, call_params,
            NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
            on_dbus_call_done, ctx);
    } else {
        /* Fire-and-forget */
        g_dbus_connection_call(session,
            route->dbus_name, route->dbus_path,
            route->dbus_iface, dbus_method, call_params,
            NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
    }

    g_object_unref(session);
    g_free(dbus_method);
}
