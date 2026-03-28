/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Adapter
 *
 * 역할: JS-Native 브릿지 — JS 스크립트 주입 및 네이티브 메시지 디스패치
 * 수행범위: bridge.js 로드/주입, WebKit user-content 연결, JS→C 메시지 라우팅
 * 의존방향: bridge.h → manifest.h
 * SOLID: OCP — 핸들러 콜백 등록으로 새 브릿지 메시지 추가 시 기존 코드 수정 불필요
 * ────────────────────────────────────────────────────────── */

#include "bridge.h"

#include <stdio.h>
#include <string.h>
#include <json-glib/json-glib.h>

/* ─── Load and inject the JS bridge ─── */
gboolean bpi_bridge_inject(const char      *bridge_js_path,
                           WebKitWebView   *webview,
                           BpiAppManifest  *manifest) {
    gchar *template = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!g_file_get_contents(bridge_js_path, &template, &length, &error)) {
        g_warning("Failed to load bridge script %s: %s",
                  bridge_js_path, error->message);
        g_error_free(error);
        return FALSE;
    }

    /* Substitute tokens: {{APP_ID}}, {{APP_NAME}}, {{APP_VERSION}} */
    gchar *s1 = g_strdup(template);
    gchar *s2, *s3;

    s2 = g_strjoinv(manifest->id,      g_strsplit(s1, "{{APP_ID}}", -1));
    g_free(s1);
    s3 = g_strjoinv(manifest->name,    g_strsplit(s2, "{{APP_NAME}}", -1));
    g_free(s2);
    s1 = g_strjoinv(manifest->version, g_strsplit(s3, "{{APP_VERSION}}", -1));
    g_free(s3);

    /* Inject via WebKit user-content manager */
    WebKitUserContentManager *ucm =
        webkit_web_view_get_user_content_manager(webview);
    WebKitUserScript *user_script = webkit_user_script_new(
        s1,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, user_script);
    webkit_user_script_unref(user_script);

    g_free(s1);
    g_free(template);
    return TRUE;
}

/* ─── Dispatch an incoming bridge message ─── */
void bpi_bridge_dispatch(BpiBridgeHandler  handler,
                         BpiAppManifest   *manifest,
                         const char       *msg_str,
                         gpointer          user_data) {
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, msg_str, -1, NULL)) {
        g_warning("Bridge: invalid JSON from %s", manifest->id);
        g_object_unref(parser);
        return;
    }

    JsonObject *msg = json_node_get_object(json_parser_get_root(parser));
    const char *type = json_object_get_string_member(msg, "type");

    g_message("Bridge message from %s: %s", manifest->id, type);

    if (handler) {
        handler(type, msg, manifest, user_data);
    }

    g_object_unref(parser);
}
