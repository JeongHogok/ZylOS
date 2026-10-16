/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: WAM(Web App Manager) 메인 서비스 — 모듈 조립 및 WebKitGTK 구현
 * 수행범위: manifest, lifecycle, bridge, D-Bus 모듈 연결, ZylWebEngine/ZylAppInterface vtable 구현,
 *           셸 정책(shell.h)을 따라 앞에 올 앱 띄우기 — 부팅 시 설정/잠금 화면,
 *           화면 꺼짐 잠금, 홈·뒤로·앱 전환(전원/하드웨어 키, 컴포지터 제스처)
 * 의존방향: wam.h, manifest.h, lifecycle.h, bridge.h, dbus_service.h, oom.h, intent.h,
 *           shell.h, app_scheme.h, app_seal.h (user apps run only as sealed), hal.h (device ID),
 *           power.h·input.h (the power and input services' D-Bus contract values)
 * SOLID: SRP — 모듈 조립과 WebKitGTK 바인딩만 담당
 * ────────────────────────────────────────────────────────── */

#include "wam.h"
#include "oom/oom.h"
#include "intent/intent.h"
#include "scheme/app_scheme.h"
#include "container.h"
#include "app_seal.h"
#include "../../services/power/power.h"
#include "../../services/input/input.h"
#include "hal.h"

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-glib/json-glib.h>
#ifdef ZYL_USE_WEBKIT2GTK
#include <webkit2/webkit2.h>
#else
#include <webkit/webkit.h>
#endif

static ZylWam       *g_wam = NULL;
static ZylOomKiller *g_oom = NULL;
static GDBusConnection *g_event_bus;   /* held for the WAM's lifetime: the
                                          subscriptions die with the connection */

/* ════════════════════════════════════════════════════════════════
 *  ZylAppInterface implementation (backed by GHashTable lookups)
 * ════════════════════════════════════════════════════════════════ */

static ZylAppManifest *iface_get_manifest(ZylAppInterface *self,
                                          const char      *app_id) {
    ZylWam *wam = self->impl_data;
    return g_hash_table_lookup(wam->manifests, app_id);
}

static gpointer iface_get_instance(ZylAppInterface *self,
                                   const char      *app_id) {
    ZylWam *wam = self->impl_data;
    return g_hash_table_lookup(wam->instances, app_id);
}

static void iface_store_instance(ZylAppInterface *self,
                                 const char      *app_id,
                                 gpointer         instance) {
    ZylWam *wam = self->impl_data;
    g_hash_table_insert(wam->instances, g_strdup(app_id), instance);
}

static void iface_remove_instance(ZylAppInterface *self,
                                  const char      *app_id) {
    ZylWam *wam = self->impl_data;
    g_hash_table_remove(wam->instances, app_id);
}

static int iface_count_instances(ZylAppInterface *self) {
    ZylWam *wam = self->impl_data;
    return (int)g_hash_table_size(wam->instances);
}

/* ════════════════════════════════════════════════════════════════
 *  Bridge handler registry is managed by bridge.c (H5).
 *  WAM-specific handlers for app lifecycle are registered below.
 * ════════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════════
 *  Intents (rule 9): an app starts one, the WAM decides which app gets it
 *  (intent/intent.c). Several → the starting app shows the choice and
 *  reports the pick, which must be one offered; none → it says so.
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    char       *token;
    char       *source;
    GPtrArray  *targets;   /* owned ids */
    JsonObject *intent;    /* what the handler gets */
} IntentChoice;

static IntentChoice g_choice;   /* the one open choice */
static guint g_next_token = 1;

static void choice_clear(void) {
    g_clear_pointer(&g_choice.token, g_free);
    g_clear_pointer(&g_choice.source, g_free);
    g_clear_pointer(&g_choice.targets, g_ptr_array_unref);
    g_clear_pointer(&g_choice.intent, json_object_unref);
}

