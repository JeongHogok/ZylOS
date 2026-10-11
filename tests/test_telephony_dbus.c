/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 텔레포니 종단 테스트 — 실제 telephony.c + 사설 버스의 가짜 ModemManager
 *       + PATH 의 가짜 nmcli
 * 수행범위: 재난문자 — 모뎀 채널 설정(SetChannels), CellBroadcast.Added → 분류 →
 *           OS 시그널 EmergencyAlert, 재전송 무시, 여러 조각 메시지 완료 시점
 *           처리, 모뎀 사본 삭제, GetEmergencyAlerts / AcknowledgeEmergencyAlert.
 *           모바일 데이터 — IPv4v6 + CLAT 연결 생성(구버전 NM 폴백),
 *           GetMobileData / SetMobileData (자동 연결·로밍)
 * 의존방향: runtime/services/telephony/telephony.c (main 이름 변경), cbs.c,
 *           gio, dbus-daemon (없으면 SKIP)
 * SOLID: SRP — 모뎀 ↔ 텔레포니 D-Bus 계약 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#define ZYL_ALERTS_FILE "/tmp/zyl-telephony-cbs-test/alerts"
#define FAKE_NM_DIR     "/tmp/zyl-telephony-cbs-test/nm"
#define main telephony_main
#include "../runtime/services/telephony/telephony.c"
#undef main

#define MM_ROOT  "/org/freedesktop/ModemManager1"
#define MODEM    MM_ROOT "/Modem/0"
#define CBM      MM_ROOT "/CBM/1"

static const char *FAKE_MM_XML =
    "<node>"
    "<interface name='org.freedesktop.DBus.ObjectManager'>"
    "  <method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method>"
    "</interface>"
    "<interface name='org.freedesktop.ModemManager1.Modem'/>"
    "<interface name='org.freedesktop.ModemManager1.Modem.CellBroadcast'>"
    "  <method name='SetChannels'><arg name='channels' type='a(uu)' direction='in'/></method>"
    "  <method name='Delete'><arg name='path' type='o' direction='in'/></method>"
    "  <signal name='Added'><arg name='path' type='o'/></signal>"
    "</interface>"
    "<interface name='org.freedesktop.ModemManager1.Cbm'>"
    "  <property name='State' type='u' access='read'/>"
    "  <property name='Text' type='s' access='read'/>"
    "  <property name='Channel' type='u' access='read'/>"
    "  <property name='MessageCode' type='u' access='read'/>"
    "  <property name='Update' type='u' access='read'/>"
    "</interface>"
    "</node>";

/* The fake modem answers from its own thread: the daemon makes blocking
   calls, which would deadlock a fake sharing its main context */
static char    *g_channels;        /* what SetChannels received, printed */
static gint     g_deletes;
static gint     g_cbm_state = 2;   /* RECEIVED */
static int      g_alerts;
static char    *g_last_category;

static void mm_call(GDBusConnection *c, const gchar *sender, const gchar *path,
                    const gchar *iface, const gchar *method, GVariant *params,
                    GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)data;
    if (strcmp(method, "GetManagedObjects") == 0) {
        GVariantBuilder objs, ifaces;
        g_variant_builder_init(&objs, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_add(&ifaces, "{s@a{sv}}", "org.freedesktop.ModemManager1.Modem",
                              g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
        g_variant_builder_add(&objs, "{o@a{sa{sv}}}", MODEM, g_variant_builder_end(&ifaces));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{oa{sa{sv}}})",
                                              g_variant_builder_end(&objs)));
    } else if (strcmp(method, "SetChannels") == 0) {
        g_free(g_channels);
        g_channels = g_variant_print(params, FALSE);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (strcmp(method, "Delete") == 0) {
        g_atomic_int_inc(&g_deletes);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "no");
    }
}

static GVariant *cbm_prop(GDBusConnection *c, const gchar *sender, const gchar *path,
                          const gchar *iface, const gchar *prop, GError **err, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)err; (void)data;
    if (strcmp(prop, "State") == 0) return g_variant_new_uint32((guint32)g_atomic_int_get(&g_cbm_state));
    if (strcmp(prop, "Text") == 0) return g_variant_new_string("대피하십시오");
    if (strcmp(prop, "Channel") == 0) return g_variant_new_uint32(4370);
    if (strcmp(prop, "MessageCode") == 0) return g_variant_new_uint32(5);
    return g_variant_new_uint32(0);   /* Update */
}

