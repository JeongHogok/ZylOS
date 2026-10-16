/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 입력 서비스(runtime/services/input) 단위 테스트
 * 수행범위: evdev 키 → ZylHardwareKey 매핑(전원·볼륨·뒤로·홈·메뉴)과
 *           HardwareKeyPressed 시그널 값, 누름만(뗌·자동반복 제외), 다른 키·이벤트
 *           무시, 전원·볼륨 키가 다른 장치여도 모두 도착, 빠진 장치만 닫고 나머지는
 *           계속, 터치스크린·evdev 가 아닌 파일은 열지 않음, 감시가 깨지면 종료,
 *           가상 키보드 표시·숨김·배열(알려진 배열만)과 D-Bus GetState
 * 의존방향: runtime/services/input/input.c (ZYL_TEST), runtime/hal/hal_input.c
 *           (ZYL_DEV_ROOT, ioctl 을 가짜 evdev 드라이버로 — 장치 노드는 FIFO), gio —
 *           버스 데몬 없이 소켓 쌍의 D-Bus 로 시그널을 받고 메서드를 부름
 * SOLID: SRP — 하드웨어 키 전달과 키보드 상태 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#define DIR_ "/tmp/zyl-input-test"
#define ZYL_DEV_ROOT DIR_
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/input.h>

/* A fake evdev driver behind the HAL's ioctl: /dev/input/event<N> is a FIFO the
   test writes events into, and it has the keys caps[N] lists */
static int fake_ioctl(int fd, unsigned long req, ...);
#define ioctl fake_ioctl
#include "../runtime/hal/hal_input.c"
#include "../runtime/services/input/input.c"

#define DEVICES 8
static const int *caps[DEVICES];
static int writer[DEVICES] = { -1, -1, -1, -1, -1, -1, -1, -1 };   /* the kernel's end of each */

static const int ALL_KEYS[] = { KEY_POWER, KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_BACK, KEY_HOMEPAGE,
                                KEY_MENU, KEY_A, KEY_HOME, -1 };
static const int POWER_KEY[] = { KEY_POWER, -1 };                    /* the PMIC's power key */
static const int VOLUME_KEYS[] = { KEY_VOLUMEUP, KEY_VOLUMEDOWN, -1 };   /* adc-keys */
static const int TOUCHSCREEN[] = { BTN_TOUCH, -1 };

static int fake_ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    unsigned long *bits = va_arg(ap, unsigned long *);
    va_end(ap);
    char link[64], path[256] = "";
    struct stat st;
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    const char *name = readlink(link, path, sizeof(path) - 1) > 0 ? strstr(path, "/event") : NULL;
    if (fstat(fd, &st) != 0 || !S_ISFIFO(st.st_mode) || !name) { errno = ENOTTY; return -1; }
    assert(_IOC_NR(req) == 0x20 + EV_KEY);                 /* the HAL asks for the key bitmap only */
    memset(bits, 0, _IOC_SIZE(req));
    for (const int *k = caps[atoi(name + 6)]; k && *k >= 0; k++)
        bits[*k / LONG_BITS] |= 1UL << (*k % LONG_BITS);
    return (int)_IOC_SIZE(req);
}

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static ZylInputService *svc;
static GDBusConnection *link_svc, *link_peer;   /* the service's end, and where WAM listens */
static GString *heard;

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

/* One event from device n, as the kernel hands it over */
static void event(int n, int type, int code, int value) {
    struct input_event ev = { .type = type, .code = code, .value = value };
    assert(write(writer[n], &ev, sizeof(ev)) == (ssize_t)sizeof(ev));
    assert(on_keys_ready(svc->keys_fd, G_IO_IN, svc) == G_SOURCE_CONTINUE);
}

/* -1 with ENXIO when nobody has the device open: the HAL left it or closed it */
static int kernel_end(int n) {
    char *path = g_strdup_printf(DIR_ "/dev/input/event%d", n);
    int fd = open(path, O_WRONLY | O_NONBLOCK);
    g_free(path);
    return fd;
}

static void plug(int n, const int *keys) {
    char *path = g_strdup_printf(DIR_ "/dev/input/event%d", n);
    assert(g_mkdir_with_parents(DIR_ "/dev/input", 0755) == 0 && mkfifo(path, 0600) == 0);
    caps[n] = keys;
    g_free(path);
}

/* The service with the devices plugged so far */
static void start(void) {
    svc = zyl_input_create();
    assert(svc);
    on_bus_acquired(link_svc, ZYL_INPUT_DBUS_NAME, svc);
    assert(svc->registration_id);
    for (int i = 0; i < DEVICES; i++)
        if (caps[i]) writer[i] = kernel_end(i);
    settle();
    g_string_truncate(heard, 0);
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
    if (svc) zyl_input_destroy(svc);              /* closes the HAL's ends first */
    svc = NULL;
    for (int i = 0; i < DEVICES; i++) {
        if (writer[i] >= 0) close(writer[i]);
        writer[i] = -1;
        caps[i] = NULL;
    }
    assert(system("rm -rf " DIR_) == 0);
}

