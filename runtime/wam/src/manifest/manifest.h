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
    gboolean launcher;  /* app.json "launcher" as written — read it through
                           zyl_manifest_in_launcher, which honours it for system apps only */
    /* What the app opens ("intents"): pairs, NULL-terminated, same length */
    char **intent_actions;
    char **intent_mimes;
} ZylAppManifest;

/*
 * Whether the app is one the user sees and returns to (home, settings' app list,
 * recents, app switching). "launcher": false is system UI's (lock screen, status
 * bar): a third-party app saying it would hide from every place the user could
 * remove it or turn its permissions off, so only a system app may.
 */
static inline gboolean zyl_manifest_in_launcher(const ZylAppManifest *m) {
    return m->launcher || !m->is_system;
}

/*
 * Parse an app.json manifest from the given app directory.
 * Returns a newly allocated ZylAppManifest or NULL on failure.
 */
ZylAppManifest *zyl_manifest_parse(const char *app_dir);

/*
 * What apps see of an installed app (apps.getInstalled) — the same fields as
 * the emulator's apps service: id, name, nameKey, icon, color, version,
 * description, system, permissions, formFactors, iconSvg, launcher.
 * "system" is the OS's own decision (m->is_system), never the app's word.
 * Returns a floating a{sv}.
 */
GVariant *zyl_manifest_app_info(const ZylAppManifest *m);

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
