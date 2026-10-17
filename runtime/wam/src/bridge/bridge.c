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
#include <sys/random.h>
#include <json-glib/json-glib.h>
#include "zyl_json.h"

/* == Handler Registry Entry ================================= */

typedef struct {
    ZylBridgeHandler handler;
    gpointer         user_data;
} BridgeHandlerEntry;

/* == Global Handler Registry ================================ */

static GHashTable *handler_registry = NULL;

/* service.request: introspection per route (service name), fetched on first use.
 * Not per D-Bus name: one name can answer on several objects (the WAM's apps and
 * webcontent), and each object's introspection holds only its own interface.
 * shortcut: a service upgraded in place keeps its old signature here until WAM
 * restarts — fine while services and WAM ship in the same OS image. */
static GHashTable *introspect_cache = NULL;

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
    zyl_bridge_register_handler("service.request",     handle_service_request, NULL);
}

void zyl_bridge_cleanup(void)
{
    if (handler_registry) {
        g_hash_table_destroy(handler_registry);
        handler_registry = NULL;
    }
    if (introspect_cache) {
        g_hash_table_destroy(introspect_cache);
        introspect_cache = NULL;
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

/* == The view's token ======================================= */

gboolean zyl_bridge_new_token(char out[ZYL_BRIDGE_TOKEN_LEN + 1])
{
    unsigned char raw[ZYL_BRIDGE_TOKEN_LEN / 2];
    out[0] = '\0';
    if (getrandom(raw, sizeof(raw), 0) != (ssize_t)sizeof(raw)) return FALSE;
    for (size_t i = 0; i < sizeof(raw); i++)
        g_snprintf(out + 2 * i, 3, "%02x", raw[i]);
    return TRUE;
}

const char *zyl_bridge_untoken(const char *msg, const char *token)
{
    if (!msg || !token || strlen(token) != ZYL_BRIDGE_TOKEN_LEN) return NULL;
    if (strlen(msg) < ZYL_BRIDGE_TOKEN_LEN) return NULL;
    unsigned char diff = 0;
    for (size_t i = 0; i < ZYL_BRIDGE_TOKEN_LEN; i++)
        diff |= (unsigned char)(msg[i] ^ token[i]);
    return diff ? NULL : msg + ZYL_BRIDGE_TOKEN_LEN;
}

/* == Load and inject the JS bridge ========================== */

/* A JS string literal of s: the manifest's strings are the app's to write,
   and the bridge script runs before — and outside — the app's CSP */
static gchar *js_literal(const char *s)
{
    GString *out = g_string_new("\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        if (*p == '"' || *p == '\\') { g_string_append_c(out, '\\'); g_string_append_c(out, (char)*p); }
        else if (*p < 0x20 || *p == '<' || *p == '>') g_string_append_printf(out, "\\u%04x", *p);
        else if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0xA8 || p[2] == 0xA9)) {   /* U+2028/9 end ES5 lines */
            g_string_append_printf(out, "\\u%04x", p[2] == 0xA8 ? 0x2028 : 0x2029);
            p += 2;
        } else g_string_append_c(out, (char)*p);
    }
    g_string_append_c(out, '"');
    return g_string_free(out, FALSE);
}

gboolean zyl_bridge_inject(const char      *bridge_js_path,
                           WebKitWebView   *webview,
                           ZylAppManifest  *manifest,
                           const char      *token)
{
    gchar *template = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!bridge_js_path || !webview || !manifest || !token) {
        g_warning("Bridge: inject called with NULL argument");
        return FALSE;
    }

    if (!g_file_get_contents(bridge_js_path, &template, &length, &error)) {
        g_warning("Failed to load bridge script %s: %s",
                  bridge_js_path, error->message);
        g_error_free(error);
        return FALSE;
    }

    /* One pass over the template: a value is never read again for tokens */
    const char *names[] = { "{{APP_ID}}", "{{APP_NAME}}", "{{APP_VERSION}}", "{{BRIDGE_TOKEN}}", NULL };
    const char *values[] = { manifest->id, manifest->name, manifest->version, token };
    GString *script = g_string_new(NULL);
    for (const char *p = template; *p; ) {
        int i = 0;
        while (names[i] && !g_str_has_prefix(p, names[i])) i++;
        if (!names[i]) { g_string_append_c(script, *p++); continue; }
        gchar *lit = js_literal(values[i]);
        g_string_append(script, lit);
        g_free(lit);
        p += strlen(names[i]);
    }
    gchar *s1 = g_string_free(script, FALSE);

    WebKitUserContentManager *ucm =
        webkit_web_view_get_user_content_manager(webview);
    WebKitUserScript *user_script = webkit_user_script_new(
        s1,
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,     /* the token stays out of frames */
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, user_script);
    webkit_user_script_unref(user_script);

    g_free(s1);
    g_free(template);
    return TRUE;
}

/* == JSON Pre-validation ==================================== */

/* A bridge message is exactly one JSON object: "{...}" with nothing but
   whitespace around it. Anything else is rejected here rather than handed to
   json-glib — the parser builds the leading value, then fails on the rest
   without attaching it to itself, so g_object_unref(parser) cannot free it.
   Two such leaks were found by tests/fuzz/fuzz_json_dispatch:
     "{}{}"                trailing object after a complete one
     "0{\"type\":\"x\"}"       leading scalar before the object
   Checking the shape instead of only brace balance closes the whole class. */
