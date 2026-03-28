/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - UseCase
 *
 * 역할: 앱 라이프사이클 관리 — 실행, 일시정지, 재개, 종료
 * 수행범위: 앱 launch/suspend/resume/close 오퍼레이션 구현
 * 의존방향: lifecycle.h → manifest.h
 * SOLID: OCP — 추상 엔진/앱 인터페이스로 WebKitGTK 교체 가능
 * ────────────────────────────────────────────────────────── */

#include "lifecycle.h"

#include <stdio.h>
#include <string.h>

/* ─── Launch (or re-activate) an app ─── */
ZylAppInstance *zyl_lifecycle_launch(ZylAppInterface *iface,
                                    ZylWebEngine    *engine,
                                    const char      *app_id) {
    /* If already running, just present it */
    ZylAppInstance *existing = iface->get_instance(iface, app_id);
    if (existing) {
        gtk_window_present(GTK_WINDOW(existing->window));
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
    gtk_window_fullscreen(GTK_WINDOW(instance->window));

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

    instance->state = ZYL_APP_STATE_SUSPENDED;
    g_message("Suspended app: %s", app_id);
}

/* ─── Close ─── */
void zyl_lifecycle_close(ZylAppInterface *iface,
                         const char      *app_id) {
    ZylAppInstance *instance = iface->get_instance(iface, app_id);
    if (!instance) return;

    gtk_window_close(GTK_WINDOW(instance->window));
    iface->remove_instance(iface, app_id);
    g_free(instance);
    g_message("Closed app: %s", app_id);
}
