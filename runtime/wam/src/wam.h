/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: WAM 최상위 타입 정의 및 BpiWam 서버 구조체 선언
 * 수행범위: BpiWam 구조체, 모듈 간 공유 타입 정의
 * 의존방향: manifest.h, lifecycle.h, bridge.h, dbus_service.h
 * SOLID: ISP — 각 모듈이 필요한 인터페이스만 참조
 * ────────────────────────────────────────────────────────── */

#ifndef BPI_WAM_H
#define BPI_WAM_H

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "manifest/manifest.h"
#include "lifecycle/lifecycle.h"
#include "bridge/bridge.h"
#include "dbus/dbus_service.h"

#define WAM_APP_DIR     "/usr/share/bpi-os/apps"
#define WAM_USER_DIR    "/home/user/.bpi-os/apps"
#define WAM_BRIDGE_JS   "/usr/share/bpi-os/wam/bridge.js"

/* ─── WAM server ─── */
typedef struct {
    GApplication    *app;
    GHashTable      *manifests;   /* id -> BpiAppManifest* */
    GHashTable      *instances;   /* id -> BpiAppInstance* */
    guint            dbus_owner_id;

    /* Abstract interfaces wired up in main() */
    BpiWebEngine     engine;
    BpiAppInterface  iface;
} BpiWam;

#endif /* BPI_WAM_H */