static gboolean json_prevalidate(const char *str)
{
    if (!str) return FALSE;

    const char *p = str;
    while (g_ascii_isspace((guchar)*p)) p++;
    if (*p != '{') return FALSE;          /* must start with an object */

    int brace_depth   = 0;
    int bracket_depth = 0;
    gboolean in_string = FALSE;
    gboolean escaped   = FALSE;

    for (; *p; p++) {
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

        if (brace_depth == 0) {
            /* Top-level object closed. Brackets must balance with it, and
               only whitespace may follow. */
            if (bracket_depth != 0) return FALSE;
            for (const char *q = p + 1; *q; q++)
                if (!g_ascii_isspace((guchar)*q)) return FALSE;
            return TRUE;
        }
    }

    return FALSE;   /* input ended with the object still open */
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

    const char *type = zyl_json_str(msg, "type", NULL);
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

/* An answer can come after its app closed: the view it goes to is held
   until then (the test mock's view is no GObject and overrides these) */
#ifndef ZYL_BRIDGE_VIEW_REF
#define ZYL_BRIDGE_VIEW_REF(v)   ((v) ? g_object_ref(v) : NULL)
#define ZYL_BRIDGE_VIEW_UNREF(v) do { if (v) g_object_unref(v); } while (0)
#endif

typedef struct {
    WebKitWebView *webview;     /* held: ZYL_BRIDGE_VIEW_REF */
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
    ZYL_BRIDGE_VIEW_UNREF(ctx->webview);
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
        target = zyl_json_str(msg, "appId", NULL);

    g_info("Bridge: app.launch from %s -> target=%s",
           app_id, target ? target : "(none)");
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
    {"telemetry",    "org.zylos.Telemetry",          "/org/zylos/Telemetry",          "org.zylos.Telemetry"},
    {"netstats",     "org.zylos.NetStats",           "/org/zylos/NetStats",           "org.zylos.NetStats"},
    {"bootctl",      "org.zylos.BootControl",        "/org/zylos/BootControl",        "org.zylos.BootControl"},
    {"lock",         "org.zylos.Lock",               "/org/zylos/Lock",               "org.zylos.Lock"},
    {"updater",      "org.zylos.Updater",            "/org/zylos/Updater",            "org.zylos.Updater"},
    /* The installed apps are the WAM's own: it answers on its bus name */
    {"apps",         "org.zylos.WebAppManager",      "/org/zylos/WebAppManager",      "org.zylos.WebAppManager"},
    {"device",       "org.zylos.Device",             "/org/zylos/Device",             "org.zylos.Device"},
    {"storage",      "org.zylos.Storage",            "/org/zylos/Storage",            "org.zylos.Storage"},
    {"fs",           "org.zylos.FileSystem",         "/org/zylos/FileSystem",         "org.zylos.FileSystem"},
    {"contacts",     "org.zylos.Contacts",           "/org/zylos/Contacts",           "org.zylos.Contacts"},
    {"messaging",    "org.zylos.Messaging",          "/org/zylos/Messaging",          "org.zylos.Messaging"},
    {"clipboard",    "org.zylos.Clipboard",          "/org/zylos/Clipboard",          "org.zylos.Clipboard"},
    {"network",      "org.zylos.Network",            "/org/zylos/Network",            "org.zylos.Network"},
    {"appdata",      "org.zylos.AppData",            "/org/zylos/AppData",            "org.zylos.AppData"},
    {"push",         "org.zylos.Push",               "/org/zylos/Push",               "org.zylos.Push"},
    {"appstore",     "org.zylos.AppStore",           "/org/zylos/AppStore",           "org.zylos.AppStore"},
    {"alarm",        "org.zylos.AlarmService",       "/org/zylos/AlarmService",       "org.zylos.AlarmService"},
    /* OS-owned web content view the browser drives — WAM answers (like apps) */
    {"webcontent",   "org.zylos.WebAppManager",      "/org/zylos/WebContent",         "org.zylos.WebContent"},
    {NULL, NULL, NULL, NULL}
};


/* == Service permission policy ===============================
 * Mirror of SERVICE_PERMISSIONS and RESERVED_PERMISSIONS in
 * apps/system/permissions.js, which is the authority (CLAUDE.md rule 8).
 * tests/verify-all.sh fails when the two drift apart, and when a routed
 * service has no entry here (app_may_call refuses an unlisted service to
 * every third-party app, so the route would work for system apps only).
 *
 * Before this table the device bridge routed service.request straight to
 * D-Bus with no check at all: the emulator enforced permissions in
 * ZylSystemServices, the device did not, so any installed app could call
 * credential, telephony or settings.
 *
 * An app needs at least ONE of the listed permissions; an empty list means
 * none is required. */
#define ZYL_POLICY_MAX_PERMS 6

typedef struct {
    const char *service;
    const char *perms[ZYL_POLICY_MAX_PERMS];
} ServicePermission;

static const ServicePermission SERVICE_PERMISSIONS[] = {
    {"fs",            {"storage", "files.read", "files.write", "files.delete", NULL}},
    {"credential",    {"credential", NULL}},
    {"settings",      {"system", NULL}},
    {"wifi",          {"wifi", NULL}},
    {"bluetooth",     {"bluetooth", NULL}},
    {"notification",  {"notification", NULL}},
    /* An alarm is a notification the user gets later: same permission */
    {"alarm",         {"notification", NULL}},
    {"camera",        {"camera", NULL}},
    {"audio",         {"audio", NULL}},
    {"appstore",      {"app.manage", NULL}},
    {"contacts",      {"contacts", NULL}},
    {"messaging",     {"messaging", NULL}},
    {"network",       {"network", NULL}},
    {"telephony",     {"telephony", "calls", "sms", NULL}},
    {"location",      {"location", NULL}},
    {"telemetry",     {"system", NULL}},
    {"netstats",      {"system", NULL}},
    {"bootctl",       {"system", NULL}},
    {"lock",          {"system", NULL}},
    /* Installs and restarts into a new OS: Settings only */
    {"updater",       {"system", NULL}},
    /* Adds, switches and removes users — removing one deletes their data */
    {"user",          {"system", NULL}},
    {"device",        {NULL}},
    {"storage",       {NULL}},
    {"apps",          {NULL}},
    {"display",       {NULL}},
    {"input",         {NULL}},
    {"sensors",       {NULL}},
    {"usb",           {NULL}},
    {"sandbox",       {NULL}},
    {"logger",        {NULL}},
    {"accessibility", {NULL}},
    {"clipboard",     {NULL}},
    /* Every app has its own, named by the bridge's caller — never another's */
    {"appdata",       {NULL}},
    {"push",          {"push", NULL}},
    {"power",         {NULL}},
    /* Embedding a web content view is the OS's browser only (reserved) */
    {"webcontent",    {"webcontent", NULL}},
    {NULL, {NULL}}
};

/* Methods that reach past the caller's own, checked after the service rule:
 * every app's notifications and the device-wide quiet settings (Cancel takes
 * app_id, so an app cancels only its own without these). Mirror of
 * METHOD_PERMISSIONS in apps/system/permissions.js (verify-all compares). */
static const ServicePermission METHOD_PERMISSIONS[] = {
    {"notification.getActive",         {"notification.read", NULL}},
    {"notification.getHistory",        {"notification.read", NULL}},
    {"notification.getAllBadgeCounts", {"notification.read", NULL}},
    {"notification.getGroupSummary",   {"notification.read", NULL}},
    {"notification.clearAll",          {"notification.read", NULL}},
    {"notification.clearHistory",      {"notification.read", NULL}},
    {"notification.invokeAction",      {"notification.read", NULL}},
    {"notification.setDndMode",        {"system", NULL}},
    {"notification.setChannelEnabled", {"system", NULL}},
    /* The chip's ID outlives a factory reset — a tracking ID if every app read it */
    {"device.getDeviceId",             {"system", NULL}},
    /* The emergency alert tone: full volume through mute and silent mode */
    {"audio.playTone",                 {"system", NULL}},
    /* The device's power is the OS's: any app may read it, only system apps
       turn the screen, sleep, shut down or reboot it, or hold it awake */
    {"power.screenOff",                {"system", NULL}},
    {"power.screenOn",                 {"system", NULL}},
    {"power.userActivity",             {"system", NULL}},
    {"power.suspend",                  {"system", NULL}},
    {"power.shutdown",                 {"system", NULL}},
    {"power.reboot",                   {"system", NULL}},
    /* The recovery screen's factory reset — and only on a recovery boot (the
       service refuses otherwise) */
    {"storage.eraseUserData",          {"system", NULL}},
    {"power.setBrightness",            {"system", NULL}},
    {"power.acquireWakelock",          {"system", NULL}},
    {"power.releaseWakelock",          {"system", NULL}},
    /* The device's settings: any app may read them, only system apps change
       them (USB mode, display, accessibility, input). The log is the OS's — it
       names every app */
    {"usb.setMode",                    {"system", NULL}},
    {"display.setMode",                {"system", NULL}},
    {"display.setScale",               {"system", NULL}},
    {"display.setRotation",            {"system", NULL}},
    {"display.setAutoRotate",          {"system", NULL}},
    {"accessibility.setHighContrast",  {"system", NULL}},
    {"accessibility.setFontScale",     {"system", NULL}},
    {"accessibility.setScreenReader",  {"system", NULL}},
    {"input.showKeyboard",             {"system", NULL}},
    {"input.hideKeyboard",             {"system", NULL}},
    {"input.switchLayout",             {"system", NULL}},
    {"logger.getLogs",                 {"system", NULL}},
    {"logger.getCrashReports",         {"system", NULL}},
    {"logger.setFilterLevel",          {"system", NULL}},
    /* Which apps are on the device is the user's to give (app.list); bringing
       one forward is app.launch — another app's data goes in an intent. What is
       in front, panels, recents and app switching are the OS's own */
    {"apps.getInstalled",              {"app.list", NULL}},
    {"apps.listApps",                  {"app.list", NULL}},
    {"apps.listRunning",               {"app.list", NULL}},
    {"apps.launch",                    {"app.launch", NULL}},
    {"apps.suspend",                   {"system", NULL}},
    {"apps.resume",                    {"system", NULL}},
    {"apps.goHome",                    {"system", NULL}},
    {"apps.goBack",                    {"system", NULL}},
    {"apps.openPanel",                 {"system", NULL}},
    {"apps.showRecents",               {"system", NULL}},
    {"apps.switchApp",                 {"system", NULL}},
    /* Calls (dialling, answering, the call log) and SMS are permissions of their
       own, each asked for; the emergency alerts, mobile data and the modem's
       hardware ID (IMEI — a tracking ID, as the chip's) are the OS's */
    {"telephony.dial",                 {"calls", NULL}},
    {"telephony.answer",               {"calls", NULL}},
    {"telephony.hangup",               {"calls", NULL}},
    {"telephony.getCallState",         {"calls", NULL}},
    {"telephony.sendDTMF",             {"calls", NULL}},
    {"telephony.getCallLog",           {"calls", NULL}},
    {"telephony.deleteCallLog",        {"calls", NULL}},
    {"telephony.sendSms",              {"sms", NULL}},
    {"telephony.getImei",              {"system", NULL}},
    {"telephony.setMobileData",        {"system", NULL}},
    {"telephony.getEmergencyAlerts",   {"system", NULL}},
    {"telephony.acknowledgeEmergencyAlert", {"system", NULL}},
    {"telephony.getEmergencyAlertSettings", {"system", NULL}},
    {"telephony.setEmergencyAlertEnabled",  {"system", NULL}},
    {NULL, {NULL}}
};

/* Events that need more than their service: who is calling, what an SMS says,
 * an emergency alert. Mirror of EVENT_PERMISSIONS in apps/system/permissions.js
 * (verify-all compares). */
static const struct { const char *service, *signal; const char *perms[4]; } EVENT_PERMISSIONS[] = {
    {"telephony", "CallStateChanged", {"calls", NULL}},
    {"telephony", "SmsReceived",      {"sms", NULL}},
    {"telephony", "EmergencyAlert",   {"system", NULL}},
    {NULL, NULL, {NULL}}
};

/* What the user is asked for the first time an app uses it — the camera and
 * microphone, where the user is, contacts and messages, calls and SMS. The
 * question is the system UI's to draw (the WAM's consent, never the app's own
 * page: there the app could answer it itself); the answer stays in settings
 * (app_grants / app_permissions). Mirror of DANGEROUS_PERMISSIONS in
 * apps/system/permissions.js (verify-all compares). */
static const char *const DANGEROUS_PERMISSIONS[] = {
    "camera", "microphone", "location", "contacts", "messaging", "calls", "sms", NULL
};

/* == The user's answers =====================================
 * Two settings categories, each one comma-separated list of permissions per app
 * id: "app_permissions" — what the user switched off (Settings, or no to a
 * first-use question) — and "app_grants" — the dangerous permissions the user
 * said yes to. The WAM is the only gate, so it keeps both tables and refreshes
 * one when the settings service says its category changed. A system app is
 * never revocable nor asked — the same rules as apps/system/permissions.js. */
typedef struct {
    const char *category;           /* the settings category */
    GHashTable *apps;               /* app id → GHashTable<perm, TRUE> */
} PermTable;

static PermTable revoked_table = { "app_permissions", NULL };
static PermTable granted_table = { "app_grants", NULL };

static void table_set(PermTable *t, const char *app_id, const char *csv)
{
    if (!app_id || !app_id[0]) return;
    if (!t->apps)
        t->apps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify)g_hash_table_destroy);
    GHashTable *set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar **parts = g_strsplit(csv ? csv : "", ",", -1);
    for (int i = 0; parts[i]; i++) {
        gchar *perm = g_strstrip(parts[i]);
        if (perm[0]) g_hash_table_add(set, g_strdup(perm));
    }
    g_strfreev(parts);
    g_hash_table_replace(t->apps, g_strdup(app_id), set);
}

