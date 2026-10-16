/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Adapter
 *
 * 역할: 컴포지터 → OS 서비스 — 제스처 → WAM 내비게이션(GoHome·GoBack·SwitchApp),
 *       사용자 입력 → 전원 서비스 UserActivity
 * 수행범위: 시스템 버스 연결(처음 쓸 때, 실패하면 다음에 다시), 답을 기다리지
 *           않는 메서드 호출 — 컴포지터의 이벤트 루프를 막지 않는다. 화면 상태는
 *           자기 스레드의 GLib 컨텍스트에서 받아 원자 변수로만 넘긴다
 * 의존방향: os_bus.h → zyl_compositor.h, gio (버스 정책: root 는 org.zylos 를 부를 수 있다)
 * SOLID: SRP — 전달만. 무엇이 앞에 올지는 WAM 의 셸 정책(runtime/wam/src/shell),
 *        언제 어두워지고 꺼질지는 전원 서비스
 * ────────────────────────────────────────────────────────── */

#include "os_bus.h"

#include <string.h>
#include <gio/gio.h>
#include <wlr/util/log.h>
#include "../../../runtime/services/power/power.h"

static GDBusConnection *os_bus;

/* <service>.<method>(args), no reply asked for; args is taken */
static void send_call(const char *service, const char *method, GVariant *args)
{
    if (!os_bus) {
        GError *err = NULL;
        /* GDBus writes from its own worker thread: sending needs no GLib main
           loop here */
        os_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
        if (!os_bus) {
            wlr_log(WLR_ERROR, "OS bus: not reachable (%s)", err ? err->message : "?");
            g_clear_error(&err);
            if (args) g_variant_unref(g_variant_ref_sink(args));
            return;
        }
    }
    char *path = g_strdup_printf("/%s", service);
    for (char *p = path; *p; p++) if (*p == '.') *p = '/';
    GDBusMessage *m = g_dbus_message_new_method_call(service, path, service, method);
    if (args) g_dbus_message_set_body(m, args);
    g_dbus_message_set_flags(m, G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    GError *err = NULL;
    if (!g_dbus_connection_send_message(os_bus, m, G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &err)) {
        wlr_log(WLR_ERROR, "OS bus: %s.%s not sent (%s)", service, method, err ? err->message : "?");
        g_clear_error(&err);
        g_clear_object(&os_bus);   /* reconnect next time */
    }
    g_object_unref(m);
    g_free(path);
}

void zyl_wam_nav_send(struct zyl_server *server, const char *signal_name,
                      const char *detail)
{
    (void)server;
    GVariant *args = NULL;
    if (strcmp(signal_name, "SwitchApp") == 0)
        args = g_variant_new("(i)", detail && detail[0] == '-' ? -1 : 1);
    else if (strcmp(signal_name, "GoHome") != 0 && strcmp(signal_name, "GoBack") != 0 &&
             strcmp(signal_name, "OpenPanel") != 0 && strcmp(signal_name, "ShowRecents") != 0)
        return;
    send_call("org.zylos.WebAppManager", signal_name, args);
}

/* ─── The screen: on or off (org.zylos.PowerManager.StateChanged) ─── */
static gint screen_on = 1;

static void on_power_state(GDBusConnection *c, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *signal, GVariant *params, gpointer data)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)signal; (void)data;
    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(ii)"))) return;
    gint32 was, now;
    g_variant_get(params, "(ii)", &was, &now);
    g_atomic_int_set(&screen_on, now == ZYL_POWER_STATE_ACTIVE || now == ZYL_POWER_STATE_DIM);
}

static gpointer screen_watch(gpointer data)
{
    (void)data;
    GMainContext *ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);   /* signals are dispatched here */
    GError *err = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!bus) {
        wlr_log(WLR_ERROR, "OS bus: screen state not followed (%s)", err ? err->message : "?");
        g_clear_error(&err);
        return NULL;
    }
    g_dbus_connection_signal_subscribe(bus, "org.zylos.PowerManager", "org.zylos.PowerManager",
        "StateChanged", "/org/zylos/PowerManager", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_power_state, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(ctx, FALSE);
    g_main_loop_run(loop);   /* for the compositor's life */
    return NULL;
}

void zyl_os_bus_watch_screen(void)
{
    g_thread_unref(g_thread_new("zyl-screen", screen_watch, NULL));
}

bool zyl_os_screen_on(void)
{
    return g_atomic_int_get(&screen_on) != 0;
}

void zyl_os_user_activity(void)
{
    static uint32_t last;
    uint32_t now = zyl_now_ms();
    if (last && now - last < 1000) return;
    last = now;
    send_call("org.zylos.PowerManager", "UserActivity", NULL);
}