static void post_to(WebKitWebView *webview, const char *type, JsonNode *data) {
    char *script = zyl_intent_post_script(type, data);
    webkit_web_view_evaluate_javascript(webview, script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

static void deliver_when_loaded(WebKitWebView *webview, WebKitLoadEvent ev, gpointer script) {
    if (ev != WEBKIT_LOAD_FINISHED) return;
    g_signal_handlers_disconnect_by_func(webview, deliver_when_loaded, script);
    webkit_web_view_evaluate_javascript(webview, script, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(script);
}

/* ════════════════════════════════════════════════════════════════
 *  Shell: which app is in front is the policy's (shell/shell.c — the same
 *  rules as the emulator's apps/system/shell.js). Everything that brings an
 *  app forward goes through wam_open (asks) or wam_show (the policy chose).
 * ════════════════════════════════════════════════════════════════ */

/* Bring the app the policy chose to the front; NULL app: stay */
static ZylAppInstance *wam_show(ZylWam *wam, const char *app_id) {
    if (!app_id) return NULL;
    gboolean running = wam->iface.get_instance(&wam->iface, app_id) != NULL;
    ZylAppInstance *inst = zyl_lifecycle_launch(&wam->iface, &wam->engine, app_id);
    if (!inst) return NULL;
    if (!running) zyl_oom_on_app_launched(g_oom, app_id);
    zyl_oom_on_app_foreground(g_oom, app_id);
    zyl_shell_shown(&wam->shell, app_id);
    return inst;
}

/* An app, a notification, an intent or a service wants this app in front */
static ZylAppInstance *wam_open(ZylWam *wam, const char *app_id) {
    if (!zyl_shell_may_launch(&wam->shell, app_id)) {
        g_message("WAM: %s not now (setup or locked)", app_id ? app_id : "(none)");
        return NULL;
    }
    return wam_show(wam, app_id);
}

/* Home, and where an app the user leaves goes: the lock screen while locked */
static void wam_go_home(ZylWam *wam) {
    wam_show(wam, zyl_shell_home(&wam->shell));
}

/* Never what the policy keeps; the front app going hands the screen to home */
static void wam_close(ZylWam *wam, const char *app_id) {
    if (!app_id || !zyl_shell_may_close(&wam->shell, app_id)) return;
    gboolean front = strcmp(wam->shell.top, app_id) == 0;
    zyl_lifecycle_close(&wam->iface, app_id);
    zyl_oom_on_app_closed(g_oom, app_id);
    if (front) wam_go_home(wam);
}

/* Back goes to the app in front first (navigation.back, as in the emulator).
   It answers navigation.handled (stays) or navigation.exit; no answer in time
   is an exit — a hung app must not trap the user. */
#define WAM_BACK_ANSWER_MS 300

static gboolean back_unanswered(gpointer data) {
    ZylWam *wam = data;
    wam->back_timer = 0;
    wam_go_home(wam);
    return G_SOURCE_REMOVE;
}

static void wam_go_back(ZylWam *wam) {
    if (wam->back_timer) return;
    ZylAppInstance *inst = wam->iface.get_instance(&wam->iface, wam->shell.top);
    if (!inst || !inst->webview_widget) { wam_go_home(wam); return; }
    post_to(WEBKIT_WEB_VIEW(inst->webview_widget), "navigation.back",
            zyl_intent_object_node(json_object_new()));
    wam->back_timer = g_timeout_add(WAM_BACK_ANSWER_MS, back_unanswered, wam);
}

/* Only the answer of the app that was asked counts */
static void wam_handle_navigation(const char        *type,
                                  gpointer           msg_obj,
                                  ZylAppManifest    *manifest,
                                  ZylBridgeReplyCtx *reply_ctx,
                                  gpointer           user_data) {
    (void)msg_obj; (void)reply_ctx;
    ZylWam *wam = user_data;
    if (!manifest || !wam->back_timer || strcmp(manifest->id, wam->shell.top) != 0) return;
    g_source_remove(wam->back_timer);
    wam->back_timer = 0;
    if (strcmp(type, "navigation.exit") == 0) wam_go_home(wam);
}

static gint compare_ids(gconstpointer a, gconstpointer b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* The next (direction > 0) or previous running app the user returns to —
   system UI ("launcher": false) is not one, as in the emulator's recents */
static void wam_switch_app(ZylWam *wam, int direction) {
    if (!zyl_shell_navigable(&wam->shell)) return;
    GPtrArray *ids = g_ptr_array_new();
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, wam->instances);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        ZylAppInstance *inst = value;
        if (inst->manifest && inst->manifest->launcher) g_ptr_array_add(ids, key);
    }
    g_ptr_array_sort(ids, compare_ids);
    int at = -1;
    for (guint i = 0; i < ids->len; i++)
        if (strcmp(g_ptr_array_index(ids, i), wam->shell.top) == 0) at = (int)i;
    int n = (int)ids->len;
    if (n >= 2 && at >= 0) {
        char *next = g_strdup(g_ptr_array_index(ids, ((at + (direction > 0 ? 1 : -1)) % n + n) % n));
        wam_open(wam, next);
        g_free(next);
    }
    g_ptr_array_unref(ids);
}

/* Only the lock screen unlocks; it opens what the user was opening, or home */
static void wam_handle_unlock(const char        *type,
                              gpointer           msg_obj,
                              ZylAppManifest    *manifest,
                              ZylBridgeReplyCtx *reply_ctx,
                              gpointer           user_data) {
    (void)type; (void)reply_ctx;
    ZylWam *wam = user_data;
    const char *app_id = json_object_get_string_member_with_default(msg_obj, "appId", NULL);
    const char *to = manifest ? zyl_shell_unlock(&wam->shell, manifest->id, app_id) : NULL;
    if (!to) {
        g_warning("WAM: unlock from %s refused", manifest ? manifest->id : "(unknown)");
        return;
    }
    g_message("WAM: unlocked");
    if (!wam_show(wam, to)) wam_go_home(wam);   /* that app is gone: home */
}

/* Only setup finishes setup; then home, unlocked (the user just set it up) */
static void wam_handle_setup_done(const char        *type,
                                  gpointer           msg_obj,
                                  ZylAppManifest    *manifest,
                                  ZylBridgeReplyCtx *reply_ctx,
                                  gpointer           user_data) {
    (void)type; (void)msg_obj; (void)reply_ctx;
    ZylWam *wam = user_data;
    wam_show(wam, manifest ? zyl_shell_setup_done(&wam->shell, manifest->id) : NULL);
}

/* An app closes itself — the bridge names it, never the message */
static void wam_handle_app_close(const char        *type,
                                 gpointer           msg_obj,
                                 ZylAppManifest    *manifest,
                                 ZylBridgeReplyCtx *reply_ctx,
                                 gpointer           user_data) {
    (void)type; (void)msg_obj; (void)reply_ctx;
    if (manifest) wam_close(user_data, manifest->id);
}

static void wam_handle_app_launch(const char        *type,
                                  gpointer           msg_obj,
                                  ZylAppManifest    *manifest,
                                  ZylBridgeReplyCtx *reply_ctx,
                                  gpointer           user_data) {
    (void)type; (void)manifest;
    ZylWam *wam = user_data;
    const char *target = json_object_get_string_member_with_default(msg_obj, "appId", NULL);
    if (!target) {
        /* Missing appId — resolve the promise with an error so it doesn't leak */
        if (reply_ctx && reply_ctx->callback_id >= 0) zyl_bridge_reply_error(reply_ctx, "missing appId");
        return;
    }
    ZylAppInstance *inst = wam_open(wam, target);
    /* Acknowledge launch to JS if a callback was requested */
    if (reply_ctx && reply_ctx->callback_id >= 0) {
        zyl_bridge_respond(reply_ctx->webview, reply_ctx->callback_id,
                           inst ? "{\"ok\":true}" : "{\"error\":true,\"message\":\"launch failed\"}");
    }
}

/* Launch the handler and hand it the intent once its page is there */
static void launch_with_intent(ZylWam *wam, const char *app_id, JsonObject *intent) {
    gboolean running = wam->iface.get_instance(&wam->iface, app_id) != NULL;
    ZylAppInstance *inst = wam_open(wam, app_id);
    if (!inst || !inst->webview_widget) return;
    char *script = zyl_intent_post_script("intent", zyl_intent_object_node(json_object_ref(intent)));
    WebKitWebView *webview = WEBKIT_WEB_VIEW(inst->webview_widget);
    if (running) {
        webkit_web_view_evaluate_javascript(webview, script, -1, NULL, NULL, NULL, NULL, NULL);
        g_free(script);
    } else {
        g_signal_connect(webview, "load-changed", G_CALLBACK(deliver_when_loaded), script);
    }
}

static void wam_handle_intent_start(const char        *type,
                                    gpointer           msg_obj,
                                    ZylAppManifest    *manifest,
                                    ZylBridgeReplyCtx *reply_ctx,
                                    gpointer           user_data) {
    (void)type;
    ZylWam *wam = user_data;
    JsonObject *msg = msg_obj;
    JsonNode *in = json_object_has_member(msg, "intent") ? json_object_get_member(msg, "intent") : NULL;
    if (!manifest || !reply_ctx || !in || !JSON_NODE_HOLDS_OBJECT(in)) return;
    JsonObject *intent = json_node_get_object(in);

    GPtrArray *targets = zyl_intent_resolve(wam->manifests, manifest->id, intent);
    JsonObject *deliver = zyl_intent_deliverable(intent, manifest->id);
    if (targets->len == 0) {
        post_to(reply_ctx->webview, "intent.none", zyl_intent_object_node(json_object_new()));
    } else if (targets->len == 1) {
        launch_with_intent(wam, g_ptr_array_index(targets, 0), deliver);
    } else {
        choice_clear();
        g_choice.token = g_strdup_printf("%u", g_next_token++);
        g_choice.source = g_strdup(manifest->id);
        g_choice.targets = g_ptr_array_new_with_free_func(g_free);
        g_choice.intent = json_object_ref(deliver);
        JsonArray *choices = json_array_new();
        for (guint i = 0; i < targets->len; i++) {
            const char *id = g_ptr_array_index(targets, i);
            ZylAppManifest *m = g_hash_table_lookup(wam->manifests, id);
            JsonObject *c = json_object_new();
            json_object_set_string_member(c, "appId", id);
            json_object_set_string_member(c, "name", m && m->name ? m->name : id);
            json_array_add_object_element(choices, c);
            g_ptr_array_add(g_choice.targets, g_strdup(id));
        }
        JsonObject *data = json_object_new();
        json_object_set_string_member(data, "token", g_choice.token);
        json_object_set_array_member(data, "choices", choices);
        post_to(reply_ctx->webview, "intent.choose", zyl_intent_object_node(data));
    }
    json_object_unref(deliver);
    g_ptr_array_unref(targets);
}

/* The pick: only the latest choice, from the app it was offered to, for an app offered */
static void wam_handle_intent_chosen(const char        *type,
                                     gpointer           msg_obj,
                                     ZylAppManifest    *manifest,
                                     ZylBridgeReplyCtx *reply_ctx,
                                     gpointer           user_data) {
    (void)type; (void)reply_ctx;
    ZylWam *wam = user_data;
    JsonObject *msg = msg_obj;
    const char *token = json_object_get_string_member_with_default(msg, "token", NULL);
    const char *app_id = json_object_get_string_member_with_default(msg, "appId", NULL);
    if (!manifest || !token || !app_id || !g_choice.token ||
        strcmp(token, g_choice.token) != 0 || strcmp(manifest->id, g_choice.source) != 0) return;
    for (guint i = 0; i < g_choice.targets->len; i++) {
        if (strcmp(g_ptr_array_index(g_choice.targets, i), app_id) != 0) continue;
        char *target = g_strdup(app_id);
        JsonObject *intent = json_object_ref(g_choice.intent);
        choice_clear();
        launch_with_intent(wam, target, intent);
        json_object_unref(intent);
        g_free(target);
        return;
    }
}

/* ════════════════════════════════════════════════════════════════
 *  WebKit bridge message signal handler
 * ════════════════════════════════════════════════════════════════ */

/* WebKitGTK 6.0 (the GTK 4 port) hands the JSCValue over directly; the
   webkit2gtk-4.x API wrapped it in a WebKitJavascriptResult */
#ifdef ZYL_USE_WEBKIT2GTK
static void on_webkit_bridge_message(WebKitUserContentManager *manager,
                                     WebKitJavascriptResult   *result,
                                     gpointer                  user_data) {
    JSCValue *value = webkit_javascript_result_get_js_value(result);
#else
static void on_webkit_bridge_message(WebKitUserContentManager *manager,
                                     JSCValue                 *value,
                                     gpointer                  user_data) {
#endif
    ZylAppInstance *instance = user_data;
    (void)manager;

    char *msg_str = jsc_value_to_string(value);

    /* H5: Dispatch via handler registry; H13: error handling inside */
    zyl_bridge_dispatch(WEBKIT_WEB_VIEW(instance->webview_widget),
                        instance->manifest,
                        msg_str);

    g_free(msg_str);
}

/* ════════════════════════════════════════════════════════════════
 *  App origins and containers: each app is zyl-app://<id>/ — its own
 *  origin — and keeps its WebKit storage (local storage, IndexedDB,
 *  cookies, cache) in its container, so nothing of it outlives the app
 * ════════════════════════════════════════════════════════════════ */

/* Served from memory: what was hashed is what the app gets. A user-installed
   app's file is served only if it is byte for byte what was sealed — code
   changed after install never runs. The OS's own (/shared/, system apps) is
   under dm-verity. */
static void on_app_request(WebKitURISchemeRequest *req, gpointer user_data) {
    ZylWam *wam = user_data;
    GUri *uri = g_uri_parse(webkit_uri_scheme_request_get_uri(req), G_URI_FLAGS_NONE, NULL);
    const char *path = uri ? g_uri_get_path(uri) : NULL;
    ZylAppManifest *m = uri && g_uri_get_host(uri)
                        ? g_hash_table_lookup(wam->manifests, g_uri_get_host(uri)) : NULL;
    char *file = m ? zyl_app_scheme_file(m->base_path, WAM_SHARED_DIR, path) : NULL;
    gchar *data = NULL;
    gsize len = 0;
    bool ok = file && g_file_get_contents(file, &data, &len, NULL);
    if (ok && !m->is_system && !g_str_has_prefix(path, "/shared/")) {
        ZylSeal *seal = g_hash_table_lookup(wam->seals, m->id);
        ok = seal && zyl_seal_bytes_ok(seal, path + 1, data, len);
        if (!ok) g_warning("WAM: %s%s is not as sealed — not served", m->id, path);
    }
    if (ok) {
        GBytes *bytes = g_bytes_new_take(data, len);
        GInputStream *in = g_memory_input_stream_new_from_bytes(bytes);
        webkit_uri_scheme_request_finish(req, in, (gint64)len, zyl_app_scheme_mime(file));
        g_object_unref(in);
        g_bytes_unref(bytes);
    } else {
        g_free(data);
        GError *e = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "not found");
        webkit_uri_scheme_request_finish_error(req, e);
        g_error_free(e);
    }
    g_free(file);
    if (uri) g_uri_unref(uri);
}

/* A user-installed app runs only if its seal holds: signed by the store, or by
   a developer the store allowed on this device, and every file as sealed */
static ZylSeal *seal_app(const ZylAppManifest *m) {
    char *pem = zyl_seal_store_key(WAM_STORE_CONFIG);
    char **revoked = zyl_seal_load_revoked(WAM_REVOKED);
    ZylBoardInfo board;
    memset(&board, 0, sizeof(board));
    zyl_hal_board()->get(&board);
    ZylSeal *seal = NULL;
    ZylSealVerdict v = zyl_seal_open(m->base_path, m->id, m->version, pem, board.device_id,
                                     (const char *const *)revoked, g_get_real_time() / G_USEC_PER_SEC,
                                     true, &seal);
    if (v != ZYL_SEAL_OK) g_warning("WAM: %s refused: %s", m->id, zyl_seal_verdict_message(v));
    g_strfreev(revoked);
    g_free(pem);
    return seal;
}

/* Drop every user-installed app whose seal does not hold; keep the seals */
static void seal_user_apps(ZylWam *wam) {
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, wam->manifests);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        ZylAppManifest *m = value;
        if (m->is_system) continue;
        ZylSeal *seal = seal_app(m);
        if (seal) g_hash_table_replace(wam->seals, g_strdup(m->id), seal);
        else g_hash_table_iter_remove(&it);
    }
}