static void on_alert(GDBusConnection *c, const gchar *sender, const gchar *path,
                     const gchar *iface, const gchar *sig, GVariant *params, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)sig; (void)data;
    const gchar *category = NULL;
    g_variant_get(params, "(t&sus)", NULL, &category, NULL, NULL);
    g_free(g_last_category);
    g_last_category = g_strdup(category);
    g_alerts++;
}

static void spin(int ms) {
    gint64 end = g_get_monotonic_time() + ms * 1000;
    while (g_get_monotonic_time() < end) g_main_context_iteration(NULL, FALSE);
}

static gboolean wait_for(int *counter, int want) {
    gint64 end = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
    while (*counter < want && g_get_monotonic_time() < end) g_main_context_iteration(NULL, FALSE);
    return *counter >= want;
}

/* The daemon answers on this thread's main context, so call it without blocking it */
static void on_reply(GObject *src, GAsyncResult *res, gpointer out) {
    *(GVariant **)out = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    if (!*(GVariant **)out) *(GVariant **)out = g_variant_ref_sink(g_variant_new("()"));
}

static GVariant *call_os(GDBusConnection *os, const char *method, GVariant *args, const char *type) {
    GVariant *out = NULL;
    g_dbus_connection_call(os, ZYL_TELEPHONY_DBUS_NAME, ZYL_TELEPHONY_DBUS_PATH,
        ZYL_TELEPHONY_DBUS_NAME, method, args, G_VARIANT_TYPE(type),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, on_reply, &out);
    gint64 end = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
    while (!out && g_get_monotonic_time() < end) g_main_context_iteration(NULL, FALSE);
    assert(out && g_variant_is_of_type(out, G_VARIANT_TYPE(type)));
    return out;
}

/* Records every nmcli call and keeps the one connection's state */
static const char *FAKE_NMCLI =
    "#!/bin/sh\n"
    "D=" FAKE_NM_DIR "; echo \"$*\" >> $D/log\n"
    "case \"$*\" in\n"
    "  '-g NAME connection show') cat $D/names 2>/dev/null; exit 0;;\n"
    "  'connection add'*) case \"$*\" in *ipv4.clat*) [ -n \"$FAKE_NO_CLAT\" ] && exit 2;; esac\n"
    "     echo zyl-mobile >> $D/names; echo yes > $D/auto; echo no > $D/home;\n"
    "     case \"$*\" in *'gsm.home-only yes'*) echo yes > $D/home;; esac; exit 0;;\n"
    "  'connection modify zyl-mobile'*) shift 3; while [ $# -gt 1 ]; do\n"
    "     case \"$1\" in connection.autoconnect) echo $2 > $D/auto;; gsm.home-only) echo $2 > $D/home;; esac;\n"
    "     shift 2; done; exit 0;;\n"
    "  '-g connection.autoconnect,gsm.home-only connection show zyl-mobile') cat $D/auto $D/home; exit 0;;\n"
    "  'connection up zyl-mobile'|'connection down zyl-mobile') exit 0;;\n"
    "esac\n"
    "exit 1\n";

static char *nm_log(void) {
    char *log = NULL;
    g_file_get_contents(FAKE_NM_DIR "/log", &log, NULL, NULL);
    return log ? log : g_strdup("");
}

