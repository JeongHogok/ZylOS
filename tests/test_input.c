/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 입력 서비스(runtime/services/input) 단위 테스트
 * 수행범위: evdev 키 → ZylHardwareKey 매핑(전원·볼륨·뒤로·홈·메뉴)과
 *           HardwareKeyPressed 시그널 값, 누름만(뗌·자동반복 제외), 다른 키·이벤트
 *           무시, 장치 분리 시 감시 종료, evdev 가 아닌 파일 거부,
 *           가상 키보드 표시·숨김·배열(알려진 배열만)과 D-Bus GetState
 * 의존방향: runtime/services/input/input.c (ZYL_TEST, ZYL_EVDEV_DIR), gio —
 *           버스 데몬 없이 소켓 쌍의 D-Bus 로 시그널을 받고 메서드를 부름
 * SOLID: SRP — 하드웨어 키 전달과 키보드 상태 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-input-test"
#define ZYL_EVDEV_DIR DIR_
#include "../runtime/services/input/input.c"
#include <assert.h>
#include <sys/socket.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static ZylInputService *svc;
static GDBusConnection *link_svc, *link_peer;   /* the service's end, and where WAM listens */
static GString *heard;
static int evdev[2];                            /* a pipe standing in for the event device */

static const char *NAMES[] = { "POWER", "VOLUME_UP", "VOLUME_DOWN", "BACK", "HOME", "MENU" };

static void on_signal(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                      const gchar *name, GVariant *params, gpointer data) {
    (void)c; (void)sender; (void)data;
    assert(strcmp(path, ZYL_INPUT_DBUS_PATH) == 0 && strcmp(iface, ZYL_INPUT_DBUS_IFACE) == 0);
    assert(strcmp(name, "HardwareKeyPressed") == 0);
    gint32 key;
    g_variant_get(params, "(i)", &key);
    assert(key >= ZYL_KEY_POWER && key <= ZYL_KEY_MENU);
    g_string_append_printf(heard, " %s", NAMES[key]);
}

static void quiet(const gchar *d, GLogLevelFlags l, const gchar *m, gpointer u) { (void)d; (void)l; (void)m; (void)u; }

static void keep(GObject *src, GAsyncResult *res, gpointer out) { (void)src; *(GAsyncResult **)out = g_object_ref(res); }

