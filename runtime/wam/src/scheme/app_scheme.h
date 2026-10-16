/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 출처 zyl-app://<앱 id>/<경로> — 앱마다 다른 출처
 * 수행범위: 경로 → 파일(/shared/·/keyboard/ 는 OS 코드, 나머지는 앱 디렉터리, 그 밖은
 *           거부 — .. ·심볼릭 링크 포함), 확장자 → MIME, 권한에 따른 CSP
 * 의존방향: glib
 * SOLID: SRP — 이름 해석만. WebKit 에 응답하는 건 wam.c
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_APP_SCHEME_H
#define ZYL_WAM_APP_SCHEME_H

#include <glib.h>

#define ZYL_APP_SCHEME "zyl-app"

/* The regular file a decoded zyl-app path names, or NULL when it would
   leave the app's directory (or, for an OS path, that OS directory under
   os_dir — the image's app directory) */
char *zyl_app_scheme_file(const char *app_dir, const char *os_dir, const char *path);

/* The OS code every page includes, whatever the app: the shared runtime and
   the on-screen keyboard shared/bridge.js loads into the page. Served from the
   image (dm-verity), never from the app — so never checked against its seal */
gboolean zyl_app_scheme_is_os(const char *path);

/* The MIME type for a file name; application/octet-stream when unknown */
const char *zyl_app_scheme_mime(const char *file);

/* zyl-app://<app id>/<entry> */
char *zyl_app_scheme_uri(const char *app_id, const char *entry);

/* The Content-Security-Policy sent with every file of an app. Code only from
   the app's own files (what was sealed): never remote, never eval, never
   inline — with the network an app could run what it fetched, and every app
   draws strings others sent (a notification, a file name, a Wi-Fi network's),
   which as markup must not run. "network" opens https/wss (never plain http),
   "webview" lets it frame https pages. The same rule as apps/system/sandbox.js
   getContentPolicy */
char *zyl_app_scheme_csp(gboolean network, gboolean webview);

/* Is a document at uri the app's own: zyl-app://<app id>/… */
gboolean zyl_app_scheme_is_own(const char *app_id, const char *uri);

/* May the app's web view load uri in any of its frames: its own origin, a blank
   or srcdoc frame, its own blob: — and, for an app allowed to frame the web
   ("webview", as its CSP), https. Whether such a page became the top document
   is not seen here (WebKit asks per navigation, not per frame): the WAM checks
   the top document with zyl_app_scheme_is_own when it loads */
gboolean zyl_app_scheme_may_load(const char *app_id, const char *uri, gboolean webview);

#endif /* ZYL_WAM_APP_SCHEME_H */
