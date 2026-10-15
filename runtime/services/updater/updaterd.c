/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 시스템 업데이트 데몬 (org.zylos.Updater) — 설정 앱의 소프트웨어 업데이트
 * 수행범위: 상태 조회, 확인, 다운로드·검증·설치(작업 스레드), 재시작,
 *           진행 상황을 StateChanged 시그널로 알림. 업데이트 시험 부팅이면
 *           부팅 후 상태 점검 → 통과 시 슬롯 확정, 실패 시 재부팅(U-Boot 가
 *           zyl_boot_limit 회 뒤 이전 슬롯으로 되돌림)
 * 의존방향: updater.h (설치·검증·U-Boot 환경), gio/gio.h (D-Bus)
 * SOLID: SRP — D-Bus 노출과 작업 순서만. 설치 로직은 updater.c
 * ────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include "updater.h"

#ifndef ZYL_UPDATE_SERVER_FILE
#define ZYL_UPDATE_SERVER_FILE "/etc/zyl-os/update-server"
#endif
#ifndef CONFIRM_AFTER_S
#define CONFIRM_AFTER_S 60       /* the session should be up by then */
#endif

static ZylUpdater *g_u;
static GDBusConnection *g_conn;
static gboolean g_busy;          /* a worker thread owns g_u */
static gboolean g_supported;
static ZylUpdateState g_state = ZYL_UPDATE_IDLE;
static int g_progress;
static char *g_version;          /* running */
static char *g_available;        /* on offer, NULL when none */

static const char *state_name(ZylUpdateState s) {
    switch (s) {
    case ZYL_UPDATE_CHECKING:       return "checking";
    case ZYL_UPDATE_AVAILABLE:      return "available";
    case ZYL_UPDATE_DOWNLOADING:    return "downloading";
    case ZYL_UPDATE_VERIFYING:      return "verifying";
    case ZYL_UPDATE_APPLYING:       return "applying";
    case ZYL_UPDATE_PENDING_REBOOT: return "pending_reboot";
    case ZYL_UPDATE_FAILED:         return "failed";
    case ZYL_UPDATE_UP_TO_DATE:     return "up_to_date";
    default:                        return "idle";
    }
}

static GVariant *state_dict(void) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "state",
        g_variant_new_string(g_supported ? state_name(g_state) : "unsupported"));
    g_variant_builder_add(&b, "{sv}", "progress", g_variant_new_int32(g_progress));
    g_variant_builder_add(&b, "{sv}", "version", g_variant_new_string(g_version));
    g_variant_builder_add(&b, "{sv}", "available", g_variant_new_string(g_available ? g_available : ""));
    return g_variant_builder_end(&b);
}

static void publish(void) {
    if (g_conn)
        g_dbus_connection_emit_signal(g_conn, NULL, "/org/zylos/Updater", "org.zylos.Updater",
                                      "StateChanged", g_variant_new("(@a{sv})", state_dict()), NULL);
}

/* ─── Work off the main loop: a download takes minutes ─── */
typedef struct { ZylUpdateState state; int pct; } Progress;

static gboolean progress_idle(gpointer data) {
    Progress *p = data;
    if (g_busy) {                /* a late report must not undo the final state */
        g_state = p->state;
        g_progress = p->pct;
        publish();
    }
    return G_SOURCE_REMOVE;
}

static void on_progress(ZylUpdateState st, int pct, const char *msg, void *data) {
    (void)msg; (void)data;
    Progress *p = g_new(Progress, 1);
    p->state = st;
    p->pct = pct;
    g_idle_add_full(G_PRIORITY_DEFAULT, progress_idle, p, g_free);
}

typedef enum { JOB_CHECK = 1, JOB_INSTALL } Job;

static void job_thread(GTask *task, gpointer src, gpointer data, GCancellable *cancel) {
    (void)src; (void)cancel;
    ZylUpdateManifest *m = NULL;
    if (GPOINTER_TO_INT(data) == JOB_CHECK)
        zyl_updater_check(g_u, &m);
    else if (zyl_updater_download(g_u, on_progress, NULL))
        zyl_updater_apply(g_u, on_progress, NULL);
    g_task_return_pointer(task, m, (GDestroyNotify)zyl_update_manifest_free);
}

