/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 매니페스트 구조체 및 파싱 인터페이스 정의
 * 수행범위: BpiAppManifest 타입, bpi_manifest_parse/free/scan 함수 선언
 * 의존방향: glib.h
 * SOLID: DIP — 매니페스트 구현이 아닌 추상 인터페이스에 의존
 * ────────────────────────────────────────────────────────── */

#ifndef BPI_WAM_MANIFEST_H
#define BPI_WAM_MANIFEST_H

#include <glib.h>

/* ─── App manifest ─── */
typedef struct {
    char *id;           /* Unique app ID (e.g. "com.bpios.home") */
    char *name;         /* Display name */
    char *version;      /* Version string */
    char *entry;        /* Entry-point HTML file */
    char *icon;         /* Icon path */
    char *base_path;    /* App directory path */
    char **permissions; /* Requested permissions (NULL-terminated) */
    int n_permissions;
    gboolean is_system; /* TRUE for system apps */
} BpiAppManifest;

/*
 * Parse an app.json manifest from the given app directory.
 * Returns a newly allocated BpiAppManifest or NULL on failure.
 */
BpiAppManifest *bpi_manifest_parse(const char *app_dir);

/*
 * Free all memory owned by a manifest.  Safe to call with NULL.
 */
void bpi_manifest_free(gpointer data);

/*
 * Scan a base directory for app subdirectories and insert discovered
 * manifests into the provided hash table (id -> BpiAppManifest*).
 * is_system marks all discovered manifests accordingly.
 */
void bpi_manifest_scan_dir(GHashTable *manifests,
                           const char *base_dir,
                           gboolean    is_system);

#endif /* BPI_WAM_MANIFEST_H */
