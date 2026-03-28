/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 알림 서비스 인터페이스 — 알림/채널 구조체, D-Bus 서비스 선언
 * 수행범위: ZylNotification, ZylNotificationChannel 타입, CRUD 함수 선언
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 알림 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_NOTIFICATION_H
#define ZYL_NOTIFICATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── D-Bus Constants ──────────────────────────────────────── */

#define ZYL_NOTIFICATION_DBUS_NAME "org.zylos.Notification"
#define ZYL_NOTIFICATION_DBUS_PATH "/org/zylos/Notification"
#define ZYL_NOTIFICATION_DBUS_IFACE "org.zylos.Notification"

/* ── Enums ────────────────────────────────────────────────── */

typedef enum {
    ZYL_NOTIFICATION_PRIORITY_LOW     = 0,
    ZYL_NOTIFICATION_PRIORITY_DEFAULT = 1,
    ZYL_NOTIFICATION_PRIORITY_HIGH    = 2,
    ZYL_NOTIFICATION_PRIORITY_URGENT  = 3
} ZylNotificationPriority;

/* ── Structs ──────────────────────────────────────────────── */

typedef struct {
    uint64_t    id;
    char       *app_id;
    char       *channel_id;
    char       *title;
    char       *body;
    char       *icon;
    uint64_t    timestamp;
    bool        read;
    bool        persistent;
    int         priority;       /* 0 (LOW) .. 3 (URGENT) */
} ZylNotification;

typedef struct {
    char       *id;
    char       *name;
    bool        enabled;
    int         importance;     /* 0 .. 3 */
    bool        show_on_lockscreen;
} ZylNotificationChannel;

/* Opaque service handle */
typedef struct _ZylNotificationService ZylNotificationService;

/* ── Service Lifecycle ────────────────────────────────────── */

ZylNotificationService *zyl_notification_service_create(void);
void                    zyl_notification_service_destroy(ZylNotificationService *service);

/* ── Notification Operations ──────────────────────────────── */

/**
 * Post a new notification.
 * Returns the assigned notification ID, or 0 on failure.
 */
uint64_t zyl_notification_post(ZylNotificationService *service,
                               const char             *app_id,
                               const char             *channel_id,
                               const char             *title,
                               const char             *body,
                               const char             *icon,
                               ZylNotificationPriority priority);

/** Cancel (dismiss) a single notification by ID. */
void zyl_notification_cancel(ZylNotificationService *service, uint64_t id);

/**
 * Retrieve all active (unread) notifications.
 * Caller must free the returned array with g_free(), and each element
 * with zyl_notification_free().
 */
void zyl_notification_get_active(ZylNotificationService  *service,
                                 ZylNotification        **out_list,
                                 int                     *out_count);

/** Clear all non-persistent notifications. */
void zyl_notification_clear_all(ZylNotificationService *service);

/* ── Channel Operations ───────────────────────────────────── */

/** Register (or update) a notification channel. */
void zyl_notification_channel_register(ZylNotificationService *service,
                                       const char             *id,
                                       const char             *name,
                                       int                     importance);

/** Enable or disable a channel. */
void zyl_notification_channel_set_enabled(ZylNotificationService *service,
                                          const char             *channel_id,
                                          bool                    enabled);

/* ── Resource Cleanup ─────────────────────────────────────── */

void zyl_notification_free(ZylNotification *notif);
void zyl_notification_channel_free(ZylNotificationChannel *channel);

#ifdef __cplusplus
}
#endif

#endif /* ZYL_NOTIFICATION_H */
