#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 알림 서비스 구현 — D-Bus로 알림 생성/조회/삭제/채널 관리
 * 수행범위: 알림 저장소 관리, D-Bus 시그널 발송, 채널별 필터링
 * 의존방향: notification.h, gio/gio.h
 * SOLID: SRP — 알림 관리 로직만 담당
 * ────────────────────────────────────────────────────────── */

#include "notification.h"

#include <gio/gio.h>
#include <glib.h>
#include <string.h>
#include <time.h>

/* ── D-Bus Introspection XML ──────────────────────────────── */

static const char *introspection_xml =
    "<node>"
    "  <interface name='" ZYL_NOTIFICATION_DBUS_IFACE "'>"
    "    <method name='Post'>"
    "      <arg direction='in'  type='s' name='app_id'/>"
    "      <arg direction='in'  type='s' name='channel_id'/>"
    "      <arg direction='in'  type='s' name='title'/>"
    "      <arg direction='in'  type='s' name='body'/>"
    "      <arg direction='in'  type='s' name='icon'/>"
    "      <arg direction='in'  type='i' name='priority'/>"
    "      <arg direction='out' type='t' name='id'/>"
    "    </method>"
    "    <method name='Cancel'>"
    "      <arg direction='in'  type='t' name='id'/>"
    "    </method>"
    "    <method name='GetActive'>"
    "      <arg direction='out' type='a(tsssssbbi)' name='notifications'/>"
    "    </method>"
    "    <method name='ClearAll'/>"
    "    <method name='RegisterChannel'>"
    "      <arg direction='in'  type='s' name='id'/>"
    "      <arg direction='in'  type='s' name='name'/>"
    "      <arg direction='in'  type='i' name='importance'/>"
    "    </method>"
    "    <method name='SetChannelEnabled'>"
    "      <arg direction='in'  type='s' name='channel_id'/>"
    "      <arg direction='in'  type='b' name='enabled'/>"
    "    </method>"
    "    <signal name='NotificationPosted'>"
    "      <arg type='t' name='id'/>"
    "      <arg type='s' name='app_id'/>"
    "      <arg type='s' name='title'/>"
    "      <arg type='s' name='body'/>"
    "      <arg type='i' name='priority'/>"
    "    </signal>"
    "    <signal name='NotificationDismissed'>"
    "      <arg type='t' name='id'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* ── Internal Service Structure ───────────────────────────── */

struct _ZylNotificationService {
    GArray            *notifications;   /* element type: ZylNotification */
    GHashTable        *channels;        /* key: channel_id (char*), value: ZylNotificationChannel* */
    GDBusConnection   *connection;
    GDBusNodeInfo     *introspection_data;
    guint              bus_owner_id;
    guint              registration_id;
    uint64_t           next_id;
};

/* ── Forward Declarations ─────────────────────────────────── */

static void handle_method_call(GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data);

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data);

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data);

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data);

/* ── Helper: Deep-copy a notification ─────────────────────── */

static ZylNotification notification_dup(const ZylNotification *src)
{
    ZylNotification copy;
    copy.id          = src->id;
    copy.app_id      = g_strdup(src->app_id);
    copy.channel_id  = g_strdup(src->channel_id);
    copy.title       = g_strdup(src->title);
    copy.body        = g_strdup(src->body);
    copy.icon        = g_strdup(src->icon);
    copy.timestamp   = src->timestamp;
    copy.read        = src->read;
    copy.persistent  = src->persistent;
    copy.priority    = src->priority;
    return copy;
}

/* ── Helper: Free fields inside a notification (not the pointer itself) */

static void notification_clear(ZylNotification *n)
{
    g_free(n->app_id);
    g_free(n->channel_id);
    g_free(n->title);
    g_free(n->body);
    g_free(n->icon);
    n->app_id     = NULL;
    n->channel_id = NULL;
    n->title      = NULL;
    n->body       = NULL;
    n->icon       = NULL;
}

/* ── Helper: Free a heap-allocated channel ────────────────── */