/* Secure, so apps get secure-context APIs (crypto.subtle …) as on https */
static void register_app_scheme(WebKitWebContext *ctx) {
    webkit_web_context_register_uri_scheme(ctx, ZYL_APP_SCHEME, on_app_request, g_wam, NULL);
    webkit_security_manager_register_uri_scheme_as_secure(
        webkit_web_context_get_security_manager(ctx), ZYL_APP_SCHEME);
}

/* <container>/webkit/{data,cache}; NULL when they cannot be made */
static gboolean app_webkit_dirs(const ZylAppManifest *m, char **data, char **cache) {
    char *base = g_build_filename(WAM_CONTAINERS, m->id, "webkit", NULL);
    *data = g_build_filename(base, "data", NULL);
    *cache = g_build_filename(base, "cache", NULL);
    g_free(base);
    if (g_mkdir_with_parents(*data, 0700) == 0 && g_mkdir_with_parents(*cache, 0700) == 0) return TRUE;
    g_warning("WAM: no container for %s — its web storage will not persist", m->id);
    g_clear_pointer(data, g_free);
    g_clear_pointer(cache, g_free);
    return FALSE;
}

/* ════════════════════════════════════════════════════════════════
 *  ZylWebEngine implementation (WebKitGTK back-end)
 * ════════════════════════════════════════════════════════════════ */

