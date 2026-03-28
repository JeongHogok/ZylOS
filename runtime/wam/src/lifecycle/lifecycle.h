/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 라이프사이클 인터페이스 정의 — 상태, 엔진, 앱 인스턴스 추상화
 * 수행범위: ZylAppState 열거형, ZylWebEngine/ZylAppInterface/ZylAppInstance 타입 선언
 * 의존방향: manifest.h
 * SOLID: DIP — 추상 엔진/앱 인터페이스로 구체 구현(WebKitGTK)에 비의존
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_LIFECYCLE_H
#define ZYL_WAM_LIFECYCLE_H

#include <glib.h>
#include <gtk/gtk.h>
#include "../manifest/manifest.h"

/* ─── App states ─── */
typedef enum {
    ZYL_APP_STATE_STOPPED,
    ZYL_APP_STATE_RUNNING,
    ZYL_APP_STATE_SUSPENDED,
} ZylAppState;

/* ─── Abstract web-engine vtable ───
 *
 * Implement this interface to swap WebKitGTK for another engine
 * (e.g. CEF, Servo, a test stub).
 */
typedef struct _ZylWebEngine ZylWebEngine;
struct _ZylWebEngine {
    /*
     * Create a new web view widget for the given manifest.
     * The implementation is responsible for setting up the user-content
     * manager, injecting the JS bridge, and registering the message
     * handler.  Returns a GtkWidget* that can be packed into a window.
     */
    GtkWidget *(*create_webview)(ZylWebEngine       *self,
                                 ZylAppManifest     *manifest,
                                 gpointer            instance_ctx);

    /* Load a URI in the previously created webview widget. */
    void (*load_uri)(ZylWebEngine *self,
                     GtkWidget    *webview_widget,
                     const char   *uri);

    /* Opaque data for the implementation. */
    gpointer impl_data;
};

/* ─── Abstract app-interface vtable ───
 *
 * Decouples lifecycle operations from the concrete WAM server type.
 */
typedef struct _ZylAppInterface ZylAppInterface;
struct _ZylAppInterface {
    /* Look up a manifest by app ID. Returns NULL when not found. */
    ZylAppManifest *(*get_manifest)(ZylAppInterface *self,
                                    const char      *app_id);

    /* Look up a running instance by app ID. Returns NULL when not running. */
    gpointer (*get_instance)(ZylAppInterface *self,
                             const char      *app_id);

    /* Store a newly created instance. */
    void (*store_instance)(ZylAppInterface *self,
                           const char      *app_id,
                           gpointer         instance);

    /* Remove a stored instance. */
    void (*remove_instance)(ZylAppInterface *self,
                            const char      *app_id);

    gpointer impl_data;
};

/* ─── Running app instance ─── */
typedef struct {
    ZylAppManifest *manifest;
    GtkWidget      *window;
    GtkWidget      *webview_widget;
    ZylAppState     state;
} ZylAppInstance;

/*
 * Launch (or re-activate) an app.
 * Returns the instance or NULL on failure.
 */
ZylAppInstance *zyl_lifecycle_launch(ZylAppInterface *iface,
                                    ZylWebEngine    *engine,
                                    const char      *app_id);

/* Suspend a running app (saves resources). */
void zyl_lifecycle_suspend(ZylAppInterface *iface,
                           const char      *app_id);

/* Close and destroy an app instance. */
void zyl_lifecycle_close(ZylAppInterface *iface,
                         const char      *app_id);

#endif /* ZYL_WAM_LIFECYCLE_H */
