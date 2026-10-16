/* ----------------------------------------------------------
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: JS-Native 브릿지 인터페이스 -- 메시지 핸들러 레지스트리 및 비동기 응답
 * 수행범위: ZylBridgeHandler 타입, 핸들러 등록/해제, JS 주입, 디스패치, 응답
 * 의존방향: manifest.h
 * SOLID: DIP -- 브릿지 구현이 아닌 콜백 인터페이스에 의존, OCP -- 핸들러 등록으로 확장
 * ---------------------------------------------------------- */

#ifndef ZYL_WAM_BRIDGE_H
#define ZYL_WAM_BRIDGE_H

#include <glib.h>
#ifdef ZYL_USE_WEBKIT2GTK
#include <webkit2/webkit2.h>
#else
#include <webkit/webkit.h>
#endif
#include "../manifest/manifest.h"

/*
 * Reply context passed to every handler so it can send an async response.
 * callback_id == -1 means the caller did not request a response (fire-and-forget).
 * When callback_id >= 0 the handler MUST eventually call zyl_bridge_respond()
 * or zyl_bridge_reply_error() — otherwise the JS Promise leaks forever.
 */
typedef struct {
    WebKitWebView *webview;     /* target view for the response */
    int            callback_id; /* message callbackId; -1 = no response expected */
} ZylBridgeReplyCtx;

/*
 * Bridge message handler callback.
 *
 * type      - message type string (e.g. "app.close")
 * msg_obj   - full JSON message as a JsonObject (borrowed, do not free)
 * manifest  - manifest of the sending app
 * reply_ctx - response context; use zyl_bridge_respond() / zyl_bridge_reply_error()
 * user_data - opaque pointer passed at registration time
 */
typedef void (*ZylBridgeHandler)(const char        *type,
                                 gpointer           msg_obj,
                                 ZylAppManifest    *manifest,
                                 ZylBridgeReplyCtx *reply_ctx,
                                 gpointer           user_data);

/*
 * Initialize the bridge handler registry.
 * Must be called once at startup before register/dispatch.
 */
void zyl_bridge_init(void);

/*
 * Tear down the bridge handler registry and free all resources.
 */
void zyl_bridge_cleanup(void);

/*
 * Register a message type handler.
 * Returns 0 on success, -1 on error.
 */
int zyl_bridge_register_handler(const char       *type,
                                ZylBridgeHandler  handler,
                                gpointer          data);

/*
 * A web view's bridge token: the WAM answers only messages that carry it, and
 * only the bridge script in the app's own top document knows it. A page the
 * app frames, or one it navigates to, can reach the message handler (WebKit
 * gives it to every frame) but is not the app.
 */
#define ZYL_BRIDGE_TOKEN_LEN 32

/* A fresh token from the kernel's random source; FALSE (out empty) if none */
gboolean zyl_bridge_new_token(char out[ZYL_BRIDGE_TOKEN_LEN + 1]);

/* The message past the token, or NULL when it does not start with it (or the
   view has no token) */
const char *zyl_bridge_untoken(const char *msg, const char *token);

/*
 * Load bridge.js, substitute app-specific tokens, and inject it into the
 * webview's top frame. Returns TRUE on success.
 */
gboolean zyl_bridge_inject(const char      *bridge_js_path,
                           WebKitWebView   *webview,
                           ZylAppManifest  *manifest,
                           const char      *token);

/*
 * Process an incoming bridge message JSON string.
 * Routes to registered handlers via the type registry.
 */
void zyl_bridge_dispatch(WebKitWebView    *webview,
                         ZylAppManifest   *manifest,
                         const char       *msg_str);

/*
 * Send a JSON response back to JavaScript for a specific callback.
 * Injects: if(window._zylCb_N){window._zylCb_N(json_data);delete window._zylCb_N;}
 *
 * Returns 0 on success, -1 on error.
 */
int zyl_bridge_respond(WebKitWebView *webview,
                       int            callback_id,
                       const char    *json_data);

/*
 * Convenience: send a structured error response back to JS.
 * Equivalent to zyl_bridge_respond with {"error":true,"message":"..."}.
 */
void zyl_bridge_reply_error(const ZylBridgeReplyCtx *ctx,
                             const char              *message);

/*
 * Service events: D-Bus signals of the routed OS services, delivered to apps.
 * The WAM subscribes once; for each signal it asks, per running app, for the
 * script that delivers it — NULL when that app may not call the service.
 */
typedef void (*ZylBridgeEventFn)(const char *service, const char *signal,
                                 GVariant *params, gpointer user_data);

/* Subscribes to every routed service's signals, each only from that
 * service's own bus name, so no other process can fake them. */
void zyl_bridge_subscribe_service_events(GDBusConnection *bus,
                                         ZylBridgeEventFn fn, gpointer user_data);

typedef void (*ZylBridgeAppFn)(const char *app_id, gpointer user_data);

/* Follow the user's permission revocations (the settings service's
 * "app_permissions" category): read them now, and again whenever that
 * category changes. Without this the device ignores a permission the user
 * switched off in Settings — only the emulator applied it. changed is called
 * for each app whose list changed (what its documents were given is stale). */
void zyl_bridge_watch_revocations(GDBusConnection *conn, ZylBridgeAppFn changed, gpointer data);

/* Does the app hold the permission now: system apps hold all, a user-installed
 * app what it declared, minus the reserved and what the user switched off */
gboolean zyl_bridge_app_holds(const ZylAppManifest *m, const char *perm);

/* window.postMessage of {type:"service.event", service, event, data}, or NULL */
char *zyl_bridge_service_event_script(const ZylAppManifest *manifest,
                                      const char *service, const char *signal,
                                      GVariant *params);

#endif /* ZYL_WAM_BRIDGE_H */
