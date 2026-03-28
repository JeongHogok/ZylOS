/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: WAM(Web App Manager) 메인 서비스 — 모듈 조립 및 WebKitGTK 구현
 * 수행범위: manifest, lifecycle, bridge, D-Bus 모듈 연결, BpiWebEngine/BpiAppInterface vtable 구현
 * 의존방향: wam.h, manifest.h, lifecycle.h, bridge.h, dbus_service.h
 * SOLID: SRP — 모듈 조립과 WebKitGTK 바인딩만 담당
 * ────────────────────────────────────────────────────────── */

#include "wam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webkit/webkit.h>

static BpiWam *g_wam = NULL;

/* ════════════════════════════════════════════════════════════════
 *  BpiAppInterface implementation (backed by GHashTable lookups)
 * ════════════════════════════════════════════════════════════════ */

static BpiAppManifest *iface_get_manifest(BpiAppInterface *self,
                                          const char      *app_id) {
    BpiWam *wam = self->impl_data;
    return g_hash_table_lookup(wam->manifests, app_id);
}

static gpointer iface_get_instance(BpiAppInterface *self,
                                   const char      *app_id) {
    BpiWam *wam = self->impl_data;
    return g_hash_table_lookup(wam->instances, app_id);
}

static void iface_store_instance(BpiAppInterface *self,
                                 const char      *app_id,
                                 gpointer         instance) {
    BpiWam *wam = self->impl_data;
    g_hash_table_insert(wam->instances, g_strdup(app_id), instance);
}

static void iface_remove_instance(BpiAppInterface *self,
                                  const char      *app_id) {
    BpiWam *wam = self->impl_data;
    g_hash_table_remove(wam->instances, app_id);
}

/* ════════════════════════════════════════════════════════════════
 *  Bridge message handler
 * ════════════════════════════════════════════════════════════════ */

static void on_bridge_message_cb(const char     *type,
                                 gpointer        msg_obj,
                                 BpiAppManifest *manifest,
                                 gpointer        user_data) {
    BpiWam *wam = user_data;
    (void)msg_obj;

    if (g_strcmp0(type, "app.close") == 0) {
        bpi_lifecycle_close(&wam->iface, manifest->id);
    } else if (g_strcmp0(type, "app.launch") == 0) {
        /* msg_obj is a JsonObject*; extract appId */
        JsonObject *obj = msg_obj;
        const char *target = json_object_get_string_member(obj, "appId");
        bpi_lifecycle_launch(&wam->iface, &wam->engine, target);
    } else if (g_strcmp0(type, "notification.create") == 0) {
        JsonObject *obj = msg_obj;
        const char *title = json_object_get_string_member(obj, "title");
        const char *body  = json_object_get_string_member(obj, "body");
        g_message("Notification: %s - %s", title, body);
    }
}

/* ════════════════════════════════════════════════════════════════
 *  WebKit bridge message signal handler
 * ════════════════════════════════════════════════════════════════ */

static void on_webkit_bridge_message(WebKitUserContentManager *manager,
                                     WebKitJavascriptResult   *result,
                                     gpointer                  user_data) {
    BpiAppInstance *instance = user_data;
    (void)manager;

    JSCValue *value = webkit_javascript_result_get_js_value(result);
    char *msg_str = jsc_value_to_string(value);

    bpi_bridge_dispatch(on_bridge_message_cb,
                        instance->manifest,
                        msg_str,
                        g_wam);

    g_free(msg_str);
}

/* ════════════════════════════════════════════════════════════════
 *  BpiWebEngine implementation (WebKitGTK back-end)
 * ════════════════════════════════════════════════════════════════ */

