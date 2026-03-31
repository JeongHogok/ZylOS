/* ----------------------------------------------------------
 * [Clean Architecture] Application Layer - UseCase
 *
 * 역할: 앱 라이프사이클 관리 -- 실행, 일시정지, 재개, 종료
 * 수행범위: 앱 launch/suspend/resume/close 오퍼레이션,
 *           H20 GTK 윈도우 관리 (풀스크린 강제, 크기 제약)
 * 의존방향: lifecycle.h → manifest.h, gdk/gdk.h
 * SOLID: OCP -- 추상 엔진/앱 인터페이스로 WebKitGTK 교체 가능
 * ---------------------------------------------------------- */

#include "lifecycle.h"

#include <stdio.h>
#include <string.h>
#ifdef ZYL_USE_WEBKIT2GTK
#include <webkit2/webkit2.h>
#else
#include <webkit/webkit.h>
#endif
#include <gdk/gdk.h>

/* --- H20: Statusbar height in pixels (reserved at top of screen) --- */
#define ZYL_STATUSBAR_HEIGHT 36

/* ─── Helper: inject JS into an app's webview ─── */
static void inject_js(ZylAppInstance *instance, const char *script) {
    webkit_web_view_evaluate_javascript(
        WEBKIT_WEB_VIEW(instance->webview_widget),
        script, -1, NULL, NULL, NULL, NULL, NULL);
}

/* ─── Launch (or re-activate) an app ─── */
ZylAppInstance *zyl_lifecycle_launch(ZylAppInterface *iface,
                                    ZylWebEngine    *engine,
                                    const char      *app_id) {
    /* If already running but suspended, resume it */
    ZylAppInstance *existing = iface->get_instance(iface, app_id);
    if (existing) {
        if (existing->state == ZYL_APP_STATE_SUSPENDED) {
            zyl_lifecycle_resume(iface, app_id);
        } else {
            gtk_window_present(GTK_WINDOW(existing->window));
        }
        existing->state = ZYL_APP_STATE_RUNNING;
        return existing;
    }

    ZylAppManifest *manifest = iface->get_manifest(iface, app_id);
    if (!manifest) {
        g_warning("App not found: %s", app_id);
        return NULL;
    }

    ZylAppInstance *instance = g_new0(ZylAppInstance, 1);
    instance->manifest = manifest;
    instance->state    = ZYL_APP_STATE_RUNNING;

    /* GTK window (becomes a Wayland toplevel) */
    instance->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(instance->window), manifest->name);
    gtk_window_set_decorated(GTK_WINDOW(instance->window), FALSE);

    /* H20: Force fullscreen and apply size constraints.
     * Query the monitor geometry to get screen dimensions, then
     * reserve ZYL_STATUSBAR_HEIGHT at the top for the status bar.
     * Set min=max size to prevent user resizing. */
    gtk_window_fullscreen(GTK_WINDOW(instance->window));

    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        GListModel *monitors = gdk_display_get_monitors(display);
        if (monitors && g_list_model_get_n_items(monitors) > 0) {
            GdkMonitor *monitor = g_list_model_get_item(monitors, 0);
            if (monitor) {
                GdkRectangle geom;
                gdk_monitor_get_geometry(monitor, &geom);
                int app_width  = geom.width;
                int app_height = geom.height - ZYL_STATUSBAR_HEIGHT;

                /* Prevent resize: set both default and min size */
                gtk_window_set_default_size(GTK_WINDOW(instance->window),
                                            app_width, app_height);
                gtk_widget_set_size_request(instance->window,
                                            app_width, app_height);

                g_message("H20: Window sized %dx%d (screen %dx%d minus "
                          "statusbar %d)",
                          app_width, app_height,
                          geom.width, geom.height,
                          ZYL_STATUSBAR_HEIGHT);
                g_object_unref(monitor);
            }
        }
    }

    /* Prevent resizability */
    gtk_window_set_resizable(GTK_WINDOW(instance->window), FALSE);

    /* Create webview via the abstract engine */
    instance->webview_widget = engine->create_webview(engine, manifest,
                                                      instance);
    gtk_window_set_child(GTK_WINDOW(instance->window),
                         instance->webview_widget);

    /* Build entry URI and load */
    char entry_uri[1024];
    snprintf(entry_uri, sizeof(entry_uri), "file://%s/%s",
             manifest->base_path, manifest->entry);
    engine->load_uri(engine, instance->webview_widget, entry_uri);

    gtk_window_present(GTK_WINDOW(instance->window));

    iface->store_instance(iface, app_id, instance);
    g_message("Launched app: %s (%s)", manifest->name, app_id);

    return instance;
}