/* Two connected D-Bus ends over a socket pair — no bus daemon */
static void bus_link(GDBusConnection **mine, GDBusConnection **peer) {
    int fd[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    GSocket *s0 = g_socket_new_from_fd(fd[0], NULL), *s1 = g_socket_new_from_fd(fd[1], NULL);
    GSocketConnection *c0 = g_socket_connection_factory_create_connection(s0);
    GSocketConnection *c1 = g_socket_connection_factory_create_connection(s1);
    char *guid = g_dbus_generate_guid();
    GAsyncResult *res = NULL;
    g_dbus_connection_new(G_IO_STREAM(c0), guid, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER,
                          NULL, NULL, keep, &res);
    *peer = g_dbus_connection_new_sync(G_IO_STREAM(c1), NULL,
                                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL, NULL);
    while (!res) g_main_context_iteration(NULL, TRUE);
    *mine = g_dbus_connection_new_finish(res, NULL);
    assert(*mine && *peer);
    g_object_unref(res);
    g_free(guid);
    g_object_unref(c0); g_object_unref(c1); g_object_unref(s0); g_object_unref(s1);
}

/* Everything the service sent has arrived and been handled: a round trip
   behind it (Peer.Ping is answered by GDBus itself), then the main loop */
static void settle(void) {
    GVariant *r = g_dbus_connection_call_sync(link_peer, NULL, "/", "org.freedesktop.DBus.Peer", "Ping",
                                              NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    assert(r);
    g_variant_unref(r);
    while (g_main_context_iteration(NULL, FALSE)) ;
}

/* The keys the shell heard since the last look, "" for none */
static bool saw(const char *want) {
    settle();
    const char *was = heard->str + (heard->len > 0);
    bool ok = strcmp(was, want) == 0;
    if (!ok) fprintf(stderr, "\n    heard '%s', want '%s'\n", was, want);
    g_string_truncate(heard, 0);
    return ok;
}

/* One event from the device, as the kernel hands it over */
static void event(int type, int code, int value) {
    struct input_event ev = { .type = type, .code = code, .value = value };
    assert(write(evdev[1], &ev, sizeof(ev)) == (ssize_t)sizeof(ev));
    assert(on_evdev_event(NULL, G_IO_IN, svc) == G_SOURCE_CONTINUE);
}

/* A method call from the bus, answered by the service on the main loop */
static GVariant *call(const char *method, GVariant *args) {
    GAsyncResult *res = NULL;
    g_dbus_connection_call(link_peer, NULL, ZYL_INPUT_DBUS_PATH, ZYL_INPUT_DBUS_IFACE, method, args,
                           NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, keep, &res);
    while (!res) g_main_context_iteration(NULL, TRUE);
    GVariant *r = g_dbus_connection_call_finish(link_peer, res, NULL);
    g_object_unref(res);
    return r;
}

static void reset(void) {
    if (svc) zyl_input_destroy(svc);              /* closes the pipe's read end */
    if (evdev[1] > 0) close(evdev[1]);
    assert(system("rm -rf " DIR_) == 0);
    svc = zyl_input_create();
    assert(svc && svc->evdev_fd == -1);           /* no device here */
    on_bus_acquired(link_svc, ZYL_INPUT_DBUS_NAME, svc);
    assert(svc->registration_id);
    assert(pipe(evdev) == 0);
    svc->evdev_fd = evdev[0];
    settle();
    g_string_truncate(heard, 0);
}

static void test_each_hardware_key_reaches_the_shell_as_its_code(void) {
    event(EV_KEY, KEY_POWER, 1);
    event(EV_KEY, KEY_VOLUMEUP, 1);
    event(EV_KEY, KEY_VOLUMEDOWN, 1);
    event(EV_KEY, KEY_BACK, 1);
    event(EV_KEY, KEY_HOMEPAGE, 1);
    event(EV_KEY, KEY_MENU, 1);
    assert(saw("POWER VOLUME_UP VOLUME_DOWN BACK HOME MENU"));
}

static void test_only_the_press_counts_not_release_or_repeat(void) {
    event(EV_KEY, KEY_POWER, 1);
    event(EV_KEY, KEY_POWER, 2);                  /* held: autorepeat */
    event(EV_KEY, KEY_POWER, 2);
    event(EV_KEY, KEY_POWER, 0);                  /* released */
    event(EV_SYN, SYN_REPORT, 0);
    assert(saw("POWER"));                         /* one press, one screen toggle */
}

static void test_other_keys_and_events_are_not_hardware_keys(void) {
    event(EV_KEY, KEY_A, 1);
    event(EV_KEY, KEY_HOME, 1);                   /* a keyboard's Home (line start), not the home button */
    event(EV_KEY, BTN_TOUCH, 1);
    event(EV_ABS, KEY_POWER, 1);                  /* same number, not a key */
    event(EV_MSC, MSC_SCAN, KEY_POWER);
    assert(saw(""));
}

static void test_an_unplugged_device_stops_being_watched(void) {
    svc->evdev_watch_id = 1;
    assert(on_evdev_event(NULL, G_IO_HUP, svc) == G_SOURCE_REMOVE);
    assert(svc->evdev_watch_id == 0);
    assert(saw(""));
}

static void test_a_file_that_is_not_an_event_device_is_not_opened(void) {
    assert(g_mkdir_with_parents(DIR_, 0755) == 0);
    assert(g_file_set_contents(DIR_ "/event0", "not a device", -1, NULL));
    ZylInputService *other = zyl_input_create();
    assert(other && other->evdev_fd == -1 && other->evdev_watch_id == 0);
    zyl_input_on_hardware_key(other, ZYL_KEY_POWER);   /* no bus yet: nothing sent, no crash */
    zyl_input_destroy(other);
    assert(saw(""));
}

static void test_keyboard_shows_hides_and_keeps_a_known_layout(void) {
    ZylKeyboardState k = zyl_input_get_keyboard_state(svc);
    assert(!k.visible && strcmp(k.layout, "en") == 0);
    assert(zyl_input_show_keyboard(svc, "ko") == 0);
    k = zyl_input_get_keyboard_state(svc);
    assert(k.visible && strcmp(k.layout, "ko") == 0);
    assert(zyl_input_show_keyboard(svc, "klingon") == 0);      /* shown, layout unchanged */
    assert(strcmp(zyl_input_get_keyboard_state(svc).layout, "ko") == 0);
    assert(zyl_input_hide_keyboard(svc) == 0);
    k = zyl_input_get_keyboard_state(svc);
    assert(!k.visible && strcmp(k.layout, "ko") == 0);
    assert(zyl_input_switch_layout(svc, "ar") == 0);
    assert(zyl_input_switch_layout(svc, "xx") == -1 && zyl_input_switch_layout(svc, "") == -1);
    assert(zyl_input_switch_layout(svc, NULL) == -1);
    assert(strcmp(zyl_input_get_keyboard_state(svc).layout, "ar") == 0);
    assert(zyl_input_show_keyboard(NULL, "en") == -1 && zyl_input_hide_keyboard(NULL) == -1);
    assert(!zyl_input_get_keyboard_state(NULL).visible);
}

static void test_the_bus_shows_and_reports_the_keyboard(void) {
    gboolean ok = FALSE, visible = FALSE;
    const char *layout = NULL;
    GVariant *r = call("ShowKeyboard", g_variant_new("(s)", "ja"));
    g_variant_get(r, "(b)", &ok);
    assert(ok);
    g_variant_unref(r);
    r = call("SwitchLayout", g_variant_new("(s)", "nope"));
    g_variant_get(r, "(b)", &ok);
    assert(!ok);
    g_variant_unref(r);
    r = call("GetState", NULL);
    g_variant_get(r, "(b&s)", &visible, &layout);
    assert(visible && strcmp(layout, "ja") == 0);
    g_variant_unref(r);
    assert(call("Reboot", NULL) == NULL);                       /* not a method here */
}

int main(void) {
    /* No system bus: the service must not own a real name */
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/zyl-input-test", 1);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_log_set_handler(NULL, G_LOG_LEVEL_WARNING, quiet, NULL);   /* "lost D-Bus name": no bus here */
    heard = g_string_new("");
    bus_link(&link_svc, &link_peer);
    g_dbus_connection_signal_subscribe(link_peer, NULL, NULL, NULL, NULL, NULL,
                                       G_DBUS_SIGNAL_FLAGS_NONE, on_signal, NULL, NULL);

    printf("=== Input service ===\n");
    RUN_TEST(test_each_hardware_key_reaches_the_shell_as_its_code);
    RUN_TEST(test_only_the_press_counts_not_release_or_repeat);
    RUN_TEST(test_other_keys_and_events_are_not_hardware_keys);
    RUN_TEST(test_an_unplugged_device_stops_being_watched);
    RUN_TEST(test_a_file_that_is_not_an_event_device_is_not_opened);
    RUN_TEST(test_keyboard_shows_hides_and_keeps_a_known_layout);
    RUN_TEST(test_the_bus_shows_and_reports_the_keyboard);
    zyl_input_destroy(svc);
    close(evdev[1]);
    assert(system("rm -rf " DIR_) == 0);
    printf("\n%d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
