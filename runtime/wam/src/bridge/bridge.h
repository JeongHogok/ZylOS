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
#include <webkit/webkit.h>
#include "../manifest/manifest.h"

/*
 * Bridge message handler callback.
 *
 * type     - message type string (e.g. "app.close")
 * msg_obj  - full JSON message as a JsonObject (borrowed, do not free)
 * manifest - manifest of the sending app
 * user_data - opaque pointer passed at registration time
 */
typedef void (*ZylBridgeHandler)(const char       *type,
                                 gpointer          msg_obj,
                                 ZylAppManifest   *manifest,
                                 gpointer          user_data);

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
 *
 * type    - message type string (e.g. "battery.getLevel")
 * handler - callback to invoke
 * data    - user data forwarded to handler
 *
 * Returns 0 on success, -1 on error.
 */
int zyl_bridge_register_handler(const char       *type,
                                ZylBridgeHandler  handler,
                                gpointer          data);

/*
 * Unregister a previously registered handler for the given type.
 *
 * Returns 0 on success, -1 if type was not registered.
 */
int zyl_bridge_unregister_handler(const char *type);

/*
 * Load the bridge.js template from disk, substitute app-specific
 * tokens, and inject it into the given WebKitWebView's user-content
 * manager.
 *
 * bridge_js_path - absolute path to bridge.js
 * webview        - the target WebKitWebView
 * manifest       - the app manifest (for template substitution)
 *
 * Returns TRUE on success.
 */
gboolean zyl_bridge_inject(const char      *bridge_js_path,
                           WebKitWebView   *webview,
                           ZylAppManifest  *manifest);

/*
 * Process an incoming bridge message JSON string.
 * Routes to registered handlers via the type registry.
 * If no handler is found, sends an error response (H13).
 *
 * webview  - the WebKitWebView to send responses to
 * manifest - manifest of the sending app
 * msg_str  - raw JSON string from JS
 */
void zyl_bridge_dispatch(WebKitWebView    *webview,
                         ZylAppManifest   *manifest,
                         const char       *msg_str);

/*
 * Send a response back to JavaScript.
 * Injects: window._zylCb_{callback_id}(json_data)
 *
 * webview     - the target WebKitWebView
 * callback_id - the JS callback ID to invoke
 * json_data   - JSON string to pass as argument
 *
 * Returns 0 on success, -1 on error.
 */
int zyl_bridge_respond(WebKitWebView *webview,
                       int            callback_id,
                       const char    *json_data);

#endif /* ZYL_WAM_BRIDGE_H */
