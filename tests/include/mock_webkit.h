/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test Stub
 *
 * 역할: WebKitGTK 최소 mock 헤더
 * 수행범위: bridge.c/unit test가 요구하는 타입/함수 선언만 제공
 * 의존방향: GLib/GObject
 * SOLID: SRP — 테스트용 WebKit shim만 담당
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_TEST_MOCK_WEBKIT_H
#define ZYL_TEST_MOCK_WEBKIT_H

#include <glib.h>
#include <gio/gio.h>

typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitUserContentManager WebKitUserContentManager;
typedef struct _WebKitUserScript WebKitUserScript;
typedef struct _WebKitJavascriptResult WebKitJavascriptResult;
typedef struct _JSCValue JSCValue;

struct _WebKitWebView {
    gchar *last_script;
};

/* The mock view lives on the test's stack, not a GObject: nothing to hold
   (bridge.c holds a real view until a late answer has gone to it) */
#define ZYL_BRIDGE_VIEW_REF(v)   (v)
#define ZYL_BRIDGE_VIEW_UNREF(v) ((void)(v))
struct _WebKitUserContentManager { int unused; };
struct _WebKitUserScript { int unused; };
struct _WebKitJavascriptResult { int unused; };
struct _JSCValue { int unused; };

typedef enum {
    WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES = 0,
    WEBKIT_USER_CONTENT_INJECT_TOP_FRAME = 1,
} WebKitUserContentInjectedFrames;

typedef enum {
    WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START = 0,
    WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END = 1,
} WebKitUserScriptInjectionTime;

static inline WebKitUserContentManager *webkit_web_view_get_user_content_manager(WebKitWebView *webview)
{
    (void)webview;
    static WebKitUserContentManager manager;
    return &manager;
}

static gchar *mock_user_script_source;   /* the last script bridge.c built */
static WebKitUserContentInjectedFrames mock_user_script_frames;

static inline WebKitUserScript *webkit_user_script_new(const gchar *source,
                                                       WebKitUserContentInjectedFrames frames,
                                                       WebKitUserScriptInjectionTime time,
                                                       gpointer whitelist,
                                                       gpointer blacklist)
{
    (void)time; (void)whitelist; (void)blacklist;
    g_free(mock_user_script_source);
    mock_user_script_source = g_strdup(source);
    mock_user_script_frames = frames;
    static WebKitUserScript script;
    return &script;
}

static inline void webkit_user_content_manager_add_script(WebKitUserContentManager *manager,
                                                          WebKitUserScript *script)
{
    (void)manager; (void)script;
}

static inline void webkit_user_script_unref(WebKitUserScript *script)
{
    (void)script;
}

static inline void webkit_web_view_evaluate_javascript(WebKitWebView *webview,
                                                       const gchar *script,
                                                       gssize length,
                                                       const gchar *world_name,
                                                       GCancellable *cancellable,
                                                       GAsyncReadyCallback callback,
                                                       gpointer user_data,
                                                       gpointer unused)
{
    (void)length; (void)world_name; (void)cancellable;
    (void)callback; (void)user_data; (void)unused;
    if (!webview)
        return;
    g_free(webview->last_script);
    webview->last_script = g_strdup(script ? script : "");
}

#define WEBKIT_WEB_VIEW(ptr) ((WebKitWebView *)(ptr))

#endif
