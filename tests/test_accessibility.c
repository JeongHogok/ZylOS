/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 접근성 서비스(runtime/services/accessibility) D-Bus 단위 테스트
 * 수행범위: 기본 상태, 글꼴 배율 범위(벗어나거나 NaN·무한대면 거부하고 알리지
 *           않음, 양 끝은 허용), 바뀔 때마다 StateChanged 로 전체 상태, 스크린리더
 *           켜기(AT-SPI 레지스트리 + orca), 읽어줄 글은 옵션이 아니라 낱말로만
 *           espeak-ng 에(앱이 준 글이 "-w 경로" 여도), 빈 글은 읽지 않음
 * 의존방향: runtime/services/accessibility/accessibility.c (ZYL_TEST), gio
 *           (소켓 쌍 위의 피어 D-Bus — 버스 데몬 불필요), PATH 의 가짜
 *           espeak-ng·orca, 가짜 at-spi2-registryd
 * SOLID: SRP — 접근성 서비스의 D-Bus 계약 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-accessibility-test"
#define ZYL_ATSPI_REGISTRYD DIR_ "/bin/at-spi2-registryd"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "../runtime/services/accessibility/accessibility.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); fflush(stdout); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static ZylAccessibilityService *svc;
static GDBusConnection *server, *client;   /* the service's end, the caller's end */
static int signals;
static gboolean sig_contrast, sig_reader;
static gdouble sig_scale;

static void reset(void) {
    signals = 0;
    g_assert_true(system("rm -f " DIR_ "/espeak " DIR_ "/orca " DIR_ "/registryd") == 0);
}

static void on_server(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src; (void)data;
    server = g_dbus_connection_new_finish(res, NULL);
}

/* A peer-to-peer bus over a socket pair: no bus daemon */
static void peer_bus(void) {
    int fd[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    GSocket *a = g_socket_new_from_fd(fd[0], NULL), *b = g_socket_new_from_fd(fd[1], NULL);
    gchar *guid = g_dbus_generate_guid();
    g_dbus_connection_new(G_IO_STREAM(g_socket_connection_factory_create_connection(a)), guid,
                          G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER, NULL, NULL, on_server, NULL);
    client = g_dbus_connection_new_sync(G_IO_STREAM(g_socket_connection_factory_create_connection(b)),
                                        NULL, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL, NULL);
    while (!server) g_main_context_iteration(NULL, TRUE);
    assert(client);
    g_free(guid);
}

static GVariant *reply;
static void on_reply(GObject *src, GAsyncResult *res, gpointer done) {
    reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    *(gboolean *)done = TRUE;
}

/* The service answers on this thread's main context: wait by running it */
static GVariant *call(const char *method, GVariant *args) {
    gboolean done = FALSE;
    g_dbus_connection_call(client, NULL, ZYL_ACCESSIBILITY_DBUS_PATH, ZYL_ACCESSIBILITY_DBUS_IFACE,
                           method, args, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_reply, &done);
    while (!done) g_main_context_iteration(NULL, TRUE);
    while (g_main_context_iteration(NULL, FALSE)) {}          /* signals sent before the reply */
    assert(reply);
    return reply;
}

static gboolean call_bool(const char *method, GVariant *args) {
    GVariant *r = call(method, args);
    gboolean b = FALSE;
    g_variant_get(r, "(b)", &b);
    g_variant_unref(r);
    return b;
}

static void state(gboolean *contrast, gdouble *scale, gboolean *reader) {
    GVariant *r = call("GetState", NULL);
    g_variant_get(r, "(bdb)", contrast, scale, reader);
    g_variant_unref(r);
}

static void on_state_changed(GDBusConnection *c, const gchar *sender, const gchar *path,
                             const gchar *iface, const gchar *name, GVariant *params, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)name; (void)data;
    g_variant_get(params, "(bdb)", &sig_contrast, &sig_scale, &sig_reader);
    signals++;
}

static void script(const char *name, const char *body) {
    char *path = g_strdup_printf(DIR_ "/bin/%s", name), *text = g_strdup_printf("#!/bin/sh\n%s\n", body);
    assert(g_file_set_contents(path, text, -1, NULL) && chmod(path, 0755) == 0);
    g_free(path); g_free(text);
}

/* The programs are started detached: wait for what they wrote */
static bool wait_for(const char *path, const char *want) {
    for (int i = 0; i < 500; i++) {
        char *text = NULL;
        bool same = g_file_get_contents(path, &text, NULL, NULL) && strcmp(text, want) == 0;
        g_free(text);
        if (same) return true;
        g_usleep(10000);
    }
    return false;
}