static int count(const char *hay, const char *needle) {
    int n = 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

static void install_fake_nmcli(void) {
    g_mkdir_with_parents(FAKE_NM_DIR, 0755);
    g_assert_true(g_file_set_contents(FAKE_NM_DIR "/nmcli", FAKE_NMCLI, -1, NULL));
    g_chmod(FAKE_NM_DIR "/nmcli", 0755);
    char *path = g_strconcat(FAKE_NM_DIR ":", g_getenv("PATH"), NULL);
    g_setenv("PATH", path, TRUE);
    g_free(path);
}

static void emit_added(GDBusConnection *mm) {
    g_dbus_connection_emit_signal(mm, NULL, MODEM,
        "org.freedesktop.ModemManager1.Modem.CellBroadcast", "Added",
        g_variant_new("(o)", CBM), NULL);
}

static GDBusConnection *g_mm;
static GMainLoop       *g_mm_loop;
static const char      *g_addr;

static gpointer fake_mm_thread(gpointer ready) {
    GMainContext *ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);
    g_mm = g_dbus_connection_new_for_address_sync(g_addr,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(FAKE_MM_XML, NULL);
    static const GDBusInterfaceVTable calls = { mm_call, NULL, NULL, {0} };
    static const GDBusInterfaceVTable props = { NULL, cbm_prop, NULL, {0} };
    g_dbus_connection_register_object(g_mm, MM_ROOT, info->interfaces[0], &calls, NULL, NULL, NULL);
    g_dbus_connection_register_object(g_mm, MODEM, info->interfaces[1], &calls, NULL, NULL, NULL);
    g_dbus_connection_register_object(g_mm, MODEM, info->interfaces[2], &calls, NULL, NULL, NULL);
    g_dbus_connection_register_object(g_mm, CBM, info->interfaces[3], &props, NULL, NULL, NULL);
    GVariant *r = g_dbus_connection_call_sync(g_mm, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.freedesktop.ModemManager1", 0),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_variant_unref(r);
    g_mm_loop = g_main_loop_new(ctx, FALSE);
    g_atomic_int_set((gint *)ready, 1);
    g_main_loop_run(g_mm_loop);
    g_dbus_node_info_unref(info);
    g_main_context_pop_thread_default(ctx);
    return NULL;
}

int main(void) {
    printf("=== Zyl OS Telephony (D-Bus: Cell Broadcast, mobile data) ===\n");
    if (!g_find_program_in_path("dbus-daemon")) {
        printf("  SKIP (no dbus-daemon)\n");
        return 0;
    }
    g_assert_true(system("rm -rf /tmp/zyl-telephony-cbs-test") == 0);
    install_fake_nmcli();

    GTestDBus *bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    /* One private bus plays both the system bus (ModemManager) and the session bus */
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_test_dbus_get_bus_address(bus), TRUE);

    g_addr = g_test_dbus_get_bus_address(bus);
    gint ready = 0;
    GThread *mm_thread = g_thread_new("fake-mm", fake_mm_thread, &ready);
    while (!g_atomic_int_get(&ready)) g_usleep(1000);
    GDBusConnection *mm = g_mm;

    GDBusConnection *os = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    g_dbus_connection_signal_subscribe(os, NULL, ZYL_TELEPHONY_DBUS_NAME, "EmergencyAlert",
        NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_alert, NULL, NULL);

    ZylTelephonyService *svc = zyl_telephony_create();
    assert(svc && svc->modem_found);
    spin(300);   /* name acquisition, SetChannels */

    printf("  %-56s ", "modem is told which channels to listen to");
    assert(g_channels && strcmp(g_channels, "([(4352, 4359), (4370, 4399)],)") == 0);
    printf("PASS\n");

    printf("  %-56s ", "a received alert reaches the OS once");
    emit_added(mm);
    assert(wait_for(&g_alerts, 1));
    assert(strcmp(g_last_category, "presidential") == 0);
    emit_added(mm);                     /* the network repeats it */
    spin(300);
    assert(g_alerts == 1);
    assert(g_atomic_int_get(&g_deletes) >= 1);   /* modem copy removed */
    printf("PASS\n");

    printf("  %-56s ", "history is served and acknowledgement sticks");
    GVariant *list = call_os(os, "GetEmergencyAlerts", NULL, "(aa{sv})");
    GVariant *arr = g_variant_get_child_value(list, 0);
    assert(g_variant_n_children(arr) == 1);
    GVariant *first = g_variant_get_child_value(arr, 0);
    guint64 id = 0;
    const gchar *text = NULL;
    assert(g_variant_lookup(first, "id", "t", &id));
    assert(g_variant_lookup(first, "text", "&s", &text) && strcmp(text, "대피하십시오") == 0);
    GVariant *ack = call_os(os, "AcknowledgeEmergencyAlert", g_variant_new("(t)", id), "(b)");
    gboolean found = FALSE;
    g_variant_get(ack, "(b)", &found);
    assert(found);
    assert(zyl_cbs_store_get(svc->cbs, 0)->acked);
    g_variant_unref(ack);
    g_variant_unref(first);
    g_variant_unref(arr);
    g_variant_unref(list);
    printf("PASS\n");

    printf("  %-56s ", "a multi-page message is handled once complete");
    g_atomic_int_set(&g_cbm_state, 1);  /* RECEIVING: announced, not finished */
    zyl_cbs_store_free(svc->cbs);       /* forget the first alert so this one is new */
    svc->cbs = zyl_cbs_store_open(NULL);
    emit_added(mm);
    spin(300);
    assert(g_alerts == 1);
    g_atomic_int_set(&g_cbm_state, 2);
    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", "State", g_variant_new_uint32(2));
    g_dbus_connection_emit_signal(mm, NULL, CBM, "org.freedesktop.DBus.Properties",
        "PropertiesChanged", g_variant_new("(sa{sv}as)", "org.freedesktop.ModemManager1.Cbm",
        &changed, NULL), NULL);
    assert(wait_for(&g_alerts, 2));
    printf("PASS\n");

    printf("  %-56s ", "mobile data: dual stack, APN from the provider DB, CLAT");
    char *log = nm_log();
    assert(strstr(log, "connection add type gsm ifname * con-name zyl-mobile gsm.auto-config yes gsm.home-only yes "
                       "ipv4.method auto ipv6.method auto ipv6.addr-gen-mode stable-privacy "
                       "ipv6.ip6-privacy 2 ipv4.clat auto"));
    g_free(log);
    printf("PASS\n");

    printf("  %-56s ", "mobile data: NetworkManager < 1.58 falls back without CLAT");
    g_assert_true(system("rm -f " FAKE_NM_DIR "/names " FAKE_NM_DIR "/log") == 0);
    g_setenv("FAKE_NO_CLAT", "1", TRUE);
    ensure_mobile_connection();
    g_unsetenv("FAKE_NO_CLAT");
    log = nm_log();
    char *retry = g_strrstr(log, "\nconnection add");
    assert(count(log, "connection add") == 2 && strstr(log, "ipv4.clat auto"));
    assert(retry && !strstr(retry, "clat") && strstr(retry, "ipv6.method auto"));
    g_free(log);
    ensure_mobile_connection();         /* exists now: not added again */
    log = nm_log();
    assert(count(log, "connection add") == 2);
    g_free(log);
    printf("PASS\n");

    printf("  %-56s ", "mobile data: switch and roaming over D-Bus");
    GVariant *st = call_os(os, "GetMobileData", NULL, "(a{sv})");
    GVariant *dict = g_variant_get_child_value(st, 0);
    gboolean sup = FALSE, en = FALSE, roam = TRUE;
    g_variant_lookup(dict, "supported", "b", &sup);
    g_variant_lookup(dict, "enabled", "b", &en);
    g_variant_lookup(dict, "roaming", "b", &roam);
    assert(sup && en && !roam);         /* created on, roaming off */
    g_variant_unref(dict);
    g_variant_unref(st);
    GVariant *set = call_os(os, "SetMobileData", g_variant_new("(bb)", FALSE, TRUE), "(b)");
    gboolean applied = FALSE;
    g_variant_get(set, "(b)", &applied);
    assert(applied);
    g_variant_unref(set);
    log = nm_log();
    assert(strstr(log, "connection modify zyl-mobile connection.autoconnect no gsm.home-only no"));
    assert(strstr(log, "connection down zyl-mobile"));
    g_free(log);
    bool e2 = true, r2 = false;
    assert(read_mobile_data(&e2, &r2) && !e2 && r2);
    printf("PASS\n");

    zyl_telephony_destroy(svc);
    g_object_unref(os);
    g_main_loop_quit(g_mm_loop);
    g_thread_join(mm_thread);
    g_object_unref(mm);
    g_test_dbus_down(bus);
    g_object_unref(bus);
    printf("\nResults: 7/7 passed\n");
    return 0;
}