static gboolean table_has(const PermTable *t, const char *app_id, const char *perm)
{
    if (!t->apps || !app_id) return FALSE;
    GHashTable *set = g_hash_table_lookup(t->apps, app_id);
    return set && g_hash_table_contains(set, perm);
}

/* The app's list with perm added, as settings keeps it */
static char *table_csv_with(const PermTable *t, const char *app_id, const char *perm)
{
    GString *csv = g_string_new(perm);
    GHashTable *set = t->apps ? g_hash_table_lookup(t->apps, app_id) : NULL;
    GHashTableIter it;
    gpointer p;
    if (set) {
        g_hash_table_iter_init(&it, set);
        while (g_hash_table_iter_next(&it, &p, NULL))
            if (strcmp(p, perm) != 0) g_string_append_printf(csv, ",%s", (const char *)p);
    }
    return g_string_free(csv, FALSE);
}

static gboolean is_revoked(const char *app_id, const char *perm) { return table_has(&revoked_table, app_id, perm); }
static gboolean is_granted(const char *app_id, const char *perm) { return table_has(&granted_table, app_id, perm); }

static gboolean same_set(GHashTable *a, GHashTable *b)
{
    guint n = a ? g_hash_table_size(a) : 0;
    if (n != (b ? g_hash_table_size(b) : 0)) return FALSE;
    GHashTableIter it;
    gpointer perm;
    if (a) {
        g_hash_table_iter_init(&it, a);
        while (g_hash_table_iter_next(&it, &perm, NULL))
            if (!g_hash_table_contains(b, perm)) return FALSE;
    }
    return TRUE;
}