static void channel_destroy(gpointer data)
{
    ZylNotificationChannel *ch = data;
    if (!ch) return;
    g_free(ch->id);
    g_free(ch->name);
    g_free(ch);
}

/* ── Helper: Get current UNIX timestamp (seconds) ─────────── */

static uint64_t now_timestamp(void)
{
    return (uint64_t)g_get_real_time() / G_USEC_PER_SEC;
}

/* ── Helper: Emit D-Bus signal ────────────────────────────── */

static void emit_signal(ZylNotificationService *service,
                        const char             *signal_name,
                        GVariant               *params)
{
    if (!service->connection) return;

    GError *err = NULL;
    g_dbus_connection_emit_signal(service->connection,
                                 NULL,
                                 ZYL_NOTIFICATION_DBUS_PATH,
                                 ZYL_NOTIFICATION_DBUS_IFACE,
                                 signal_name,
                                 params,
                                 &err);
    if (err) {
        g_warning("zyl-notification: failed to emit %s: %s", signal_name, err->message);
        g_error_free(err);
    }
}

/* ── Channel Lookup ───────────────────────────────────────── */

static ZylNotificationChannel *channel_lookup(ZylNotificationService *service,
                                              const char             *channel_id)
{
    if (!channel_id) return NULL;
    return g_hash_table_lookup(service->channels, channel_id);
}

/* ══════════════════════════════════════════════════════════════
 * D-Bus Method Dispatch Table (OCP — extend by adding entries)
 * ══════════════════════════════════════════════════════════════ */

typedef void (*MethodHandler)(ZylNotificationService *service,
                              GVariant               *parameters,
                              GDBusMethodInvocation  *invocation);

static void handle_post(ZylNotificationService *service,
                        GVariant               *parameters,
                        GDBusMethodInvocation  *invocation);

static void handle_cancel(ZylNotificationService *service,
                          GVariant               *parameters,
                          GDBusMethodInvocation  *invocation);

static void handle_get_active(ZylNotificationService *service,
                              GVariant               *parameters,
                              GDBusMethodInvocation  *invocation);

static void handle_clear_all(ZylNotificationService *service,
                             GVariant               *parameters,
                             GDBusMethodInvocation  *invocation);

static void handle_register_channel(ZylNotificationService *service,
                                    GVariant               *parameters,
                                    GDBusMethodInvocation  *invocation);

static void handle_set_channel_enabled(ZylNotificationService *service,
                                       GVariant               *parameters,
                                       GDBusMethodInvocation  *invocation);

typedef struct {
    const char    *name;
    MethodHandler  handler;
} DispatchEntry;

static const DispatchEntry dispatch_table[] = {
    { "Post",              handle_post               },
    { "Cancel",            handle_cancel             },
    { "GetActive",         handle_get_active         },
    { "ClearAll",          handle_clear_all          },
    { "RegisterChannel",   handle_register_channel   },
    { "SetChannelEnabled", handle_set_channel_enabled },
    { NULL, NULL }
};

/* ── D-Bus Virtual Table ──────────────────────────────────── */

static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    NULL,   /* get_property */
    NULL    /* set_property */
};

/* ══════════════════════════════════════════════════════════════
 * D-Bus Method Handler (dispatches to table)
 * ══════════════════════════════════════════════════════════════ */