static GtkWidget *webkit_create_webview(BpiWebEngine   *self,
                                        BpiAppManifest *manifest,
                                        gpointer        instance_ctx) {
    (void)self;

    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);

    WebKitUserContentManager *ucm = webkit_user_content_manager_new();
    g_signal_connect(ucm, "script-message-received::bridge",
                     G_CALLBACK(on_webkit_bridge_message), instance_ctx);
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge");

    WebKitWebView *webview = WEBKIT_WEB_VIEW(
        webkit_web_view_new_with_user_content_manager(ucm));
    webkit_web_view_set_settings(webview, settings);

    /* Inject JS bridge from external file */
    bpi_bridge_inject(WAM_BRIDGE_JS, webview, manifest);

    g_object_unref(settings);
    return GTK_WIDGET(webview);
}

static void webkit_load_uri(BpiWebEngine *self,
                            GtkWidget    *webview_widget,
                            const char   *uri) {
    (void)self;
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview_widget), uri);
}

/* ════════════════════════════════════════════════════════════════
 *  D-Bus method implementations (dispatch table entries)
 * ════════════════════════════════════════════════════════════════ */

static void dbus_launch(GVariant *params, GDBusMethodInvocation *inv,
                        gpointer user_data) {
    BpiWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    BpiAppInstance *inst = bpi_lifecycle_launch(&wam->iface,
                                               &wam->engine, app_id);
    g_dbus_method_invocation_return_value(inv,
        g_variant_new("(b)", inst != NULL));
}

static void dbus_close(GVariant *params, GDBusMethodInvocation *inv,
                       gpointer user_data) {
    BpiWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    bpi_lifecycle_close(&wam->iface, app_id);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_suspend(GVariant *params, GDBusMethodInvocation *inv,
                         gpointer user_data) {
    BpiWam *wam = user_data;
    const gchar *app_id;
    g_variant_get(params, "(&s)", &app_id);
    bpi_lifecycle_suspend(&wam->iface, app_id);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void dbus_list_apps(GVariant *params, GDBusMethodInvocation *inv,
                           gpointer user_data) {
    BpiWam *wam = user_data;
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
    BpiWam *wam = user_data;
    (void)params;
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, wam->instances);
    while (g_hash_table_iter_next(&iter, &key, NULL))
        g_variant_builder_add(&builder, "s", (const char *)key);
    g_dbus_method_invocation_return_value(inv,
        g_variant_new("(as)", &builder));
}

static const BpiDbusMethodEntry dbus_methods[] = {
    { "Launch",      dbus_launch      },
    { "Close",       dbus_close       },
    { "Suspend",     dbus_suspend     },
    { "ListApps",    dbus_list_apps   },
    { "ListRunning", dbus_list_running },
    { NULL, NULL }
};

/* ════════════════════════════════════════════════════════════════
 *  GTK activate
 * ════════════════════════════════════════════════════════════════ */

static void on_activate(GApplication *app, gpointer user_data) {
    BpiWam *wam = user_data;

    bpi_manifest_scan_dir(wam->manifests, WAM_APP_DIR,  TRUE);
    bpi_manifest_scan_dir(wam->manifests, WAM_USER_DIR, FALSE);

    /* Auto-launch home screen */
    bpi_lifecycle_launch(&wam->iface, &wam->engine, "com.bpios.home");

    g_message("WAM activated, %d apps registered",
              g_hash_table_size(wam->manifests));
}

/* ════════════════════════════════════════════════════════════════
 *  main
 * ════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    BpiWam wam = {0};
    g_wam = &wam;

    /* Hash tables */
    wam.manifests = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, bpi_manifest_free);
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

    /* Start D-Bus service */
    wam.dbus_owner_id = bpi_dbus_service_start(dbus_methods, &wam);

    /* GTK application */
    wam.app = G_APPLICATION(gtk_application_new("org.bpios.wam",
        G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect(wam.app, "activate", G_CALLBACK(on_activate), &wam);

    int status = g_application_run(wam.app, argc, argv);

    /* Cleanup */
    g_bus_unown_name(wam.dbus_owner_id);
    g_hash_table_destroy(wam.manifests);
    g_hash_table_destroy(wam.instances);
    g_object_unref(wam.app);

    return status;
}
