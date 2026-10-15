/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: WAM 최상위 타입 정의 및 ZylWam 서버 구조체 선언
 * 수행범위: ZylWam 구조체, 모듈 간 공유 타입 정의
 * 의존방향: manifest.h, lifecycle.h, bridge.h, dbus_service.h
 * SOLID: ISP — 각 모듈이 필요한 인터페이스만 참조
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_H
#define ZYL_WAM_H

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "manifest/manifest.h"
#include "lifecycle/lifecycle.h"
#include "bridge/bridge.h"
#include "dbus/dbus_service.h"

#define WAM_APP_DIR     "/usr/share/zyl-os/apps"
#define WAM_SHARED_DIR  WAM_APP_DIR "/shared"   /* the OS code every app includes */
/* Apps the user installed (the store's install directory, zyl-wam.service) */
#define WAM_USER_DIR    "/data/apps"
/* One container per app: its private data, files and WebKit storage —
   deleting the app deletes the container */
#define WAM_CONTAINERS  "/data/appdata"
#define WAM_BRIDGE_JS   "/usr/share/zyl-os/wam/bridge.js"
/* Whose signatures make a user-installed app's code trusted (the store's key) */
#define WAM_STORE_CONFIG "/etc/zyl-os/store.json"
#define WAM_REVOKED      WAM_USER_DIR "/.revoked"

/* ─── WAM server ─── */
typedef struct {
    GApplication    *app;
    GHashTable      *manifests;   /* id -> ZylAppManifest* */
    GHashTable      *instances;   /* id -> ZylAppInstance* */
    GHashTable      *seals;       /* id -> ZylSeal* — every user-installed app has one */
    guint            dbus_owner_id;

    /* Abstract interfaces wired up in main() */
    ZylWebEngine     engine;
    ZylAppInterface  iface;
} ZylWam;

#endif /* ZYL_WAM_H */
