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
#define WAM_USER_DIR    "/home/user/.zyl-os/apps"
#define WAM_BRIDGE_JS   "/usr/share/zyl-os/wam/bridge.js"

/* ─── WAM server ─── */
typedef struct {
    GApplication    *app;
    GHashTable      *manifests;   /* id -> ZylAppManifest* */
    GHashTable      *instances;   /* id -> ZylAppInstance* */
    guint            dbus_owner_id;

    /* Abstract interfaces wired up in main() */
    ZylWebEngine     engine;
    ZylAppInterface  iface;
} ZylWam;

#endif /* ZYL_WAM_H */