static GtkWidget *webkit_create_webview(ZylWebEngine   *self,
                                        ZylAppManifest *manifest,
                                        gpointer        instance_ctx) {
    (void)self;

    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);

    /* H10: Security hardening — restrict file and cross-origin access */
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, FALSE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, FALSE);

    WebKitUserContentManager *ucm = webkit_user_content_manager_new();
    g_signal_connect(ucm, "script-message-received::bridge",
                     G_CALLBACK(on_webkit_bridge_message), instance_ctx);

    /* The app's own storage. H11: TLS certificate verification — reject
       invalid certificates (6.0 moved the policy to the network session). */
    char *data_dir = NULL, *cache_dir = NULL;
    gboolean persistent = app_webkit_dirs(manifest, &data_dir, &cache_dir);
#ifdef ZYL_USE_WEBKIT2GTK
    WebKitWebsiteDataManager *store = persistent
        ? webkit_website_data_manager_new("base-data-directory", data_dir,
                                          "base-cache-directory", cache_dir, NULL)
        : webkit_website_data_manager_new_ephemeral();
    WebKitWebContext *ctx = webkit_web_context_new_with_website_data_manager(store);
    /* The app's web process is isolated by WebKit itself: bubblewrap namespaces
       + seccomp. webkitgtk-6.0 has it always on and dropped this setter. */
    webkit_web_context_set_sandbox_enabled(ctx, TRUE);
    register_app_scheme(ctx);
    webkit_web_context_set_tls_errors_policy(ctx, WEBKIT_TLS_ERRORS_POLICY_FAIL);
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge");
    WebKitWebView *webview = WEBKIT_WEB_VIEW(
        g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", ctx, "user-content-manager", ucm, NULL));
    g_object_unref(ctx);
    g_object_unref(store);
#else
    WebKitNetworkSession *session = persistent
        ? webkit_network_session_new(data_dir, cache_dir)
        : webkit_network_session_new_ephemeral();
    webkit_network_session_set_tls_errors_policy(session, WEBKIT_TLS_ERRORS_POLICY_FAIL);
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge", NULL);
    WebKitWebView *webview = WEBKIT_WEB_VIEW(
        g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", session, "user-content-manager", ucm, NULL));
    g_object_unref(session);
#endif
    g_free(data_dir);
    g_free(cache_dir);
    webkit_web_view_set_settings(webview, settings);

    /* Inject JS bridge from external file */
    zyl_bridge_inject(WAM_BRIDGE_JS, webview, manifest);

    /* The device class is the hardware's: the image's build profile, declared
       before any app script runs (apps/shared/form-factor.js reads it) */
    WebKitUserScript *ff_script = webkit_user_script_new(
        "window.__zylFormFactor='" ZYL_FORM_FACTOR "';",
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, ff_script);
    webkit_user_script_unref(ff_script);

    /* H10: Content Security Policy injection via user script */
    const char *csp_script =
        "var meta = document.createElement('meta');"
        "meta.httpEquiv = 'Content-Security-Policy';"
        "meta.content = \"default-src 'self'; script-src 'self' 'unsafe-inline';"
        " style-src 'self' 'unsafe-inline'; img-src 'self' data:;\";"
        "document.head.appendChild(meta);";

    WebKitUserScript *csp_user_script = webkit_user_script_new(
        csp_script,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, csp_user_script);
    webkit_user_script_unref(csp_user_script);

    g_object_unref(settings);
    return GTK_WIDGET(webview);
}

static void webkit_load_uri(ZylWebEngine *self,
                            GtkWidget    *webview_widget,
                            const char   *uri) {
    (void)self;
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview_widget), uri);
}