static void handle_method_call(GDBusConnection       *connection,
                               const gchar           *sender,
                               const gchar           *object_path,
                               const gchar           *interface_name,
                               const gchar           *method_name,
                               GVariant              *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer               user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    ZylNotificationService *service = user_data;

    for (const DispatchEntry *e = dispatch_table; e->name; e++) {
        if (g_strcmp0(method_name, e->name) == 0) {
            e->handler(service, parameters, invocation);
            return;
        }
    }

    g_dbus_method_invocation_return_error(invocation,
                                          G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method: %s", method_name);
}

/* ══════════════════════════════════════════════════════════════
 * D-Bus Method Implementations
 * ══════════════════════════════════════════════════════════════ */

static void handle_post(ZylNotificationService *service,
                        GVariant               *parameters,
                        GDBusMethodInvocation  *invocation)
{
    const char *app_id, *channel_id, *title, *body, *icon;
    gint32 priority;

    g_variant_get(parameters, "(&s&s&s&s&si)",
                  &app_id, &channel_id, &title, &body, &icon, &priority);

    uint64_t id = zyl_notification_post(service, app_id, channel_id,
                                        title, body, icon,
                                        (ZylNotificationPriority)priority);

    g_dbus_method_invocation_return_value(invocation, g_variant_new("(t)", id));
}

static void handle_cancel(ZylNotificationService *service,
                          GVariant               *parameters,
                          GDBusMethodInvocation  *invocation)
{
    guint64 id;
    g_variant_get(parameters, "(t)", &id);
    zyl_notification_cancel(service, id);
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static void handle_get_active(ZylNotificationService *service,
                              GVariant               *parameters,
                              GDBusMethodInvocation  *invocation)
{
    (void)parameters;

    ZylNotification *list = NULL;
    int count = 0;
    zyl_notification_get_active(service, &list, &count);

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(tsssssbbi)"));

    for (int i = 0; i < count; i++) {
        ZylNotification *n = &list[i];
        g_variant_builder_add(&builder, "(tsssssbbi)",
                              n->id,
                              n->app_id     ? n->app_id     : "",
                              n->channel_id ? n->channel_id : "",
                              n->title      ? n->title      : "",
                              n->body       ? n->body       : "",
                              n->icon       ? n->icon       : "",
                              n->read,
                              n->persistent,
                              n->priority);
        zyl_notification_free(n);
    }
    g_free(list);

    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(a(tsssssbbi))", &builder));
}

static void handle_clear_all(ZylNotificationService *service,
                             GVariant               *parameters,
                             GDBusMethodInvocation  *invocation)
{
    (void)parameters;
    zyl_notification_clear_all(service);
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static void handle_register_channel(ZylNotificationService *service,
                                    GVariant               *parameters,
                                    GDBusMethodInvocation  *invocation)
{
    const char *id, *name;
    gint32 importance;
    g_variant_get(parameters, "(&s&si)", &id, &name, &importance);
    zyl_notification_channel_register(service, id, name, importance);
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static void handle_set_channel_enabled(ZylNotificationService *service,
                                       GVariant               *parameters,
                                       GDBusMethodInvocation  *invocation)
{
    const char *channel_id;
    gboolean enabled;
    g_variant_get(parameters, "(&sb)", &channel_id, &enabled);
    zyl_notification_channel_set_enabled(service, channel_id, (bool)enabled);
    g_dbus_method_invocation_return_value(invocation, NULL);
}

/* ══════════════════════════════════════════════════════════════
 * Bus Ownership Callbacks
 * ══════════════════════════════════════════════════════════════ */

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data)
{
    (void)name;
    ZylNotificationService *service = user_data;
    service->connection = g_object_ref(connection);

    if (!service->introspection_data || !service->introspection_data->interfaces ||
        !service->introspection_data->interfaces[0]) {
        g_warning("zyl-notification: introspection data is NULL, cannot register object");
        return;
    }

    GError *err = NULL;
    service->registration_id = g_dbus_connection_register_object(
        connection,
        ZYL_NOTIFICATION_DBUS_PATH,
        service->introspection_data->interfaces[0],
        &interface_vtable,
        service,
        NULL,
        &err);

    if (err) {
        g_warning("zyl-notification: failed to register object: %s", err->message);
        g_error_free(err);
    }
}

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_info("zyl-notification: acquired D-Bus name %s", name);
}

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    (void)connection;
    (void)user_data;
    g_warning("zyl-notification: lost D-Bus name %s", name);
}

/* ══════════════════════════════════════════════════════════════
 * Public API — Service Lifecycle
 * ══════════════════════════════════════════════════════════════ */

ZylNotificationService *zyl_notification_service_create(void)
{
    ZylNotificationService *service = g_new0(ZylNotificationService, 1);

    service->notifications = g_array_new(FALSE, TRUE, sizeof(ZylNotification));
    service->channels      = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                    g_free, channel_destroy);
    service->next_id       = 1;

    service->introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_assert(service->introspection_data != NULL);

    service->bus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        ZYL_NOTIFICATION_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        service,
        NULL);

    return service;
}

