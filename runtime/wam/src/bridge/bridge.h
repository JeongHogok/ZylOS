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
    int            callback_id; /* JS _cbId; -1 = no response expected */
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
 * Unregister a previously registered handler.
 * Returns 0 on success, -1 if type was not registered.
 */
int zyl_bridge_unregister_handler(const char *type);

/*
 * Load bridge.js, substitute app-specific tokens, and inject into the webview.
 * Returns TRUE on success.
 */
gboolean zyl_bridge_inject(const char      *bridge_js_path,
                           WebKitWebView   *webview,
                           ZylAppManifest  *manifest);

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

#endif /* ZYL_WAM_BRIDGE_H */
