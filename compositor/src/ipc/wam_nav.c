/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Adapter
 *
 * 역할: 제스처 → WAM 내비게이션 (org.zylos.WebAppManager GoHome·GoBack·SwitchApp)
 * 수행범위: 시스템 버스 연결(처음 쓸 때, 실패하면 다음에 다시), 답을 기다리지
 *           않는 메서드 호출 — 컴포지터의 이벤트 루프를 막지 않는다
 * 의존방향: wam_nav.h → zyl_compositor.h, gio (버스 정책: root 는 org.zylos 를 부를 수 있다)
 * SOLID: SRP — 전달만. 무엇이 앞에 올지는 WAM 의 셸 정책(runtime/wam/src/shell)
 * ────────────────────────────────────────────────────────── */

#include "wam_nav.h"

#include <string.h>
#include <gio/gio.h>
#include <wlr/util/log.h>

static GDBusConnection *wam_bus;

void zyl_wam_nav_send(struct zyl_server *server, const char *signal_name,
                      const char *detail)
{
    (void)server;
    GVariant *args = NULL;
    if (strcmp(signal_name, "SwitchApp") == 0)
        args = g_variant_new("(i)", detail && detail[0] == '-' ? -1 : 1);
    else if (strcmp(signal_name, "GoHome") != 0 && strcmp(signal_name, "GoBack") != 0)
        return;

    if (!wam_bus) {
        GError *err = NULL;
        /* GDBus writes from its own worker thread: sending needs no GLib main
           loop here, and no reply is asked for */
        wam_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
        if (!wam_bus) {
            wlr_log(WLR_ERROR, "WAM navigation: no system bus (%s)", err ? err->message : "?");
            g_clear_error(&err);
            if (args) g_variant_unref(g_variant_ref_sink(args));
            return;
        }
    }
    GDBusMessage *m = g_dbus_message_new_method_call("org.zylos.WebAppManager",
        "/org/zylos/WebAppManager", "org.zylos.WebAppManager", signal_name);
    if (args) g_dbus_message_set_body(m, args);
    g_dbus_message_set_flags(m, G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    GError *err = NULL;
    if (!g_dbus_connection_send_message(wam_bus, m, G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &err)) {
        wlr_log(WLR_ERROR, "WAM navigation: %s not sent (%s)", signal_name, err ? err->message : "?");
        g_clear_error(&err);
        g_clear_object(&wam_bus);   /* reconnect next time */
    }
    g_object_unref(m);
}