void zyl_notification_service_destroy(ZylNotificationService *service)
{
    if (!service) return;

    if (service->bus_owner_id > 0)
        g_bus_unown_name(service->bus_owner_id);

    if (service->registration_id > 0 && service->connection) {
        g_dbus_connection_unregister_object(service->connection,
                                            service->registration_id);
    }

    if (service->connection)
        g_object_unref(service->connection);

    if (service->introspection_data)
        g_dbus_node_info_unref(service->introspection_data);

    /* Free all stored notifications */
    for (guint i = 0; i < service->notifications->len; i++) {
        ZylNotification *n = &g_array_index(service->notifications, ZylNotification, i);
        notification_clear(n);
    }
    g_array_free(service->notifications, TRUE);

    g_hash_table_destroy(service->channels);

    g_free(service);
}

/* ══════════════════════════════════════════════════════════════
 * Public API — Notification Operations
 * ══════════════════════════════════════════════════════════════ */

uint64_t zyl_notification_post(ZylNotificationService *service,
                               const char             *app_id,
                               const char             *channel_id,
                               const char             *title,
                               const char             *body,
                               const char             *icon,
                               ZylNotificationPriority priority)
{
    if (!service || !app_id || !title) return 0;

    /* ── Channel-based filtering ──────────────────────────── */
    ZylNotificationChannel *ch = channel_lookup(service, channel_id);
    if (ch) {
        /* Channel disabled -> reject */
        if (!ch->enabled) {
            g_debug("zyl-notification: channel '%s' is disabled, dropping", channel_id);
            return 0;
        }
        /* Channel importance 0 -> silent drop */
        if (ch->importance == 0) {
            g_debug("zyl-notification: channel '%s' importance=0, dropping", channel_id);
            return 0;
        }
    }

    /* ── Build notification ───────────────────────────────── */
    ZylNotification notif = {
        .id         = service->next_id++,
        .app_id     = g_strdup(app_id),
        .channel_id = g_strdup(channel_id ? channel_id : ""),
        .title      = g_strdup(title),
        .body       = g_strdup(body ? body : ""),
        .icon       = g_strdup(icon ? icon : ""),
        .timestamp  = now_timestamp(),
        .read       = false,
        .persistent = (priority == ZYL_NOTIFICATION_PRIORITY_URGENT),
        .priority   = (int)priority
    };

    g_array_append_val(service->notifications, notif);

    /* ── Emit D-Bus signal ────────────────────────────────── */
    emit_signal(service, "NotificationPosted",
                g_variant_new("(tssi)",
                              notif.id,
                              notif.app_id,
                              notif.title,
                              notif.body,
                              notif.priority));

    g_info("zyl-notification: posted id=%" G_GUINT64_FORMAT " app=%s title='%s'",
           notif.id, notif.app_id, notif.title);

    return notif.id;
}

void zyl_notification_cancel(ZylNotificationService *service, uint64_t id)
{
    if (!service) return;

    for (guint i = 0; i < service->notifications->len; i++) {
        ZylNotification *n = &g_array_index(service->notifications, ZylNotification, i);
        if (n->id == id) {
            notification_clear(n);
            g_array_remove_index(service->notifications, i);

            emit_signal(service, "NotificationDismissed",
                        g_variant_new("(t)", id));

            g_info("zyl-notification: cancelled id=%" G_GUINT64_FORMAT, id);
            return;
        }
    }
}