static void test_each_hardware_key_reaches_the_shell_as_its_code(void) {
    plug(0, ALL_KEYS);
    start();
    event(0, EV_KEY, KEY_POWER, 1);
    event(0, EV_KEY, KEY_VOLUMEUP, 1);
    event(0, EV_KEY, KEY_VOLUMEDOWN, 1);
    event(0, EV_KEY, KEY_BACK, 1);
    event(0, EV_KEY, KEY_HOMEPAGE, 1);
    event(0, EV_KEY, KEY_MENU, 1);
    assert(saw("POWER VOLUME_UP VOLUME_DOWN BACK HOME MENU"));
}

static void test_only_the_press_counts_not_release_or_repeat(void) {
    plug(0, ALL_KEYS);
    start();
    event(0, EV_KEY, KEY_POWER, 1);
    event(0, EV_KEY, KEY_POWER, 2);               /* held: autorepeat */
    event(0, EV_KEY, KEY_POWER, 2);
    event(0, EV_KEY, KEY_POWER, 0);               /* released */
    event(0, EV_SYN, SYN_REPORT, 0);
    assert(saw("POWER"));                         /* one press, one screen toggle */
}

static void test_other_keys_and_events_are_not_hardware_keys(void) {
    plug(0, ALL_KEYS);
    start();
    event(0, EV_KEY, KEY_A, 1);
    event(0, EV_KEY, KEY_HOME, 1);                /* a keyboard's Home (line start), not the home button */
    event(0, EV_KEY, BTN_TOUCH, 1);
    event(0, EV_ABS, KEY_POWER, 1);               /* same number, not a key */
    event(0, EV_MSC, MSC_SCAN, KEY_POWER);
    assert(saw(""));
}

static void test_keys_on_separate_devices_all_arrive(void) {
    /* A phone: the power key, the volume keys and the touchscreen are three devices */
    plug(0, POWER_KEY);
    plug(1, VOLUME_KEYS);
    plug(2, TOUCHSCREEN);
    start();
    assert(writer[0] >= 0 && writer[1] >= 0);
    assert(kernel_end(2) == -1 && errno == ENXIO);   /* no keys of ours: not kept open */
    event(1, EV_KEY, KEY_VOLUMEUP, 1);
    event(0, EV_KEY, KEY_POWER, 1);
    event(1, EV_KEY, KEY_VOLUMEDOWN, 1);
    assert(saw("VOLUME_UP POWER VOLUME_DOWN"));
}

static void test_an_unplugged_device_goes_and_the_others_keep_working(void) {
    plug(0, POWER_KEY);
    plug(1, VOLUME_KEYS);
    start();
    close(writer[0]);                             /* event0 hangs up */
    writer[0] = -1;
    event(1, EV_KEY, KEY_VOLUMEUP, 1);
    assert(saw("VOLUME_UP"));
    assert(kernel_end(0) == -1 && errno == ENXIO);   /* the HAL closed it */
    event(1, EV_KEY, KEY_VOLUMEDOWN, 1);
    assert(saw("VOLUME_DOWN"));
}

static void test_a_broken_watch_is_dropped(void) {
    plug(0, POWER_KEY);
    start();
    g_source_remove(svc->keys_watch_id);          /* the loop's own source: called by hand below */
    assert(on_keys_ready(svc->keys_fd, G_IO_HUP, svc) == G_SOURCE_REMOVE);
    assert(svc->keys_watch_id == 0);
    assert(saw(""));
}

static void test_files_and_keyless_devices_are_not_opened(void) {
    assert(g_mkdir_with_parents(DIR_ "/dev/input", 0755) == 0);
    assert(g_file_set_contents(DIR_ "/dev/input/event0", "not a device", -1, NULL));
    plug(1, TOUCHSCREEN);
    start();
    assert(svc->keys_fd == -1 && svc->keys_watch_id == 0);
    assert(kernel_end(1) == -1 && errno == ENXIO);
    zyl_input_destroy(svc);
    svc = zyl_input_create();                     /* no bus yet: nothing sent, no crash */
    zyl_input_on_hardware_key(svc, ZYL_KEY_POWER);
    assert(saw(""));
}

static void test_keyboard_shows_hides_and_keeps_a_known_layout(void) {
    start();
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
    start();
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
    RUN_TEST(test_keys_on_separate_devices_all_arrive);
    RUN_TEST(test_an_unplugged_device_goes_and_the_others_keep_working);
    RUN_TEST(test_a_broken_watch_is_dropped);
    RUN_TEST(test_files_and_keyless_devices_are_not_opened);
    RUN_TEST(test_keyboard_shows_hides_and_keeps_a_known_layout);
    RUN_TEST(test_the_bus_shows_and_reports_the_keyboard);
    reset();
    printf("\n%d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