/* ════════════════════════════════════════════════════════════════
 *  D-Bus method implementations (dispatch table entries)
 * ════════════════════════════════════════════════════════════════ */

#define WAM_MAX_APPS_WARN 5

static void dbus_launch(GVariant *params, GDBusMethodInvocation *inv,
                        gpointer user_data) {
    ZylWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    ZylAppInstance *inst = wam_open(wam, app_id);
    if (inst) {
        int count = zyl_lifecycle_get_running_count(&wam->iface);
        if (count > WAM_MAX_APPS_WARN) {
            g_warning("Memory pressure: %d apps running (threshold %d)",
                      count, WAM_MAX_APPS_WARN);
        }
        zyl_oom_check_pressure(g_oom);
    }
    g_dbus_method_invocation_return_value(inv,
        g_variant_new("(b)", inst != NULL));
}

static void dbus_close(GVariant *params, GDBusMethodInvocation *inv,
                       gpointer user_data) {
    ZylWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    wam_close(wam, app_id);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_suspend(GVariant *params, GDBusMethodInvocation *inv,
                         gpointer user_data) {
    ZylWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    /* Hiding is closing for the policy: never the lock screen while locked */
    if (zyl_shell_may_close(&wam->shell, app_id)) {
        gboolean front = strcmp(wam->shell.top, app_id) == 0;
        zyl_lifecycle_suspend(&wam->iface, app_id);
        if (front) wam_go_home(wam);
    }
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_resume(GVariant *params, GDBusMethodInvocation *inv,
                        gpointer user_data) {
    ZylWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    /* Resuming shows it: only what may be in front now */
    if (zyl_shell_may_launch(&wam->shell, app_id) && wam->iface.get_instance(&wam->iface, app_id)) {
        zyl_lifecycle_resume(&wam->iface, app_id);
        zyl_oom_on_app_foreground(g_oom, app_id);
        zyl_shell_shown(&wam->shell, app_id);
    }
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_list_apps(GVariant *params, GDBusMethodInvocation *inv,
                           gpointer user_data) {
    ZylWam *wam = user_data;
    (void)params;
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, wam->manifests);
    while (g_hash_table_iter_next(&iter, &key, NULL))
        g_variant_builder_add(&builder, "s", (const char *)key);
    g_dbus_method_invocation_return_value(inv,
        g_variant_new("(as)", &builder));
}

/* apps.getInstalled — every installed app as apps see it (manifest.c) */
static void dbus_get_installed(GVariant *params, GDBusMethodInvocation *inv,
                               gpointer user_data) {
    ZylWam *wam = user_data;
    (void)params;
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, wam->manifests);
    while (g_hash_table_iter_next(&iter, NULL, &value))
        g_variant_builder_add_value(&list, zyl_manifest_app_info(value));
    g_dbus_method_invocation_return_value(inv, g_variant_new("(aa{sv})", &list));
}

static void dbus_list_running(GVariant *params, GDBusMethodInvocation *inv,
                              gpointer user_data) {
    ZylWam *wam = user_data;
    (void)params;
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, wam->instances);
    while (g_hash_table_iter_next(&iter, &key, NULL))
        g_variant_builder_add(&builder, "s", (const char *)key);
    int count = zyl_lifecycle_get_running_count(&wam->iface);
    g_dbus_method_invocation_return_value(inv,
        g_variant_new("(asi)", &builder, count));
}

