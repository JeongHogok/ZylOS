/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: WAM(Web App Manager) 메인 서비스 — 모듈 조립 및 WebKitGTK 구현
 * 수행범위: manifest, lifecycle, bridge, D-Bus 모듈 연결, ZylWebEngine/ZylAppInterface vtable 구현
 * 의존방향: wam.h, manifest.h, lifecycle.h, bridge.h, dbus_service.h, oom.h, intent.h
 * SOLID: SRP — 모듈 조립과 WebKitGTK 바인딩만 담당
 * ────────────────────────────────────────────────────────── */

#include "wam.h"
#include "oom/oom.h"
#include "intent/intent.h"

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

static void wam_handle_app_close(const char        *type,
                                 gpointer           msg_obj,
                                 ZylAppManifest    *manifest,
                                 ZylBridgeReplyCtx *reply_ctx,
                                 gpointer           user_data) {
    (void)type; (void)msg_obj; (void)reply_ctx;
    ZylWam *wam = user_data;
    if (manifest && manifest->id)
        zyl_lifecycle_close(&wam->iface, manifest->id);
}

static void wam_handle_app_launch(const char        *type,
                                  gpointer           msg_obj,
                                  ZylAppManifest    *manifest,
                                  ZylBridgeReplyCtx *reply_ctx,
                                  gpointer           user_data) {
    (void)type; (void)manifest;
    ZylWam *wam = user_data;
    JsonObject *obj = msg_obj;
    if (json_object_has_member(obj, "appId")) {
        const char *target = json_object_get_string_member(obj, "appId");
        ZylAppInstance *inst = zyl_lifecycle_launch(&wam->iface, &wam->engine, target);
        /* Acknowledge launch to JS if a callback was requested */
        if (reply_ctx && reply_ctx->callback_id >= 0) {
            zyl_bridge_respond(reply_ctx->webview, reply_ctx->callback_id,
                               inst ? "{\"ok\":true}" : "{\"error\":true,\"message\":\"launch failed\"}");
        }
    } else {
        /* Missing appId — resolve the promise with an error so it doesn't leak */
        if (reply_ctx && reply_ctx->callback_id >= 0) {
            zyl_bridge_reply_error(reply_ctx, "missing appId");
        }
    }
}

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

