/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 저장공간 서비스(runtime/services/storage) + Storage HAL 단위 테스트
 * 수행범위: 데이터 파티션의 statvfs 를 읽음(전체·사용·남은 바이트), 따로 마운트되지
 *           않았으면 / 로 폴백, GetUsage 가 에뮬레이터와 같은 필드
 *           { total, used, available, percent } 와 타입으로 답함(사설 버스),
 *           EraseUserData 는 복구 부팅(/data 가 tmpfs)에서만 지우기 유닛을 시작
 * 의존방향: runtime/services/storage/storage.c (ZYL_TEST), runtime/hal/hal_storage.c,
 *           gio, dbus-daemon (없으면 GetUsage 만 SKIP)
 * SOLID: SRP — 용량 조회 계약 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-storage-test"
/* A link to a tmpfs: a filesystem other than /, so the fallback is visible */
#define ZYL_DATA_MOUNT DIR_ "/data"
#include <assert.h>
#include <unistd.h>
#include "../runtime/hal/hal_storage.c"
#include "../runtime/services/storage/storage.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void reset(void) {
    assert(system("rm -rf " DIR_ " && mkdir -p " DIR_ " && ln -s /dev/shm " ZYL_DATA_MOUNT) == 0);
}

static uint64_t total_of(const char *path) {
    struct statvfs sv;
    assert(statvfs(path, &sv) == 0);
    return (uint64_t)sv.f_blocks * sv.f_frsize;
}

static void test_reads_the_data_partition(void) {
    ZylStorageState st;
    assert(zyl_hal_storage()->get(&st));
    assert(st.total_bytes == total_of("/dev/shm") && st.total_bytes > 0);
    assert(st.used_bytes <= st.total_bytes);
    assert(st.used_bytes + st.available_bytes <= st.total_bytes);
}

static void test_falls_back_to_root_when_data_is_not_mounted(void) {
    assert(unlink(ZYL_DATA_MOUNT) == 0);
    ZylStorageState st;
    assert(zyl_hal_storage()->get(&st));
    assert(st.total_bytes == total_of("/") && st.total_bytes > 0);
    assert(st.used_bytes + st.available_bytes <= st.total_bytes);
}

static GMainLoop *g_loop;

static void on_reply(GObject *src, GAsyncResult *res, gpointer out) {
    *(GVariant **)out = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    g_main_loop_quit(g_loop);
}

/* systemd's manager on the private bus: what StartUnit was asked for */
static char *g_started;
static void systemd_method(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                           const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)method; (void)data;
    const gchar *unit = NULL, *mode = NULL;
    g_variant_get(params, "(&s&s)", &unit, &mode);
    g_free(g_started);
    g_started = g_strdup_printf("%s %s", unit, mode);
    g_dbus_method_invocation_return_value(inv, g_variant_new("(o)", "/org/freedesktop/systemd1/job/1"));
}
static const GDBusInterfaceVTable systemd_vtable = { .method_call = systemd_method };

static gboolean erase(GDBusConnection *c) {
    GVariant *r = NULL;
    g_loop = g_main_loop_new(NULL, FALSE);
    g_dbus_connection_call(c, g_dbus_connection_get_unique_name(c), "/org/zylos/Storage",
        "org.zylos.Storage", "EraseUserData", NULL, G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, on_reply, &r);
    g_main_loop_run(g_loop);
    g_main_loop_unref(g_loop);
    assert(r);
    gboolean started = FALSE;
    g_variant_get(r, "(b)", &started);
    g_variant_unref(r);
    return started;
}

/* The factory reset only on a recovery boot (/data is a tmpfs there — here the
   link to /dev/shm); with the real partition up it erases nothing */
static void test_erase_only_on_a_recovery_boot(void) {
    GTestDBus *bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    GDBusConnection *c = g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(bus),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    GDBusConnection *sd = g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(bus),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    assert(c && sd);
    on_bus(c, NULL, NULL);
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.freedesktop.systemd1.Manager'><method name='StartUnit'>"
        "<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='o' direction='out'/>"
        "</method></interface></node>", NULL);
    g_dbus_connection_register_object(sd, "/org/freedesktop/systemd1", info->interfaces[0],
                                      &systemd_vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
    GVariant *own = g_dbus_connection_call_sync(sd, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.freedesktop.systemd1", 0),
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    assert(own);
    g_variant_unref(own);

    assert(erase(c));
    assert(g_strcmp0(g_started, "zyl-erase-data.service replace") == 0);
    g_clear_pointer(&g_started, g_free);
    assert(unlink(ZYL_DATA_MOUNT) == 0 && symlink("/", ZYL_DATA_MOUNT) == 0);   /* a real /data */
    assert(!erase(c));
    assert(!g_started);

    g_object_unref(sd);
    g_object_unref(c);
    g_test_dbus_down(bus);
    g_object_unref(bus);
}

static void test_get_usage_answers_the_emulator_fields(void) {
    GTestDBus *bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    GDBusConnection *c = g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(bus),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    assert(c);
    on_bus(c, NULL, NULL);
    GVariant *r = NULL;
    g_loop = g_main_loop_new(NULL, FALSE);
    g_dbus_connection_call(c, g_dbus_connection_get_unique_name(c), "/org/zylos/Storage",
        "org.zylos.Storage", "GetUsage", NULL, G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, on_reply, &r);
    g_main_loop_run(g_loop);
    assert(r);
    GVariant *d = g_variant_get_child_value(r, 0);
    guint64 total = 0, used = 0, avail = 0;
    gint32 pct = -1;
    assert(g_variant_lookup(d, "total", "t", &total) && g_variant_lookup(d, "used", "t", &used));
    assert(g_variant_lookup(d, "available", "t", &avail));
    assert(g_variant_lookup(d, "percent", "i", &pct));
    assert(total == total_of("/dev/shm") && used + avail <= total);
    assert(pct == (gint32)(used * 100 / total) && pct >= 0 && pct <= 100);
    g_variant_unref(d);
    g_variant_unref(r);
    g_main_loop_unref(g_loop);
    g_object_unref(c);
    g_test_dbus_down(bus);
    g_object_unref(bus);
}

int main(void) {
    printf("=== Zyl OS Storage Tests ===\n");
    RUN_TEST(test_reads_the_data_partition);
    RUN_TEST(test_falls_back_to_root_when_data_is_not_mounted);
    gchar *daemon = g_find_program_in_path("dbus-daemon");
    if (daemon) {
        RUN_TEST(test_get_usage_answers_the_emulator_fields);
        RUN_TEST(test_erase_only_on_a_recovery_boot);
    } else {
        printf("  %-56s SKIP (no dbus-daemon)\n", "test_get_usage_answers_the_emulator_fields");
        printf("  %-56s SKIP (no dbus-daemon)\n", "test_erase_only_on_a_recovery_boot");
    }
    g_free(daemon);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
