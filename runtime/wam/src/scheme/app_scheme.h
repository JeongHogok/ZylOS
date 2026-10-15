/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 출처 zyl-app://<앱 id>/<경로> — 앱마다 다른 출처
 * 수행범위: 경로 → 파일(/shared/ 는 OS 공용 코드, 나머지는 앱 디렉터리, 둘 밖은
 *           거부 — .. ·심볼릭 링크 포함), 확장자 → MIME
 * 의존방향: glib
 * SOLID: SRP — 이름 해석만. WebKit 에 응답하는 건 wam.c
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_APP_SCHEME_H
#define ZYL_WAM_APP_SCHEME_H

#include <glib.h>

#define ZYL_APP_SCHEME "zyl-app"

/* The regular file a decoded zyl-app path names, or NULL when it would
   leave the app's directory (or, for /shared/..., the OS shared directory) */
char *zyl_app_scheme_file(const char *app_dir, const char *shared_dir, const char *path);

/* The MIME type for a file name; application/octet-stream when unknown */
const char *zyl_app_scheme_mime(const char *file);

/* zyl-app://<app id>/<entry> */
char *zyl_app_scheme_uri(const char *app_id, const char *entry);

#endif /* ZYL_WAM_APP_SCHEME_H */
