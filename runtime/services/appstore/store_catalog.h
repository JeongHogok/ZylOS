/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱스토어 카탈로그 — 스토어 서버의 catalog.json 해석
 * 수행범위: 항목 검증(잘못된 항목은 건너뜀), 패키지 주소 해석(https 만),
 *           버전 비교. 카탈로그 서명 검증은 데몬(appstore.c)이 먼저 한다
 * 의존방향: glib, json-glib
 * SOLID: SRP — 해석만 (순수 함수)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_STORE_CATALOG_H
#define ZYL_STORE_CATALOG_H

#include <glib.h>

#define ZYL_STORE_MAX_PACKAGE (50 * 1024 * 1024)

typedef struct {
    char   *id, *name, *version, *description, *developer, *category;
    char   *package_url;   /* absolute https */
    char   *sha256;        /* 64 hex */
    char   *signature;     /* base64 RSA-SHA256 over the package's SHA-256 */
    gint64  size;
    char  **permissions;   /* NULL-terminated */
} ZylStoreApp;

/* The valid entries (invalid ones are skipped), or NULL when the text is not
   a catalog at all. catalog_url resolves relative package paths. */
GPtrArray *zyl_store_catalog_parse(const char *json, gssize len, const char *catalog_url);

void zyl_store_app_free(gpointer app);

/* Dotted numeric versions: <0, 0, >0 */
int zyl_store_version_cmp(const char *a, const char *b);

/* ref against base; NULL unless the result is https */
char *zyl_store_resolve_url(const char *base, const char *ref);

#endif /* ZYL_STORE_CATALOG_H */
