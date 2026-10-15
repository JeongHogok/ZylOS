/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: Bluetooth HAL Linux 구현(runtime/hal/hal_bt.c) 종단 테스트 — 사설 버스의
 *       가짜 BlueZ
 * 수행범위: 어댑터를 찾아 씀(hci0 고정 아님), 기기 목록(이름 없으면 주소),
 *           전원 켜고 끄기, 페어링이 그 어댑터 아래 기기 경로로, 어댑터가 없으면
 *           블루투스 없음
 * 의존방향: hal_bt.c, gio, dbus-daemon (없으면 SKIP)
 * SOLID: SRP — 블루투스 장치 접근 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include "../runtime/hal/hal_bt.c"
#include <assert.h>

#define ADAPTER "/org/bluez/hci1"
#define DEV1    ADAPTER "/dev_AA_BB_CC_DD_EE_01"
#define DEV2    ADAPTER "/dev_AA_BB_CC_DD_EE_02"

static const char *FAKE_BLUEZ =
    "<node>"
    "<interface name='org.freedesktop.DBus.ObjectManager'>"
    "  <method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method>"
    "</interface>"
    "<interface name='org.bluez.Adapter1'>"
    "  <method name='StartDiscovery'/><method name='StopDiscovery'/>"
    "  <method name='RemoveDevice'><arg type='o' direction='in'/></method>"
    "  <property name='Powered' type='b' access='readwrite'/>"
    "</interface>"
    "<interface name='org.bluez.Device1'>"
    "  <method name='Pair'/><method name='Connect'/><method name='Disconnect'/>"
    "</interface>"
    "</node>";

static gint g_no_adapter, g_powered = 1;
static GMutex g_lock;
static GString *g_calls;

static void note(const char *fmt, const char *a, const char *b) {
    g_mutex_lock(&g_lock);
    g_string_append_printf(g_calls, fmt, a, b);
    g_mutex_unlock(&g_lock);
}

static GVariant *dict(const char *k1, GVariant *v1, ...) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    va_list ap;
    va_start(ap, v1);
    for (const char *k = k1; k; k = va_arg(ap, const char *)) {
        GVariant *v = k == k1 ? v1 : va_arg(ap, GVariant *);
        g_variant_builder_add(&b, "{sv}", k, v);
    }
    va_end(ap);
    return g_variant_builder_end(&b);
}