static void add_changed(GHashTable *from, GHashTable *to, GPtrArray *changed)
{
    if (!from) return;
    GHashTableIter it;
    gpointer id, set;
    g_hash_table_iter_init(&it, from);
    while (g_hash_table_iter_next(&it, &id, &set)) {
        GHashTable *other = to ? g_hash_table_lookup(to, id) : NULL;
        if (!same_set(set, other) &&
            !g_ptr_array_find_with_equal_func(changed, id, g_str_equal, NULL))
            g_ptr_array_add(changed, g_strdup(id));
    }
}

/* The settings' a{sv} becomes the table; the ids whose list changed */
static GPtrArray *table_load(PermTable *t, GVariant *dict)
{
    GHashTable *old = t->apps;
    t->apps = NULL;
    GVariantIter iter;
    const gchar *app_id = NULL;
    GVariant *value = NULL;
    g_variant_iter_init(&iter, dict);
    while (g_variant_iter_next(&iter, "{&sv}", &app_id, &value)) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            table_set(t, app_id, g_variant_get_string(value, NULL));
        g_variant_unref(value);
    }
    GPtrArray *changed = g_ptr_array_new_with_free_func(g_free);
    add_changed(old, t->apps, changed);
    add_changed(t->apps, old, changed);
    if (old) g_hash_table_destroy(old);
    return changed;
}

static ZylBridgeAppFn revocations_changed;
static gpointer revocations_changed_data;
static GDBusConnection *settings_conn;      /* where the answers are kept */

static void on_table_read(GObject *src, GAsyncResult *res, gpointer data)
{
    PermTable *t = data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &error);
    if (!reply) {
        g_message("Bridge: no %s from the settings service (%s)", t->category,
                  error ? error->message : "no reply");
        g_clear_error(&error);
        return;
    }
    GVariant *dict = g_variant_get_child_value(reply, 0);
    GPtrArray *changed = table_load(t, dict);
    /* What a revoked permission gave the app's documents (CSP, CORS) is stale */
    for (guint i = 0; t == &revoked_table && revocations_changed && i < changed->len; i++)
        revocations_changed(g_ptr_array_index(changed, i), revocations_changed_data);
    g_ptr_array_unref(changed);
    g_variant_unref(dict);
    g_variant_unref(reply);
}

static void table_read(GDBusConnection *conn, PermTable *t)
{
    g_dbus_connection_call(conn, "org.zylos.Settings", "/org/zylos/Settings",
        "org.zylos.Settings", "Get", g_variant_new("(s)", t->category),
        G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
        on_table_read, t);
}

static void on_settings_changed(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *signal, GVariant *params,
                                gpointer data)
{
    (void)sender; (void)path; (void)iface; (void)signal; (void)data;
    const gchar *category = NULL;
    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) return;
    g_variant_get(params, "(&s)", &category);
    if (g_strcmp0(category, revoked_table.category) == 0) table_read(conn, &revoked_table);
    if (g_strcmp0(category, granted_table.category) == 0) table_read(conn, &granted_table);
}

void zyl_bridge_watch_revocations(GDBusConnection *conn, ZylBridgeAppFn changed, gpointer data)
{
    if (!conn) return;
    revocations_changed = changed;
    revocations_changed_data = data;
    settings_conn = conn;
    g_dbus_connection_signal_subscribe(conn, "org.zylos.Settings",
        "org.zylos.Settings", "CategoryChanged", "/org/zylos/Settings", NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_settings_changed, NULL, NULL);
    table_read(conn, &revoked_table);
    table_read(conn, &granted_table);
}

/* The user answered a first-use question: kept here at once (this request and
   the next see it — and the settings reload finds nothing changed, so a no does
   not close the app) and in settings for the next boot */
