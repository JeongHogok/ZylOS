/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: WAM(Web App Manager) 메인 서비스 — 모듈 조립 및 WebKitGTK 구현
 * 수행범위: manifest, lifecycle, bridge, D-Bus 모듈 연결, ZylWebEngine/ZylAppInterface vtable 구현
 * 의존방향: wam.h, manifest.h, lifecycle.h, bridge.h, dbus_service.h, oom.h
 * SOLID: SRP — 모듈 조립과 WebKitGTK 바인딩만 담당
 * ────────────────────────────────────────────────────────── */

#include "wam.h"
#include "oom/oom.h"

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

/* ════════════════════════════════════════════════════════════════
 *  Bridge handler registry is managed by bridge.c (H5).
 *  WAM-specific handlers for app lifecycle are registered below.
 * ════════════════════════════════════════════════════════════════ */

static void wam_handle_app_close(const char     *type,
                                 gpointer        msg_obj,
                                 ZylAppManifest *manifest,
                                 gpointer        user_data) {
    (void)type;
    (void)msg_obj;
    ZylWam *wam = user_data;
    if (manifest && manifest->id)
        zyl_lifecycle_close(&wam->iface, manifest->id);
}

static void wam_handle_app_launch(const char     *type,
                                  gpointer        msg_obj,
                                  ZylAppManifest *manifest,
                                  gpointer        user_data) {
    (void)type;
    (void)manifest;
    ZylWam *wam = user_data;
    JsonObject *obj = msg_obj;
    if (json_object_has_member(obj, "appId")) {
        const char *target = json_object_get_string_member(obj, "appId");
        zyl_lifecycle_launch(&wam->iface, &wam->engine, target);
    }
}

/* ════════════════════════════════════════════════════════════════
 *  WebKit bridge message signal handler
 * ════════════════════════════════════════════════════════════════ */

static void on_webkit_bridge_message(WebKitUserContentManager *manager,
                                     WebKitJavascriptResult   *result,
                                     gpointer                  user_data) {
    ZylAppInstance *instance = user_data;
    (void)manager;

    JSCValue *value = webkit_javascript_result_get_js_value(result);
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
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge");

    WebKitWebView *webview = WEBKIT_WEB_VIEW(
        webkit_web_view_new_with_user_content_manager(ucm));
    webkit_web_view_set_settings(webview, settings);

    /* H11: TLS certificate verification — reject invalid certificates */
    WebKitWebContext *web_ctx = webkit_web_context_get_default();
    webkit_web_context_set_tls_errors_policy(web_ctx,
        WEBKIT_TLS_ERRORS_POLICY_FAIL);

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
        zyl_oom_on_app_launched(g_oom, app_id, is_sys);
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
    { "ListRunning", dbus_list_running },
    { NULL, NULL }
};

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
    zyl_lifecycle_launch(&wam->iface, &wam->engine, "com.zylos.home");
    zyl_oom_on_app_launched(g_oom, "com.zylos.home", true);
    zyl_oom_on_app_foreground(g_oom, "com.zylos.home");

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
    wam.iface.get_manifest    = iface_get_manifest;
    wam.iface.get_instance    = iface_get_instance;
    wam.iface.store_instance  = iface_store_instance;
    wam.iface.remove_instance = iface_remove_instance;
    wam.iface.impl_data       = &wam;

    /* Wire up the WebKitGTK engine */
    wam.engine.create_webview = webkit_create_webview;
    wam.engine.load_uri       = webkit_load_uri;
    wam.engine.impl_data      = NULL;

    /* Initialize bridge handler registry (H5) and override defaults with WAM handlers */
    zyl_bridge_init();
    zyl_bridge_register_handler("app.close",  wam_handle_app_close,  &wam);
    zyl_bridge_register_handler("app.launch", wam_handle_app_launch, &wam);

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