/* Navigation from the compositor's gestures (the bus lets only root and the
   WAM call here); what happens is the policy's */
static void dbus_go_home(GVariant *params, GDBusMethodInvocation *inv, gpointer user_data) {
    (void)params;
    wam_go_home(user_data);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_go_back(GVariant *params, GDBusMethodInvocation *inv, gpointer user_data) {
    (void)params;
    wam_go_back(user_data);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_switch_app(GVariant *params, GDBusMethodInvocation *inv, gpointer user_data) {
    gint32 direction = 1;
    g_variant_get(params, "(i)", &direction);
    wam_switch_app(user_data, direction);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static const ZylDbusMethodEntry dbus_methods[] = {
    { "Launch",      dbus_launch      },
    { "Close",       dbus_close       },
    { "Suspend",     dbus_suspend     },
    { "Resume",      dbus_resume      },
    { "ListApps",    dbus_list_apps   },
    { "GetInstalled", dbus_get_installed },
    { "ListRunning", dbus_list_running },
    { "GoHome",      dbus_go_home     },
    { "GoBack",      dbus_go_back     },
    { "SwitchApp",   dbus_switch_app  },
    { NULL, NULL }
};

/* ════════════════════════════════════════════════════════════════
 *  Service events → apps
 *  Every routed OS service's signals reach the running apps allowed to
 *  call that service (bridge.c decides and builds the message). Some
 *  events must also bring an app to the front: it then asks its service
 *  for the current state when it loads, so nothing is lost while it starts.
 * ════════════════════════════════════════════════════════════════ */

static const char *app_to_show(const char *service, const char *signal, GVariant *params) {
    if (strcmp(service, "telephony") != 0) return NULL;
    if (strcmp(signal, "EmergencyAlert") == 0) return "com.zylos.alerts";
    if (strcmp(signal, "CallStateChanged") == 0 &&
        g_variant_is_of_type(params, G_VARIANT_TYPE("(sss)"))) {
        const gchar *now = NULL;
        g_variant_get(params, "(&s&s&s)", NULL, &now, NULL);
        if (strcmp(now, "RINGING") == 0) return "com.zylos.phone";
    }
    return NULL;
}

/* The app store installed or removed an app: the WAM's own part — its
   manifest, its running instance, its web storage in the container */
static void on_store_event(ZylWam *wam, const char *signal, GVariant *params) {
    const gchar *id = NULL;
    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) return;
    g_variant_get(params, "(&s)", &id);
    ZylAppManifest *known = g_hash_table_lookup(wam->manifests, id);
    if (known && known->is_system) return;             /* never a system app */
    if (wam->iface.get_instance(&wam->iface, id)) wam_close(wam, id);
    g_hash_table_remove(wam->manifests, id);
    g_hash_table_remove(wam->seals, id);
    if (strcmp(signal, "AppInstalled") == 0) {
        char *dir = zyl_container_dir(WAM_USER_DIR, id, NULL, false);
        ZylAppManifest *m = dir ? zyl_manifest_parse(dir) : NULL;
        ZylSeal *seal = m && strcmp(m->id, id) == 0 ? seal_app(m) : NULL;
        if (seal) {
            m->is_system = FALSE;
            g_hash_table_insert(wam->manifests, g_strdup(m->id), m);
            g_hash_table_replace(wam->seals, g_strdup(id), seal);
            g_message("WAM: installed %s", id);
        } else if (m) {
            zyl_manifest_free(m);
        }
        g_free(dir);
    } else if (strcmp(signal, "AppRemoved") == 0) {
        /* The store removed the container; anything WebKit wrote while closing goes too */
        char *web = zyl_container_dir(WAM_CONTAINERS, id, "webkit", false);
        if (web) zyl_remove_tree(web);
        g_free(web);
        g_message("WAM: removed %s", id);
    }
}

static void power_call(const char *method) {
    if (!g_event_bus) return;
    g_dbus_connection_call(g_event_bus, "org.zylos.PowerManager", "/org/zylos/PowerManager",
        "org.zylos.PowerManager", method, NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

/* The shell's part of the device's events: the screen going off locks (the
   lock screen goes up while it is dark, so nothing shows when it comes back);
   the keys are the user's navigation */
static void on_shell_event(ZylWam *wam, const char *service, const char *signal, GVariant *params) {
    if (strcmp(service, "power") == 0 && strcmp(signal, "StateChanged") == 0 &&
        g_variant_is_of_type(params, G_VARIANT_TYPE("(ii)"))) {
        gint32 was, now;
        g_variant_get(params, "(ii)", &was, &now);
        gboolean on = now == ZYL_POWER_STATE_ACTIVE || now == ZYL_POWER_STATE_DIM;
        if (on == wam->screen_on) return;
        wam->screen_on = on;
        wam_show(wam, on ? zyl_shell_screen_on(&wam->shell) : zyl_shell_lock(&wam->shell));
    } else if (strcmp(service, "input") == 0 && strcmp(signal, "HardwareKeyPressed") == 0 &&
               g_variant_is_of_type(params, G_VARIANT_TYPE("(i)"))) {
        gint32 key;
        g_variant_get(params, "(i)", &key);
        switch (key) {
        case ZYL_KEY_POWER: power_call(wam->screen_on ? "ScreenOff" : "ScreenOn"); break;
        case ZYL_KEY_HOME:  wam_go_home(wam); break;
        case ZYL_KEY_BACK:  wam_go_back(wam); break;
        case ZYL_KEY_MENU:  wam_switch_app(wam, 1); break;
        default: break;
        }
    }
}

static void on_service_event(const char *service, const char *signal,
                             GVariant *params, gpointer user_data) {
    ZylWam *wam = user_data;
    if (strcmp(service, "appstore") == 0) on_store_event(wam, signal, params);
    on_shell_event(wam, service, signal, params);
    const char *show = app_to_show(service, signal, params);
    if (show && !wam_open(wam, show))
        g_warning("WAM: %s.%s needs %s, which could not start", service, signal, show);
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, wam->instances);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        ZylAppInstance *inst = value;
        if (!inst->webview_widget) continue;
        char *script = zyl_bridge_service_event_script(inst->manifest, service, signal, params);
        if (!script) continue;
        webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(inst->webview_widget),
            script, -1, NULL, NULL, NULL, NULL, NULL);
        g_free(script);
    }
}

static void subscribe_service_events(ZylWam *wam) {
    GError *err = NULL;
    g_event_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!g_event_bus) {
        g_warning("WAM: no system bus for service events: %s", err->message);
        g_error_free(err);
        return;
    }
    zyl_bridge_subscribe_service_events(g_event_bus, on_service_event, wam);
    zyl_bridge_watch_revocations(g_event_bus);
}

