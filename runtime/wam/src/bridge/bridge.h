/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: JS-Native 브릿지 인터페이스 정의 — 메시지 핸들러 콜백 및 주입 함수
 * 수행범위: BpiBridgeHandler 타입, bpi_bridge_inject/register 함수 선언
 * 의존방향: manifest.h
 * SOLID: DIP — 브릿지 구현이 아닌 콜백 인터페이스에 의존
 * ────────────────────────────────────────────────────────── */

#ifndef BPI_WAM_BRIDGE_H
#define BPI_WAM_BRIDGE_H

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
typedef void (*BpiBridgeHandler)(const char       *type,
                                 gpointer          msg_obj,
                                 BpiAppManifest   *manifest,
                                 gpointer          user_data);

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
gboolean bpi_bridge_inject(const char      *bridge_js_path,
                           WebKitWebView   *webview,
                           BpiAppManifest  *manifest);

/*
 * Process an incoming bridge message JSON string.
 *
 * handler   - callback to invoke
 * manifest  - manifest of the sending app
 * msg_str   - raw JSON string from JS
 * user_data - forwarded to handler
 */
void bpi_bridge_dispatch(BpiBridgeHandler  handler,
                         BpiAppManifest   *manifest,
                         const char       *msg_str,
                         gpointer          user_data);

#endif /* BPI_WAM_BRIDGE_H */
