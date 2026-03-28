/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 매니페스트 구조체 및 파싱 인터페이스 정의
 * 수행범위: ZylAppManifest 타입, zyl_manifest_parse/free/scan 함수 선언
 * 의존방향: glib.h
 * SOLID: DIP — 매니페스트 구현이 아닌 추상 인터페이스에 의존
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_MANIFEST_H
#define ZYL_WAM_MANIFEST_H

#include <glib.h>

/* ─── App manifest ─── */
typedef struct {
    char *id;           /* Unique app ID (e.g. "com.zylos.home") */
    char *name;         /* Display name */
    char *version;      /* Version string */
    char *entry;        /* Entry-point HTML file */
    char *icon;         /* Icon path */
    char *base_path;    /* App directory path */
    char **permissions; /* Requested permissions (NULL-terminated) */
    int n_permissions;
    gboolean is_system; /* TRUE for system apps */
} ZylAppManifest;

/*
 * Parse an app.json manifest from the given app directory.
 * Returns a newly allocated ZylAppManifest or NULL on failure.
 */
ZylAppManifest *zyl_manifest_parse(const char *app_dir);

/*
 * Free all memory owned by a manifest.  Safe to call with NULL.
 */
void zyl_manifest_free(gpointer data);

/*
 * Scan a base directory for app subdirectories and insert discovered
 * manifests into the provided hash table (id -> ZylAppManifest*).
 * is_system marks all discovered manifests accordingly.
 */
void zyl_manifest_scan_dir(GHashTable *manifests,
                           const char *base_dir,
                           gboolean    is_system);

#endif /* ZYL_WAM_MANIFEST_H */