/* Launch the handler and hand it the intent once its page is there */
static void launch_with_intent(ZylWam *wam, const char *app_id, JsonObject *intent) {
    gboolean running = wam->iface.get_instance(&wam->iface, app_id) != NULL;
    ZylAppInstance *inst = zyl_lifecycle_launch(&wam->iface, &wam->engine, app_id);
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
#ifdef ZYL_USE_WEBKIT2GTK
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge");
    WebKitWebView *webview = WEBKIT_WEB_VIEW(
        webkit_web_view_new_with_user_content_manager(ucm));
#else
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge", NULL);
    WebKitWebView *webview = WEBKIT_WEB_VIEW(
        g_object_new(WEBKIT_TYPE_WEB_VIEW, "user-content-manager", ucm, NULL));
#endif
    webkit_web_view_set_settings(webview, settings);

    /* H11: TLS certificate verification — reject invalid certificates.
       6.0 moved the policy from the web context to the network session. */
#ifdef ZYL_USE_WEBKIT2GTK
    webkit_web_context_set_tls_errors_policy(webkit_web_context_get_default(),
        WEBKIT_TLS_ERRORS_POLICY_FAIL);
#else
    webkit_network_session_set_tls_errors_policy(webkit_network_session_get_default(),
        WEBKIT_TLS_ERRORS_POLICY_FAIL);
#endif

    /* Inject JS bridge from external file */
    zyl_bridge_inject(WAM_BRIDGE_JS, webview, manifest);

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
    ZylAppInstance *inst = zyl_lifecycle_launch(&wam->iface,
                                               &wam->engine, app_id);
    if (inst) {
        int count = zyl_lifecycle_get_running_count(&wam->iface);
        if (count > WAM_MAX_APPS_WARN) {
            g_warning("Memory pressure: %d apps running (threshold %d)",
                      count, WAM_MAX_APPS_WARN);
        }

        /* OOM: 앱 등록 + 포그라운드 설정 + 즉시 압박 체크 */
        ZylAppManifest *m = wam->iface.get_manifest(&wam->iface, app_id);
        bool is_sys = m ? m->is_system : false;
        /* Pass pid=0: WAM runs WebKitGTK in-process, so there's no child PID.
         * If zygote integration adds a real child PID, pass it here. */
        zyl_oom_on_app_launched(g_oom, app_id, is_sys, 0);
        zyl_oom_on_app_foreground(g_oom, app_id);
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
    zyl_lifecycle_close(&wam->iface, app_id);
    zyl_oom_on_app_closed(g_oom, app_id);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_suspend(GVariant *params, GDBusMethodInvocation *inv,
                         gpointer user_data) {
    ZylWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    zyl_lifecycle_suspend(&wam->iface, app_id);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_resume(GVariant *params, GDBusMethodInvocation *inv,
                        gpointer user_data) {
    ZylWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    zyl_lifecycle_resume(&wam->iface, app_id);
    /* OOM: resume된 앱을 포그라운드로 전환 */
    zyl_oom_on_app_foreground(g_oom, app_id);
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

static const ZylDbusMethodEntry dbus_methods[] = {
    { "Launch",      dbus_launch      },
    { "Close",       dbus_close       },
    { "Suspend",     dbus_suspend     },
    { "Resume",      dbus_resume      },
    { "ListApps",    dbus_list_apps   },
    { "GetInstalled", dbus_get_installed },
    { "ListRunning", dbus_list_running },
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

static void on_service_event(const char *service, const char *signal,
                             GVariant *params, gpointer user_data) {
    ZylWam *wam = user_data;
    const char *show = app_to_show(service, signal, params);
    if (show) {
        gboolean running = wam->iface.get_instance(&wam->iface, show) != NULL;
        if (zyl_lifecycle_launch(&wam->iface, &wam->engine, show)) {
            if (!running) zyl_oom_on_app_launched(g_oom, show, true, 0);
            zyl_oom_on_app_foreground(g_oom, show);
        } else {
            g_warning("WAM: %s.%s needs %s, which could not start", service, signal, show);
        }
    }
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

static GDBusConnection *g_event_bus;   /* held for the WAM's lifetime: the
                                          subscriptions die with the connection */

static void subscribe_service_events(ZylWam *wam) {
    GError *err = NULL;
    g_event_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!g_event_bus) {
        g_warning("WAM: no session bus for service events: %s", err->message);
        g_error_free(err);
        return;
    }
    zyl_bridge_subscribe_service_events(g_event_bus, on_service_event, wam);
}

/* ════════════════════════════════════════════════════════════════
 *  GTK activate
 * ════════════════════════════════════════════════════════════════ */

static void on_activate(GApplication *app, gpointer user_data) {
    ZylWam *wam = user_data;

    /* OOM Killer 초기화 — cgroup v2 + 주기적 모니터링 시작 */
    g_oom = zyl_oom_init(&wam->iface);

    zyl_manifest_scan_dir(wam->manifests, WAM_APP_DIR,  TRUE);
    zyl_manifest_scan_dir(wam->manifests, WAM_USER_DIR, FALSE);

    /* Auto-launch home screen */
    (void)zyl_lifecycle_launch(&wam->iface, &wam->engine, "com.zylos.home");
    zyl_oom_on_app_launched(g_oom, "com.zylos.home", true, 0);
    zyl_oom_on_app_foreground(g_oom, "com.zylos.home");

    subscribe_service_events(wam);

    g_message("WAM activated, %d apps registered",
              g_hash_table_size(wam->manifests));
}

/* ════════════════════════════════════════════════════════════════
 *  main
 * ════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    ZylWam wam = {0};
    g_wam = &wam;

    /* Hash tables */
    wam.manifests = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, zyl_manifest_free);
    wam.instances = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, NULL);

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
    g_hash_table_destroy(wam.instances);
    g_object_unref(wam.app);

    return status;
}