/* ─── Suspend ─── */
void zyl_lifecycle_suspend(ZylAppInterface *iface,
                           const char      *app_id) {
    ZylAppInstance *instance = iface->get_instance(iface, app_id);
    if (!instance || instance->state == ZYL_APP_STATE_SUSPENDED) return;

    /* Notify the web app so it can save state / pause work */
    inject_js(instance,
              "document.dispatchEvent(new Event('zyl:pause'))");

    /* Hide the window to free compositor resources */
    gtk_widget_set_visible(instance->window, FALSE);

    instance->state = ZYL_APP_STATE_SUSPENDED;
    g_message("Suspended app: %s", app_id);
}

/* ─── Close with graceful cleanup ─── */

/* Timeout callback: actually tears down the window after JS cleanup. */
static gboolean close_after_cleanup(gpointer data) {
    gpointer *ctx = data;
    ZylAppInterface *iface    = ctx[0];
    ZylAppInstance  *instance = ctx[1];
    char            *app_id   = ctx[2]; /* strdup'd — we own it */

    /* Remove from registry first (uses app_id before we free it) */
    iface->remove_instance(iface, app_id);

    /* Now destroy the window */
    gtk_window_close(GTK_WINDOW(instance->window));

    g_message("Closed app: %s", app_id);

    /* Free instance and the ctx array */
    g_free(instance);
    g_free(app_id);
    g_free(ctx);
    return G_SOURCE_REMOVE;
}

void zyl_lifecycle_close(ZylAppInterface *iface,
                         const char      *app_id) {
    ZylAppInstance *instance = iface->get_instance(iface, app_id);
    if (!instance) return;

    /* Give the web app a chance to run final cleanup */
    inject_js(instance,
              "document.dispatchEvent(new Event('zyl:destroy'))");

    /* Schedule the actual teardown after 100 ms so JS can finish */
    gpointer *ctx = g_new(gpointer, 3);
    ctx[0] = iface;
    ctx[1] = instance;
    ctx[2] = g_strdup(app_id);
    g_timeout_add(100, close_after_cleanup, ctx);
}

/* ─── Resume ─── */
void zyl_lifecycle_resume(ZylAppInterface *iface,
                          const char      *app_id) {
    ZylAppInstance *instance = iface->get_instance(iface, app_id);
    if (!instance || instance->state != ZYL_APP_STATE_SUSPENDED) return;

    /* Make the window visible again and present it */
    gtk_widget_set_visible(instance->window, TRUE);
    gtk_window_present(GTK_WINDOW(instance->window));

    /* Notify the web app */
    inject_js(instance,
              "document.dispatchEvent(new Event('zyl:resume'))");

    instance->state = ZYL_APP_STATE_RUNNING;
    g_message("Resumed app: %s", app_id);
}

/* ─── Running count ─── */
int zyl_lifecycle_get_running_count(ZylAppInterface *iface) {
    /*
     * Delegate to the vtable count_instances() so lifecycle.c does not need
     * to know the concrete ZylWam struct layout (DIP compliance).
     */
    if (!iface) return -1;
    if (iface->count_instances)
        return iface->count_instances(iface);
    return -1;
}