/* The first screen: setup until it is done, then the lock screen. A device
   whose settings cannot be read starts locked, never open. */
static void on_setup_state(GObject *src, GAsyncResult *res, gpointer data) {
    ZylWam *wam = data;
    GError *err = NULL;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    gboolean done = TRUE;
    if (r) {
        GVariant *d = g_variant_get_child_value(r, 0);
        if (!g_variant_lookup(d, "oobe_completed", "b", &done)) done = FALSE;
        g_variant_unref(d);
        g_variant_unref(r);
    } else {
        g_warning("WAM: setup state unknown (%s) — starting locked", err ? err->message : "no reply");
        g_clear_error(&err);
    }
    wam_show(wam, zyl_shell_boot(&wam->shell, done));
}

static void read_setup_state(ZylWam *wam) {
    if (!g_event_bus) {
        wam_show(wam, zyl_shell_boot(&wam->shell, TRUE));
        return;
    }
    g_dbus_connection_call(g_event_bus, "org.zylos.Settings", "/org/zylos/Settings",
        "org.zylos.Settings", "Get", g_variant_new("(s)", "system"), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, on_setup_state, wam);
}

/* ════════════════════════════════════════════════════════════════
 *  GTK activate
 * ════════════════════════════════════════════════════════════════ */

static void on_activate(GApplication *app, gpointer user_data) {
    ZylWam *wam = user_data;

#ifndef ZYL_USE_WEBKIT2GTK
    /* 6.0: schemes belong to the one web context; storage is per session.
       GTK 가 디스플레이를 연 뒤여야 한다 — main() 에서 부르면 WebKit 이
       플랫폼 레이어 없이 전역 컨텍스트를 만들다가 죽는다 (QEMU 레인이 잡음) */
    register_app_scheme(webkit_web_context_get_default());
#endif

    /* The WAM is a service: it runs until stopped, with or without app
       windows. App windows are not the application's, so without this hold
       g_application_run returned right after activation and the WAM exited. */
    g_application_hold(app);

    /* OOM Killer 초기화 — 주기적 메모리 압박 모니터링 시작 */
    g_oom = zyl_oom_init(&wam->iface);

    zyl_manifest_scan_dir(wam->manifests, WAM_APP_DIR,  TRUE);
    zyl_manifest_scan_dir(wam->manifests, WAM_USER_DIR, FALSE);
    seal_user_apps(wam);

    subscribe_service_events(wam);
    read_setup_state(wam);

    g_message("WAM activated, %d apps registered",
              g_hash_table_size(wam->manifests));
}