void zyl_bridge_consent_answered(const char *app_id, const char *perm, gboolean allowed)
{
    if (!app_id || !perm) return;
    PermTable *t = allowed ? &granted_table : &revoked_table;
    char *csv = table_csv_with(t, app_id, perm);
    table_set(t, app_id, csv);
    if (settings_conn)
        g_dbus_connection_call(settings_conn, "org.zylos.Settings", "/org/zylos/Settings",
            "org.zylos.Settings", "Update",
            g_variant_new("(ssv)", t->category, app_id, g_variant_new_string(csv)),
            NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
    g_free(csv);
}

/* Never granted by declaring them in app.json — only system apps hold these */
static const char *const RESERVED_PERMISSIONS[] = {
    "system", "credential", "app.manage", "notification.read", "webcontent", NULL
};

static gboolean is_reserved_permission(const char *perm) {
    for (int i = 0; RESERVED_PERMISSIONS[i]; i++)
        if (strcmp(RESERVED_PERMISSIONS[i], perm) == 0) return TRUE;
    return FALSE;
}

static gboolean manifest_holds(const ZylAppManifest *m, const char *perm) {
    if (!m->is_system && is_reserved_permission(perm)) return FALSE;
    if (!m->is_system && is_revoked(m->id, perm)) return FALSE;
    for (int i = 0; i < m->n_permissions && m->permissions && m->permissions[i]; i++)
        if (strcmp(m->permissions[i], perm) == 0) return TRUE;
    return FALSE;
}

gboolean zyl_bridge_app_holds(const ZylAppManifest *m, const char *perm)
{
    return m && perm && (m->is_system || manifest_holds(m, perm));
}

static gboolean is_dangerous(const char *perm) {
    for (int i = 0; DANGEROUS_PERMISSIONS[i]; i++)
        if (strcmp(DANGEROUS_PERMISSIONS[i], perm) == 0) return TRUE;
    return FALSE;
}

/* Of the permissions a rule accepts, is one the app holds usable now: one that
   needs no asking, or a dangerous one the user allowed. *ask: the first held
   dangerous one still to ask for. An empty rule needs nothing */
static gboolean usable(const ZylAppManifest *m, const char *const *perms, const char **ask) {
    if (ask) *ask = NULL;
    if (m->is_system || !perms[0]) return TRUE;
    for (int i = 0; perms[i]; i++) {
        if (!manifest_holds(m, perms[i])) continue;
        if (!is_dangerous(perms[i]) || is_granted(m->id, perms[i])) return TRUE;
        if (ask && !*ask) *ask = perms[i];
    }
    return FALSE;
}

const char *zyl_bridge_consent_for(const ZylAppManifest *m, const char *perm)
{
    if (!m || !perm || m->is_system || !is_dangerous(perm) ||
        !manifest_holds(m, perm) || is_granted(m->id, perm)) return NULL;
    return perm;
}

/**
 * May this app call this service at all?
 * System apps (installed on the verified read-only system partition) may call
 * everything. Unknown apps may call nothing. A service missing from the table
 * is denied: failing closed is the safe answer for an unreviewed service.
 */
static gboolean app_may_call(const ZylAppManifest *m, const char *service) {
    if (!m || !m->id) return FALSE;
    if (m->is_system) return TRUE;

    for (int i = 0; SERVICE_PERMISSIONS[i].service; i++) {
        if (strcmp(SERVICE_PERMISSIONS[i].service, service) != 0) continue;
        const char *const *req = SERVICE_PERMISSIONS[i].perms;
        if (!req[0]) return TRUE;                   /* no permission required */
        for (int j = 0; req[j]; j++)
            if (manifest_holds(m, req[j])) return TRUE;
        return FALSE;
    }
    return FALSE;
}

/* The method rule, after app_may_call: system apps hold every permission.
   The rule is keyed by the method that is called: "reboot" and "Reboot" are
   both D-Bus Reboot (capitalize_method), so both meet the rule for "reboot" */
static gboolean app_may_call_method(const ZylAppManifest *m, const char *service,
                                    const char *method) {
    if (m->is_system) return TRUE;
    char *key = g_strconcat(service, ".", method, NULL);
    char *first = key + strlen(service) + 1;
    *first = g_ascii_tolower(*first);
    gboolean ok = TRUE;
    for (int i = 0; METHOD_PERMISSIONS[i].service; i++) {
        if (strcmp(METHOD_PERMISSIONS[i].service, key) != 0) continue;
        ok = FALSE;
        for (int j = 0; METHOD_PERMISSIONS[i].perms[j] && !ok; j++)
            ok = manifest_holds(m, METHOD_PERMISSIONS[i].perms[j]);
    }
    g_free(key);
    return ok;
}

/* The rule a call goes by: its method's when it has one, else its service's */
static const char *const *call_rule(const char *service, const char *method) {
    static const char *const none[] = { NULL };
    char *key = g_strconcat(service, ".", method, NULL);
    char *first = key + strlen(service) + 1;
    *first = g_ascii_tolower(*first);
    const char *const *rule = NULL;
    for (int i = 0; METHOD_PERMISSIONS[i].service && !rule; i++)
        if (strcmp(METHOD_PERMISSIONS[i].service, key) == 0) rule = METHOD_PERMISSIONS[i].perms;
    g_free(key);
    for (int i = 0; SERVICE_PERMISSIONS[i].service && !rule; i++)
        if (strcmp(SERVICE_PERMISSIONS[i].service, service) == 0) rule = SERVICE_PERMISSIONS[i].perms;
    return rule ? rule : none;
}

/* The dangerous permission this call needs the user's yes for, or NULL —
   asked after the call passed app_may_call and app_may_call_method */
static const char *consent_needed(const ZylAppManifest *m, const char *service, const char *method) {
    const char *ask = NULL;
    return usable(m, call_rule(service, method), &ask) ? NULL : ask;
}

gboolean zyl_bridge_app_may_call(const ZylAppManifest *m, const char *service, const char *method)
{
    return service && method && app_may_call(m, service) && app_may_call_method(m, service, method);
}

/* Services only the app in front may use (the emulator has one app alive at a
   time, so its frame is always the front one): the clipboard holds what the
   user copied — a background app neither reads nor swaps it; the camera, where
   the user is and the motion sensors are taken only by what the user sees —
   an app sent to the background or behind the lock screen stops capturing
   (its WebKit camera and microphone are muted by the WAM, wam.c hold_capture) */
static const char *const FRONT_ONLY_SERVICES[] = { "clipboard", "camera", "location", "sensors", NULL };
static gboolean (*front_query)(const char *app_id);

void zyl_bridge_set_front_query(gboolean (*is_front)(const char *app_id))
{
    front_query = is_front;
}

static gboolean app_may_use_now(const ZylAppManifest *m, const char *service)
{
    if (m->is_system) return TRUE;
    for (int i = 0; FRONT_ONLY_SERVICES[i]; i++)
        if (strcmp(FRONT_ONLY_SERVICES[i], service) == 0)
            return front_query && m->id && front_query(m->id);
    return TRUE;
}

/* Events about one app's own things reach a third-party app only when they
   are its own — another app's notification (a message, a code) or geofence
   is not its to see. They carry the owner as their second argument; one that
   names no owner goes to none of them. signal NULL: every signal of the service. */
static const struct { const char *service, *signal, *read_all; } OWNED_EVENTS[] = {
    {"notification", NULL,            "notification.read"},
    {"location",     "GeofenceEnter", NULL},
    {"location",     "GeofenceExit",  NULL},
    {"alarm",        "AlarmTriggered", NULL},
    {"push",         "PushReceived",   NULL},
    {"webcontent",   NULL,             NULL},
    {NULL, NULL, NULL}
};

/* One of the events about one app's own things (OWNED_EVENTS) */
static gboolean event_is_owned(const char *service, const char *signal) {
    for (int i = 0; OWNED_EVENTS[i].service; i++)
        if (strcmp(OWNED_EVENTS[i].service, service) == 0 &&
            (!OWNED_EVENTS[i].signal || strcmp(OWNED_EVENTS[i].signal, signal) == 0))
            return TRUE;
    return FALSE;
}

static gboolean event_is_apps_own(const ZylAppManifest *m, const char *service,
                                  const char *signal, GVariant *params) {
    if (m->is_system) return TRUE;
    for (int i = 0; OWNED_EVENTS[i].service; i++) {
        if (strcmp(OWNED_EVENTS[i].service, service) != 0) continue;
        if (OWNED_EVENTS[i].signal && strcmp(OWNED_EVENTS[i].signal, signal) != 0) continue;
        if (OWNED_EVENTS[i].read_all && manifest_holds(m, OWNED_EVENTS[i].read_all)) return TRUE;
        if (!params || !g_variant_is_of_type(params, G_VARIANT_TYPE_TUPLE) ||
            g_variant_n_children(params) < 2)
            return FALSE;
        GVariant *owner = g_variant_get_child_value(params, 1);
        gboolean own = g_variant_is_of_type(owner, G_VARIANT_TYPE_STRING) &&
                       strcmp(g_variant_get_string(owner, NULL), m->id) == 0;
        g_variant_unref(owner);
        return own;
    }
    return TRUE;
}

static const ServiceRoute *find_route(const char *service) {
    for (int i = 0; SERVICE_ROUTES[i].service; i++) {
        if (strcmp(SERVICE_ROUTES[i].service, service) == 0)
            return &SERVICE_ROUTES[i];
    }
    return NULL;
}

/* == Service events (signals → apps) ======================== */

typedef struct {
    const ServiceRoute *route;
    ZylBridgeEventFn    fn;
    gpointer            data;
} EventSub;

static void on_service_signal(GDBusConnection *conn, const gchar *sender,
                              const gchar *path, const gchar *iface,
                              const gchar *signal, GVariant *params, gpointer user) {
    (void)conn; (void)sender; (void)path; (void)iface;
    EventSub *sub = user;
    sub->fn(sub->route->service, signal, params, sub->data);
}

void zyl_bridge_subscribe_service_events(GDBusConnection *bus,
                                         ZylBridgeEventFn fn, gpointer user_data) {
    for (int i = 0; SERVICE_ROUTES[i].service; i++) {
        EventSub *sub = g_new(EventSub, 1);   /* lives as long as the subscription */
        sub->route = &SERVICE_ROUTES[i];
        sub->fn = fn;
        sub->data = user_data;
        g_dbus_connection_signal_subscribe(bus, SERVICE_ROUTES[i].dbus_name,
            SERVICE_ROUTES[i].dbus_iface, NULL, SERVICE_ROUTES[i].dbus_path, NULL,
            G_DBUS_SIGNAL_FLAGS_NONE, on_service_signal, sub, g_free);
    }
}

/* An event reaches only apps allowed to call its service: a call or an SMS
   must not be visible to an app without the telephony permission. */
char *zyl_bridge_service_event_script(const ZylAppManifest *manifest,
                                      const char *service, const char *signal,
                                      GVariant *params) {
    if (!find_route(service) || !app_may_call(manifest, service) ||
        !event_is_apps_own(manifest, service, signal, params)) return NULL;
    /* A front-only service's stream (where the user is, the sensors) is the front
       app's too; the app's own events (a geofence it set) still reach it */
    if (!event_is_owned(service, signal) && !app_may_use_now(manifest, service)) return NULL;
    /* What the event needs (its own rule, else the service's): held, and if
       dangerous, allowed — an event never asks */
    const char *const *need = call_rule(service, "");
    for (int i = 0; EVENT_PERMISSIONS[i].service; i++)
        if (strcmp(EVENT_PERMISSIONS[i].service, service) == 0 &&
            strcmp(EVENT_PERMISSIONS[i].signal, signal) == 0) need = EVENT_PERMISSIONS[i].perms;
    if (!usable(manifest, need, NULL)) return NULL;
    JsonObject *msg = json_object_new();
    json_object_set_string_member(msg, "type", "service.event");
    json_object_set_string_member(msg, "service", service);
    json_object_set_string_member(msg, "event", signal);
    /* Arguments as an array, in the signal's order (one argument: that value) */
    json_object_set_member(msg, "data", json_from_variant(params));
    JsonNode *root = json_node_init_object(json_node_alloc(), msg);
    gchar *json = json_to_string(root, FALSE);
    /* The JSON text itself, quoted as a JS string literal */
    JsonNode *lit = json_node_init_string(json_node_alloc(), json);
    gchar *quoted = json_to_string(lit, FALSE);
    char *script = g_strdup_printf("window.postMessage(%s,'*')", quoted);
    g_free(quoted);
    json_node_unref(lit);
    g_free(json);
    json_node_unref(root);
    json_object_unref(msg);
    return script;
}

static char *capitalize_method(const char *method) {
    if (!method || !method[0]) return g_strdup("");
    char *cap = g_strdup(method);
    if (cap[0] >= 'a' && cap[0] <= 'z') cap[0] = (char)(cap[0] - 32);
    return cap;
}

/* == JSON params → typed D-Bus arguments ====================
 * Apps send one JSON object; services declare typed arguments
 * (Dial(s number), SetEnabled(b enabled), GetState()). This bridge used to
 * pass the JSON as a lone string, which matched almost no method, so GDBus
 * rejected nearly every device service call with InvalidArgs.
 * The service's own introspection XML is the authority: each in-argument is
 * filled from the JSON member of the same name. Case and '_' are ignored,
 * so a JS appId fills a C app_id.
 * An app_id argument from a third-party app is always the caller's own id:
 * otherwise any app could post notifications as com.zylos.messages.
 * A caller argument is the calling app for every app, system ones included,
 * and never comes from the request: whose data it is (clipboard source).
 * A container argument is "" for system apps (the user's shared files) and
 * the calling app for every other app (its own container), never the
 * request's: a third-party app's files are its own and go with it. */

static gboolean arg_name_matches(const char *dbus_name, const char *json_key) {
    const char *a = dbus_name, *b = json_key;
    for (;;) {
        while (*a == '_') a++;
        while (*b == '_') b++;
        if (!*a || !*b) return *a == *b;
        if (g_ascii_tolower(*a) != g_ascii_tolower(*b)) return FALSE;
        a++; b++;
    }
}

static JsonNode *find_param(JsonObject *params, const char *arg_name) {
    if (!params) return NULL;
    GList *members = json_object_get_members(params);
    JsonNode *found = NULL;
    for (GList *l = members; l && !found; l = l->next)
        if (arg_name_matches(arg_name, l->data))
            found = json_object_get_member(params, l->data);
    g_list_free(members);
    return found;
}

/* Returns the argument tuple (floating), or NULL with *error naming the bad argument.
 * app_id: the calling app; is_system: whether it may name another app in app_id. */
static GVariant *params_to_dbus_args(const GDBusMethodInfo *method,
                                     JsonObject *params, const char *app_id,
                                     gboolean is_system, GError **error) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_TUPLE);
    for (int i = 0; method->in_args && method->in_args[i]; i++) {
        const GDBusArgInfo *arg = method->in_args[i];
        JsonNode *node = find_param(params, arg->name);
        GVariant *v = NULL;
        if (strcmp(arg->signature, "s") == 0 && strcmp(arg->name, "caller") == 0) {
            v = g_variant_new_string(app_id);
        } else if (strcmp(arg->signature, "s") == 0 && strcmp(arg->name, "container") == 0) {
            v = g_variant_new_string(is_system ? "" : app_id);
        } else if (!is_system && strcmp(arg->signature, "s") == 0 &&
                   arg_name_matches(arg->name, "appid")) {
            v = g_variant_new_string(app_id);
        } else if (strcmp(arg->name, "params") == 0 && strcmp(arg->signature, "a{sv}") == 0) {
            /* The whole request: for a method whose fields are each optional
               (an update that changes only what it was given) */
            JsonObject *all = params ? json_object_ref(params) : json_object_new();
            JsonNode *whole = json_node_init_object(json_node_alloc(), all);
            v = json_gvariant_deserialize(whole, "a{sv}", NULL);
            json_node_unref(whole);
            json_object_unref(all);
            if (!v) g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "argument 'params' is not an object");
        } else if (!node) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "missing argument '%s'", arg->name);
        } else if (strcmp(arg->signature, "d") == 0 && JSON_NODE_HOLDS_VALUE(node) &&
                   json_node_get_value_type(node) == G_TYPE_INT64) {
            /* JS has one number type: 3 arrives as an integer for a double argument */
            v = g_variant_new_double((double)json_node_get_int(node));
        } else {
            GError *inner = NULL;
            v = json_gvariant_deserialize(node, arg->signature, &inner);
            if (!v) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "argument '%s' is not '%s'", arg->name, arg->signature);
                g_clear_error(&inner);
            }
        }
        if (!v) {
            g_variant_builder_clear(&b);
            return NULL;
        }
        g_variant_builder_add_value(&b, v);
    }
    return g_variant_builder_end(&b);
}

