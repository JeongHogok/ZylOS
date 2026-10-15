/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 기기 정보 서비스 (org.zylos.Device) — 설정 > 이 기기 정보
 * 수행범위: 모델·SoC·메모리·커널(Board HAL), OS 버전·보안 패치 수준(이미지의
 *           release — meson 이 apps/system/release.js 에서), 화면 해상도(Display
 *           HAL), 호스트 이름, 가동 시간
 * 의존방향: hal.h (Board·Display HAL), gio/gio.h (D-Bus)
 * SOLID: SRP — 기기 정보 조회만
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <gio/gio.h>
#include <glib-unix.h>

#ifndef ZYL_OS_VERSION
#define ZYL_OS_VERSION "unknown"
#endif
#ifndef ZYL_SECURITY_PATCH
#define ZYL_SECURITY_PATCH "unknown"
#endif

static const char *device_xml =
    "<node>"
    "  <interface name='org.zylos.Device'>"
    "    <method name='GetInfo'>"
    "      <arg type='a{sv}' name='info' direction='out'/>"
    "    </method>"
    "    <method name='GetUptime'>"
    "      <arg type='x' name='seconds' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

/* Same fields as the emulator's device service */
static GVariant *info_variant(void) {
    ZylBoardInfo b;
    zyl_hal_board()->get(&b);
    char ram[32] = "", kernel[128] = "", resolution[32] = "", host[64] = "";
    if (b.ram_bytes) snprintf(ram, sizeof(ram), "%.1f GB", (double)b.ram_bytes / (1024.0 * 1024 * 1024));
    if (b.kernel[0]) snprintf(kernel, sizeof(kernel), "Linux %s", b.kernel);
    ZylDisplayOutput outs[4];
    int n = zyl_hal_display()->list_outputs(outs, 4);
    for (int i = 0; i < n && !resolution[0]; i++)
        if (outs[i].connected && outs[i].mode_count)
            snprintf(resolution, sizeof(resolution), "%dx%d", outs[i].modes[0].width, outs[i].modes[0].height);
    gethostname(host, sizeof(host) - 1);
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "deviceName", g_variant_new_string(b.model));
    g_variant_builder_add(&d, "{sv}", "osVersion", g_variant_new_string(ZYL_OS_VERSION));
    g_variant_builder_add(&d, "{sv}", "securityPatch", g_variant_new_string(ZYL_SECURITY_PATCH));
    g_variant_builder_add(&d, "{sv}", "soc", g_variant_new_string(b.soc));
    g_variant_builder_add(&d, "{sv}", "ram", g_variant_new_string(ram));
    g_variant_builder_add(&d, "{sv}", "kernel", g_variant_new_string(kernel));
    g_variant_builder_add(&d, "{sv}", "build", g_variant_new_string(ZYL_OS_VERSION));
    g_variant_builder_add(&d, "{sv}", "resolution", g_variant_new_string(resolution));
    g_variant_builder_add(&d, "{sv}", "hostname", g_variant_new_string(host));
    return g_variant_new("(a{sv})", &d);
}

static void handle(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)params; (void)data;
    if (g_strcmp0(method, "GetInfo") == 0) {
        g_dbus_method_invocation_return_value(inv, info_variant());
    } else if (g_strcmp0(method, "GetUptime") == 0) {
        struct sysinfo si;
        gint64 up = sysinfo(&si) == 0 ? si.uptime : 0;
        g_dbus_method_invocation_return_value(inv, g_variant_new("(x)", up));
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(device_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/Device", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SESSION, "org.zylos.Device", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