/* ════════════════════════════════════════════════════════════════
 *  main
 * ════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    ZylWam wam = {0};
    g_wam = &wam;
    /* Locked until the settings say otherwise: nothing opens before boot */
    (void)zyl_shell_boot(&wam.shell, TRUE);
    wam.screen_on = TRUE;

    /* Hash tables */
    wam.manifests = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, zyl_manifest_free);
    wam.instances = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, NULL);
    wam.seals = g_hash_table_new_full(g_str_hash, g_str_equal,
                                      g_free, (GDestroyNotify)zyl_seal_free);

    /* Wire up the app interface */
    wam.iface.get_manifest     = iface_get_manifest;
    wam.iface.get_instance     = iface_get_instance;
    wam.iface.store_instance   = iface_store_instance;
    wam.iface.remove_instance  = iface_remove_instance;
    wam.iface.count_instances  = iface_count_instances;
    wam.iface.impl_data        = &wam;

    /* Wire up the WebKitGTK engine */
    wam.engine.create_webview = webkit_create_webview;
    wam.engine.load_uri       = webkit_load_uri;
    wam.engine.impl_data      = NULL;

    /* Initialize bridge handler registry (H5) and override defaults with WAM handlers */
    zyl_bridge_init();
    zyl_bridge_register_handler("app.close",  wam_handle_app_close,  &wam);
    zyl_bridge_register_handler("app.launch", wam_handle_app_launch, &wam);
    zyl_bridge_register_handler("intent.start",  wam_handle_intent_start,  &wam);
    zyl_bridge_register_handler("intent.chosen", wam_handle_intent_chosen, &wam);
    zyl_bridge_register_handler("unlock",            wam_handle_unlock,     &wam);
    zyl_bridge_register_handler("setup.done",        wam_handle_setup_done, &wam);
    zyl_bridge_register_handler("navigation.handled", wam_handle_navigation, &wam);
    zyl_bridge_register_handler("navigation.exit",   wam_handle_navigation, &wam);

    /* Start D-Bus service */
    wam.dbus_owner_id = zyl_dbus_service_start(dbus_methods, &wam);

    /* GTK application */
    wam.app = G_APPLICATION(gtk_application_new("org.zylos.wam",
        G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect(wam.app, "activate", G_CALLBACK(on_activate), &wam);

    int status = g_application_run(wam.app, argc, argv);

    /* Cleanup */
    zyl_oom_destroy(g_oom);
    g_oom = NULL;
    zyl_bridge_cleanup();
    g_bus_unown_name(wam.dbus_owner_id);
    g_hash_table_destroy(wam.manifests);
    g_hash_table_destroy(wam.seals);
    g_hash_table_destroy(wam.instances);
    g_object_unref(wam.app);

    return status;
}