typedef struct {
    const ServiceRoute *route;
    char               *method;   /* D-Bus method name */
    char               *app_id;   /* the calling app */
    gboolean            is_system;
    JsonObject         *params;   /* owned ref, may be NULL */
    ZylBridgeReplyCtx   reply;
    GDBusConnection    *conn;     /* owned ref */
} ServiceCall;

static void service_call_free(ServiceCall *c) {
    ZYL_BRIDGE_VIEW_UNREF(c->reply.webview);
    g_free(c->method);
    g_free(c->app_id);
    if (c->params) json_object_unref(c->params);
    g_object_unref(c->conn);
    g_free(c);
}

static void service_call_invoke(ServiceCall *c, GDBusNodeInfo *node) {
    GDBusInterfaceInfo *iface =
        g_dbus_node_info_lookup_interface(node, c->route->dbus_iface);
    GDBusMethodInfo *m =
        iface ? g_dbus_interface_info_lookup_method(iface, c->method) : NULL;
    if (!m) {
        zyl_bridge_reply_error(&c->reply, "unknown method");
        service_call_free(c);
        return;
    }

    GError *err = NULL;
    GVariant *args = params_to_dbus_args(m, c->params, c->app_id, c->is_system, &err);
    if (!args) {
        g_warning("Bridge: %s.%s: %s", c->route->service, c->method, err->message);
        zyl_bridge_reply_error(&c->reply, err->message);
        g_error_free(err);
        service_call_free(c);
        return;
    }

    if (c->reply.callback_id >= 0) {
        DbusReplyCtx *ctx = g_new(DbusReplyCtx, 1);
        ctx->webview     = ZYL_BRIDGE_VIEW_REF(c->reply.webview);
        ctx->callback_id = c->reply.callback_id;
        g_object_ref(c->conn);   /* released by on_dbus_call_done */
        g_dbus_connection_call(c->conn,
            c->route->dbus_name, c->route->dbus_path,
            c->route->dbus_iface, c->method, args,
            NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
            on_dbus_call_done, ctx);
    } else {
        g_dbus_connection_call(c->conn,
            c->route->dbus_name, c->route->dbus_path,
            c->route->dbus_iface, c->method, args,
            NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL, NULL);
    }
    service_call_free(c);
}