static void job_done(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src; (void)data;
    ZylUpdateManifest *m = g_task_propagate_pointer(G_TASK(res), NULL);
    if (m) {
        g_free(g_available);
        g_available = g_strdup(m->version);
        zyl_update_manifest_free(m);
    }
    g_busy = FALSE;
    g_state = zyl_updater_get_state(g_u);
    g_progress = 0;
    publish();
}

static void start(Job job) {
    g_busy = TRUE;
    g_state = job == JOB_CHECK ? ZYL_UPDATE_CHECKING : ZYL_UPDATE_DOWNLOADING;
    g_progress = 0;
    GTask *t = g_task_new(NULL, NULL, job_done, NULL);
    g_task_set_task_data(t, GINT_TO_POINTER(job), NULL);
    g_task_run_in_thread(t, job_thread);
    g_object_unref(t);
    publish();
}

/* ─── Boot confirmation ─── */
static gboolean confirm_boot(gpointer data) {
    (void)data;
    if (g_busy || !zyl_updater_on_trial(g_u)) return G_SOURCE_REMOVE;
    if (!zyl_updater_mark_verified(g_u)) {
        /* Restart so U-Boot counts this try; after zyl_boot_limit it boots the previous slot */
        fprintf(stderr, "[UPDATER] Updated slot is not healthy — restarting\n");
        const char *argv[] = {"systemctl", "reboot", NULL};
        g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
    }
    return G_SOURCE_REMOVE;
}

/* ─── D-Bus ─── */
static const char *updater_introspection_xml =
    "<node>"
    "  <interface name='org.zylos.Updater'>"
    "    <method name='GetState'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <method name='Check'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <method name='Install'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <method name='Restart'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <signal name='StateChanged'>"
    "      <arg type='a{sv}' name='state'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

/* Every method answers with the state; one that does not fit it is ignored */
static void handle_updater_method(GDBusConnection *conn, const gchar *sender,
                                  const gchar *path, const gchar *iface,
                                  const gchar *method, GVariant *params,
                                  GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)params; (void)data;
    bool idle = g_supported && !g_busy;
    if (g_strcmp0(method, "Check") == 0) {
        if (idle && g_state != ZYL_UPDATE_PENDING_REBOOT) start(JOB_CHECK);
    } else if (g_strcmp0(method, "Install") == 0) {
        if (idle && g_state == ZYL_UPDATE_AVAILABLE) start(JOB_INSTALL);
    } else if (g_strcmp0(method, "Restart") == 0) {
        if (idle && g_state == ZYL_UPDATE_PENDING_REBOOT) zyl_updater_reboot_to_update(g_u);
    }
    g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{sv})", state_dict()));
}

static const GDBusInterfaceVTable updater_vtable = { .method_call = handle_updater_method };

static void on_updater_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    g_conn = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(updater_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0])
        g_dbus_connection_register_object(conn, "/org/zylos/Updater",
            info->interfaces[0], &updater_vtable, NULL, NULL, NULL);
    if (info) g_dbus_node_info_unref(info);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char *server = NULL;
    if (g_file_get_contents(ZYL_UPDATE_SERVER_FILE, &server, NULL, NULL)) g_strstrip(server);
    g_u = zyl_updater_create(server && *server ? server : NULL, NULL);
    g_free(server);
    if (!g_u) return 1;
    ZylPartitionInfo *info = zyl_updater_get_partition_info(g_u);
    g_version = g_strdup(info ? info->active_version : "");
    zyl_partition_info_free(info);
    g_supported = zyl_updater_supported(g_u);

    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.Updater", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_updater_bus, NULL, NULL, NULL, NULL);
    g_timeout_add_seconds(CONFIRM_AFTER_S, confirm_boot, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