static void method(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *name, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)iface; (void)params; (void)data;
    if (strcmp(name, "GetManagedObjects") == 0) {
        GVariantBuilder objs;
        g_variant_builder_init(&objs, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        if (!g_atomic_int_get(&g_no_adapter)) {
            GVariantBuilder i;
            g_variant_builder_init(&i, G_VARIANT_TYPE("a{sa{sv}}"));
            g_variant_builder_add(&i, "{s@a{sv}}", "org.bluez.Adapter1",
                                  dict("Powered", g_variant_new_boolean(g_atomic_int_get(&g_powered)), NULL));
            g_variant_builder_add(&objs, "{o@a{sa{sv}}}", ADAPTER, g_variant_builder_end(&i));
        }
        GVariantBuilder d1, d2;
        g_variant_builder_init(&d1, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_add(&d1, "{s@a{sv}}", "org.bluez.Device1",
            dict("Address", g_variant_new_string("AA:BB:CC:DD:EE:01"), "Name", g_variant_new_string("Ana's Buds"),
                 "Icon", g_variant_new_string("audio-headphones"), "Paired", g_variant_new_boolean(TRUE),
                 "Connected", g_variant_new_boolean(FALSE), "RSSI", g_variant_new_int16(-55), NULL));
        g_variant_builder_add(&objs, "{o@a{sa{sv}}}", DEV1, g_variant_builder_end(&d1));
        g_variant_builder_init(&d2, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_add(&d2, "{s@a{sv}}", "org.bluez.Device1",
            dict("Address", g_variant_new_string("AA:BB:CC:DD:EE:02"), NULL));
        g_variant_builder_add(&objs, "{o@a{sa{sv}}}", DEV2, g_variant_builder_end(&d2));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{oa{sa{sv}}})", g_variant_builder_end(&objs)));
        return;
    }
    note("%s %s\n", name, path);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static GVariant *get_prop(GDBusConnection *c, const gchar *s, const gchar *p, const gchar *i,
                          const gchar *prop, GError **e, gpointer d) {
    (void)c; (void)s; (void)p; (void)i; (void)prop; (void)e; (void)d;
    return g_variant_new_boolean(g_atomic_int_get(&g_powered));
}

static gboolean set_prop(GDBusConnection *c, const gchar *s, const gchar *p, const gchar *i,
                         const gchar *prop, GVariant *v, GError **e, gpointer d) {
    (void)c; (void)s; (void)i; (void)e; (void)d;
    g_atomic_int_set(&g_powered, g_variant_get_boolean(v));
    note("Set %s %s\n", prop, g_variant_get_boolean(v) ? "on" : "off");
    (void)p;
    return TRUE;
}

static const char *g_addr;
static GMainLoop *g_loop;
static gpointer fake_thread(gpointer ready) {
    GMainContext *ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);
    GDBusConnection *c = g_dbus_connection_new_for_address_sync(g_addr,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(FAKE_BLUEZ, NULL);
    static const GDBusInterfaceVTable calls = { method, NULL, NULL, {0} };
    static const GDBusInterfaceVTable adapter = { method, get_prop, set_prop, {0} };
    g_dbus_connection_register_object(c, "/", info->interfaces[0], &calls, NULL, NULL, NULL);
    g_dbus_connection_register_object(c, ADAPTER, info->interfaces[1], &adapter, NULL, NULL, NULL);
    g_dbus_connection_register_object(c, DEV1, info->interfaces[2], &calls, NULL, NULL, NULL);
    GVariant *r = g_dbus_connection_call_sync(c, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.bluez", 0),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_variant_unref(r);
    g_loop = g_main_loop_new(ctx, FALSE);
    g_atomic_int_set((gint *)ready, 1);
    g_main_loop_run(g_loop);
    g_object_unref(c);
    g_dbus_node_info_unref(info);
    return NULL;
}

int main(void) {
    printf("=== Bluetooth HAL (BlueZ) ===\n");
    if (!g_find_program_in_path("dbus-daemon")) { printf("  SKIP (no dbus-daemon)\n"); return 0; }
    g_calls = g_string_new("");
    GTestDBus *tb = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(tb);
    g_addr = g_test_dbus_get_bus_address(tb);
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", g_addr, TRUE);
    gint ready = 0;
    GThread *t = g_thread_new("fake-bluez", fake_thread, &ready);
    while (!g_atomic_int_get(&ready)) g_usleep(1000);
    const ZylBtHal *bt = zyl_hal_bt();

    printf("  %-56s ", "the adapter BlueZ has is found (hci1, not hci0)");
    assert(bt->present() && bt->get_enabled());
    printf("PASS\n");

    printf("  %-56s ", "devices listed; one with no name shows its address");
    ZylBtDevice d[8];
    assert(bt->list_devices(d, 8) == 2);
    assert(strcmp(d[0].name, "Ana's Buds") == 0 && d[0].paired && d[0].rssi == -55);
    assert(strcmp(d[1].name, "AA:BB:CC:DD:EE:02") == 0 && !d[1].paired);
    printf("PASS\n");

    printf("  %-56s ", "power and pairing reach the right objects");
    assert(bt->set_enabled(false) && !bt->get_enabled());
    assert(bt->pair("AA:BB:CC:DD:EE:01") && !bt->pair("not-an-address"));
    g_mutex_lock(&g_lock);
    assert(strstr(g_calls->str, "Set Powered off\n") && strstr(g_calls->str, "Pair " DEV1 "\n"));
    g_mutex_unlock(&g_lock);
    printf("PASS\n");

    printf("  %-56s ", "no adapter: no Bluetooth");
    g_atomic_int_set(&g_no_adapter, 1);
    assert(!bt->present() && !bt->set_enabled(true));
    printf("PASS\n");

    g_main_loop_quit(g_loop);
    g_thread_join(t);
    g_test_dbus_down(tb);
    g_object_unref(tb);
    printf("\nResults: 4/4 passed\n");
    return 0;
}