static void on_introspect_done(GObject *source, GAsyncResult *res, gpointer data) {
    ServiceCall *c = data;
    GError *err = NULL;
    GDBusNodeInfo *node = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    if (reply) {
        const char *xml = NULL;
        g_variant_get(reply, "(&s)", &xml);
        node = g_dbus_node_info_new_for_xml(xml, &err);
        g_variant_unref(reply);
    }
    if (!node) {
        g_warning("Bridge: cannot introspect %s: %s", c->route->dbus_name,
                  err ? err->message : "no data");
        zyl_bridge_reply_error(&c->reply, "service unavailable");
        g_clear_error(&err);
        service_call_free(c);
        return;
    }
    if (!introspect_cache)
        introspect_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                 (GDestroyNotify)g_dbus_node_info_unref);
    g_hash_table_replace(introspect_cache, g_strdup(c->route->service), node);
    service_call_invoke(c, node);
}

/* == First-use consent =====================================
 * A request waiting for the user's answer (the WAM draws the question in the
 * system UI): the same request again on yes, "Permission denied" on no. */
static ZylBridgeConsentAsk consent_ask;

void zyl_bridge_set_consent(ZylBridgeConsentAsk ask)
{
    consent_ask = ask;
}

typedef struct {
    JsonObject        *msg;
    char              *app_id, *perm;
    ZylBridgeReplyCtx  reply;
} PendingRequest;