static void test_starts_with_nothing_on_and_normal_text(void) {
    gboolean contrast = TRUE, reader = TRUE;
    gdouble scale = 0;
    state(&contrast, &scale, &reader);
    assert(!contrast && scale == 1.0 && !reader);
}

static void test_font_scale_outside_the_range_is_refused_and_not_announced(void) {
    const gdouble bad[] = { 0.99, 3.01, -1.0, 0.0, NAN, INFINITY, -INFINITY };
    gboolean contrast, reader;
    gdouble before = 0, after = 0;
    state(&contrast, &before, &reader);
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++)
        assert(!call_bool("SetFontScale", g_variant_new("(d)", bad[i])));
    state(&contrast, &after, &reader);
    assert(after == before && signals == 0);
}

static void test_font_scale_range_ends_are_allowed_and_announced(void) {
    assert(call_bool("SetFontScale", g_variant_new("(d)", ZYL_FONT_SCALE_MAX)));
    assert(signals == 1 && sig_scale == ZYL_FONT_SCALE_MAX);
    assert(call_bool("SetFontScale", g_variant_new("(d)", ZYL_FONT_SCALE_MIN)));
    assert(signals == 2 && sig_scale == ZYL_FONT_SCALE_MIN);
}

static void test_every_change_announces_the_whole_state(void) {
    g_variant_unref(call("SetHighContrast", g_variant_new("(b)", TRUE)));
    assert(signals == 1 && sig_contrast && sig_scale == 1.0 && !sig_reader);
    assert(call_bool("SetFontScale", g_variant_new("(d)", 2.0)));
    assert(signals == 2 && sig_contrast && sig_scale == 2.0 && !sig_reader);
    g_variant_unref(call("SetScreenReader", g_variant_new("(b)", FALSE)));
    assert(signals == 3 && sig_contrast && sig_scale == 2.0 && !sig_reader);
    gboolean contrast, reader;
    gdouble scale;
    state(&contrast, &scale, &reader);
    assert(contrast && scale == 2.0 && !reader);
}

static void test_screen_reader_on_starts_the_registry_and_orca(void) {
    g_variant_unref(call("SetScreenReader", g_variant_new("(b)", TRUE)));
    assert(signals == 1 && sig_reader);
    assert(wait_for(DIR_ "/registryd", "started\n"));
    assert(wait_for(DIR_ "/orca", "[--no-setup]\n"));
}

static void test_spoken_text_is_never_taken_as_an_option(void) {
    g_variant_unref(call("Speak", g_variant_new("(s)", "")));  /* nothing to say: nothing started */
    /* An app's text that looks like espeak-ng's "write to this file" */
    g_variant_unref(call("Speak", g_variant_new("(s)", "-w" DIR_ "/out.wav")));
    assert(wait_for(DIR_ "/espeak", "[--][-w" DIR_ "/out.wav]\n"));
    g_variant_unref(call("Speak", g_variant_new("(s)", "hello there")));
    assert(wait_for(DIR_ "/espeak", "[--][-w" DIR_ "/out.wav]\n[--][hello there]\n"));
}

int main(void) {
    printf("=== Zyl OS Accessibility Service Tests ===\n");
    g_assert_true(system("rm -rf " DIR_ " && mkdir -p " DIR_ "/bin") == 0);
    script("espeak-ng", "printf '[%s]' \"$@\" >> " DIR_ "/espeak; echo >> " DIR_ "/espeak");
    script("orca", "printf '[%s]' \"$@\" >> " DIR_ "/orca; echo >> " DIR_ "/orca");
    script("at-spi2-registryd", "echo started >> " DIR_ "/registryd");
    char *path = g_strdup_printf(DIR_ "/bin:%s", g_getenv("PATH"));
    g_setenv("PATH", path, TRUE);
    g_free(path);
    /* No system bus for the service to take a name on: the test is its caller */
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=" DIR_ "/no-bus", TRUE);

    svc = zyl_accessibility_service_create();
    assert(svc);
    peer_bus();
    on_bus_acquired(server, ZYL_ACCESSIBILITY_DBUS_NAME, svc);
    g_dbus_connection_signal_subscribe(client, NULL, ZYL_ACCESSIBILITY_DBUS_IFACE, "StateChanged",
                                       ZYL_ACCESSIBILITY_DBUS_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                       on_state_changed, NULL, NULL);

    RUN_TEST(test_starts_with_nothing_on_and_normal_text);
    RUN_TEST(test_font_scale_outside_the_range_is_refused_and_not_announced);
    RUN_TEST(test_font_scale_range_ends_are_allowed_and_announced);
    RUN_TEST(test_every_change_announces_the_whole_state);
    RUN_TEST(test_screen_reader_on_starts_the_registry_and_orca);
    RUN_TEST(test_spoken_text_is_never_taken_as_an_option);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
