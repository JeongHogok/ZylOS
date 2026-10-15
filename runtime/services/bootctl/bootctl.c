/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 부트로더 잠금 제어 — 소유자가 수정한 소프트웨어를 설치할 수 있는 경로
 *       (GPLv3 §6 Installation Information)를 OS 안에서 연다
 * 수행범위: 잠금 상태·현재 슬롯 조회, "잠금 해제 허용" 설정, 잠금/해제 요청.
 *           요청은 U-Boot 환경에 기록만 한다 — 실제 전환은 다음 부팅 때
 *           부트로더가 기기 앞의 버튼 확인을 받고 사용자 데이터를 지운 뒤에
 *           한다 (board/bpi-f3/u-boot-env.txt)
 * 의존방향: gio/gio.h (D-Bus), hal.h (Boot HAL — 부트로더 환경)
 * SOLID: SRP — 잠금 요청 기록만. 확인·삭제·검증 해제는 부트로더 몫
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <gio/gio.h>
#include "hal.h"

#ifndef ZYL_CMDLINE
#define ZYL_CMDLINE "/proc/cmdline"
#endif

/* The bootloader environment, through the board's Boot HAL */
static char *env_get(const char *name) { return zyl_hal_boot()->env_get(name); }
static bool  env_set(const char *name, const char *value) { return zyl_hal_boot()->env_set(name, value); }

static bool env_is(const char *name, const char *want) {
    char *v = env_get(name);
    bool is = v && strcmp(v, want) == 0;
    free(v);
    return is;
}

static char *booted_slot(void) {
    char *cmdline = NULL;
    char *slot = g_strdup("");
    if (g_file_get_contents(ZYL_CMDLINE, &cmdline, NULL, NULL)) {
        const char *p = strstr(cmdline, "zyl.slot=");
        if (p && (p[9] == 'a' || p[9] == 'b')) {
            g_free(slot);
            slot = g_strndup(p + 9, 1);
        }
    }
    g_free(cmdline);
    return slot;
}

static GVariant *state_variant(void) {
    /* No U-Boot environment (emulator, other boards): nothing to unlock here */
    char *lock_seen = env_get("zyl_lock_seen");
    bool supported = lock_seen != NULL;
    bool unlocked = supported && strcmp(lock_seen, "1") == 0;
    bool pending = supported && !env_is("zyl_unlocked", lock_seen);
    char *slot = booted_slot();
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "supported", g_variant_new_boolean(supported));
    /* What the device is running with now; a request shows up as pending */
    g_variant_builder_add(&b, "{sv}", "unlocked", g_variant_new_boolean(unlocked));
    g_variant_builder_add(&b, "{sv}", "pending", g_variant_new_boolean(pending));
    g_variant_builder_add(&b, "{sv}", "unlockAllowed",
                          g_variant_new_boolean(supported && env_is("zyl_oem_unlock_allowed", "1")));
    g_variant_builder_add(&b, "{sv}", "slot", g_variant_new_string(slot));
    g_free(slot);
    free(lock_seen);
    return g_variant_new("(a{sv})", &b);
}

/* Unlocking needs the Settings permission first; relocking never does */
static bool request_lock_state(bool unlocked) {
    if (unlocked && !env_is("zyl_oem_unlock_allowed", "1")) return false;
    return env_set("zyl_unlocked", unlocked ? "1" : "0");
}

static const char *bootctl_introspection_xml =
    "<node>"
    "  <interface name='org.zylos.BootControl'>"
    "    <method name='GetState'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <method name='SetUnlockAllowed'>"
    "      <arg type='b' name='allowed' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <method name='RequestLockState'>"
    "      <arg type='b' name='unlocked' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void handle_bootctl_method(GDBusConnection *conn, const gchar *sender,
                                  const gchar *path, const gchar *iface,
                                  const gchar *method, GVariant *params,
                                  GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    if (g_strcmp0(method, "GetState") == 0) {
        g_dbus_method_invocation_return_value(inv, state_variant());
    } else if (g_strcmp0(method, "SetUnlockAllowed") == 0) {
        gboolean allowed = FALSE;
        g_variant_get(params, "(b)", &allowed);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)",
            env_set("zyl_oem_unlock_allowed", allowed ? "1" : "0")));
    } else if (g_strcmp0(method, "RequestLockState") == 0) {
        gboolean unlocked = FALSE;
        g_variant_get(params, "(b)", &unlocked);
        bool ok = request_lock_state(unlocked);
        if (ok) g_message("[BootCtl] %s requested; confirmed at next boot",
                          unlocked ? "unlock" : "relock");
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ok));
    }
}

static const GDBusInterfaceVTable bootctl_vtable = { .method_call = handle_bootctl_method };

#ifndef ZYL_TEST
static void on_bootctl_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(bootctl_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0])
        g_dbus_connection_register_object(conn, "/org/zylos/BootControl",
            info->interfaces[0], &bootctl_vtable, NULL, NULL, NULL);
    if (info) g_dbus_node_info_unref(info);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    g_bus_own_name(G_BUS_TYPE_SESSION, "org.zylos.BootControl", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bootctl_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