static void handle_service_request(const char *type, gpointer msg_obj,
                                    ZylAppManifest *manifest,
                                    ZylBridgeReplyCtx *reply_ctx, gpointer data);

/* answered: the user said yes or no (kept); FALSE: no answer this time (not kept) */
static void on_request_consent(gboolean allowed, gboolean answered, ZylAppManifest *manifest, gpointer data)
{
    PendingRequest *pr = data;
    if (answered) zyl_bridge_consent_answered(pr->app_id, pr->perm, allowed);
    if (allowed && manifest && manifest->id && strcmp(manifest->id, pr->app_id) == 0)
        handle_service_request("service.request", pr->msg, manifest, &pr->reply, NULL);
    else
        zyl_bridge_reply_error(&pr->reply, "Permission denied");
    ZYL_BRIDGE_VIEW_UNREF(pr->reply.webview);
    json_object_unref(pr->msg);
    g_free(pr->app_id);
    g_free(pr->perm);
    g_free(pr);
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

    const char *service = zyl_json_str(msg, "service", NULL);
    const char *method  = zyl_json_str(msg, "method", NULL);

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

    if (!app_may_call(manifest, service) || !app_may_call_method(manifest, service, method)) {
        g_warning("Bridge: %s denied %s.%s (missing permission)",
                  app_id, service, method);
        zyl_bridge_reply_error(reply_ctx, "Permission denied");
        return;
    }
    if (!app_may_use_now(manifest, service)) {
        g_warning("Bridge: %s denied %s.%s (not in front)", app_id, service, method);
        zyl_bridge_reply_error(reply_ctx, "Not in front");
        return;
    }
    /* A dangerous permission the user has not answered yet: asked first, then
       this same request again. Only the app in front asks — a question from an
       app the user is not looking at is not one to answer */
    const char *ask = consent_needed(manifest, service, method);
    if (ask) {
        if (!consent_ask || !front_query || !front_query(app_id)) {
            g_warning("Bridge: %s denied %s.%s (%s not allowed yet)", app_id, service, method, ask);
            zyl_bridge_reply_error(reply_ctx, "Permission denied");
            return;
        }
        PendingRequest *pr = g_new0(PendingRequest, 1);
        pr->msg = json_object_ref(msg);
        pr->app_id = g_strdup(app_id);
        pr->perm = g_strdup(ask);
        pr->reply = *reply_ctx;
        pr->reply.webview = ZYL_BRIDGE_VIEW_REF(reply_ctx->webview);
        consent_ask(app_id, ask, on_request_consent, pr);
        return;
    }

    char *dbus_method = capitalize_method(method);

    g_info("Bridge: service.request from %s → %s.%s → D-Bus %s.%s",
           app_id, service, method, route->dbus_name, dbus_method);

    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (err) {
        g_warning("Bridge: system bus error: %s", err->message);
        zyl_bridge_reply_error(reply_ctx, "dbus unavailable");
        g_error_free(err);
        g_free(dbus_method);
        return;
    }

    ServiceCall *call = g_new0(ServiceCall, 1);
    call->route  = route;
    call->method = dbus_method;
    call->app_id = g_strdup(app_id);
    call->is_system = manifest->is_system;
    call->reply  = *reply_ctx;
    call->reply.webview = ZYL_BRIDGE_VIEW_REF(reply_ctx->webview);   /* service_call_free */
    call->conn   = bus;   /* g_bus_get_sync returned a ref; the call owns it */
    JsonNode *params_node = json_object_get_member(msg, "params");
    if (params_node && JSON_NODE_HOLDS_OBJECT(params_node))
        call->params = json_object_ref(json_node_get_object(params_node));

    GDBusNodeInfo *cached = introspect_cache
        ? g_hash_table_lookup(introspect_cache, route->service) : NULL;
    if (cached) {
        service_call_invoke(call, cached);
        return;
    }
    g_dbus_connection_call(bus,
        route->dbus_name, route->dbus_path,
        "org.freedesktop.DBus.Introspectable", "Introspect", NULL,
        G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
        on_introspect_done, call);
}