void zyl_notification_get_active(ZylNotificationService  *service,
                                 ZylNotification        **out_list,
                                 int                     *out_count)
{
    if (!service || !out_list || !out_count) {
        if (out_list)  *out_list  = NULL;
        if (out_count) *out_count = 0;
        return;
    }

    /* Count unread notifications */
    int count = 0;
    for (guint i = 0; i < service->notifications->len; i++) {
        ZylNotification *n = &g_array_index(service->notifications, ZylNotification, i);
        if (!n->read)
            count++;
    }

    if (count == 0) {
        *out_list  = NULL;
        *out_count = 0;
        return;
    }

    ZylNotification *list = g_new0(ZylNotification, count);
    int idx = 0;

    for (guint i = 0; i < service->notifications->len; i++) {
        ZylNotification *n = &g_array_index(service->notifications, ZylNotification, i);
        if (!n->read) {
            list[idx] = notification_dup(n);
            idx++;
        }
    }

    *out_list  = list;
    *out_count = count;
}

void zyl_notification_clear_all(ZylNotificationService *service)
{
    if (!service) return;

    /* Remove non-persistent notifications from the end to avoid index shifting */
    for (gint i = (gint)service->notifications->len - 1; i >= 0; i--) {
        ZylNotification *n = &g_array_index(service->notifications, ZylNotification, (guint)i);
        if (!n->persistent) {
            uint64_t id = n->id;
            notification_clear(n);
            g_array_remove_index(service->notifications, (guint)i);

            emit_signal(service, "NotificationDismissed",
                        g_variant_new("(t)", id));
        }
    }

    g_info("zyl-notification: cleared all non-persistent notifications");
}

/* ══════════════════════════════════════════════════════════════
 * Public API — Channel Operations
 * ══════════════════════════════════════════════════════════════ */

void zyl_notification_channel_register(ZylNotificationService *service,
                                       const char             *id,
                                       const char             *name,
                                       int                     importance)
{
    if (!service || !id || !name) return;

    /* Clamp importance to valid range */
    if (importance < 0) importance = 0;
    if (importance > 3) importance = 3;

    ZylNotificationChannel *ch = g_new0(ZylNotificationChannel, 1);
    ch->id                = g_strdup(id);
    ch->name              = g_strdup(name);
    ch->enabled           = true;
    ch->importance        = importance;
    ch->show_on_lockscreen = (importance >= 2);  /* HIGH and URGENT visible on lockscreen */

    /* Insert or replace existing channel */
    g_hash_table_replace(service->channels, g_strdup(id), ch);

    g_info("zyl-notification: registered channel '%s' (%s) importance=%d",
           id, name, importance);
}

void zyl_notification_channel_set_enabled(ZylNotificationService *service,
                                          const char             *channel_id,
                                          bool                    enabled)
{
    if (!service || !channel_id) return;

    ZylNotificationChannel *ch = channel_lookup(service, channel_id);
    if (ch) {
        ch->enabled = enabled;
        g_info("zyl-notification: channel '%s' %s",
               channel_id, enabled ? "enabled" : "disabled");
    } else {
        g_warning("zyl-notification: channel '%s' not found", channel_id);
    }
}

/* ══════════════════════════════════════════════════════════════
 * Public API — Resource Cleanup
 * ══════════════════════════════════════════════════════════════ */

void zyl_notification_free(ZylNotification *notif)
{
    if (!notif) return;
    g_free(notif->app_id);
    g_free(notif->channel_id);
    g_free(notif->title);
    g_free(notif->body);
    g_free(notif->icon);
    notif->app_id     = NULL;
    notif->channel_id = NULL;
    notif->title      = NULL;
    notif->body       = NULL;
    notif->icon       = NULL;
}

void zyl_notification_channel_free(ZylNotificationChannel *channel)
{
    if (!channel) return;
    g_free(channel->id);
    g_free(channel->name);
    channel->id   = NULL;
    channel->name = NULL;
}

/* ══════════════════════════════════════════════════════════════
 * Entry Point
 * ══════════════════════════════════════════════════════════════ */

#ifndef ZYL_TEST
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_info("zyl-notification: starting service");

    ZylNotificationService *service = zyl_notification_service_create();
    if (!service) {
        g_critical("zyl-notification: failed to create service");
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* Cleanup (reached on shutdown) */
    g_main_loop_unref(loop);
    zyl_notification_service_destroy(service);

    return 0;
}
#endif /* ZYL_TEST */
