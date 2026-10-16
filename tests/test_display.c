/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 디스플레이 서비스(runtime/services/display) 단위 테스트 (가짜 sysfs)
 * 수행범위: 연결된 출력의 모드(선호 모드 먼저, 끊긴 출력 제외, 외부 화면이 꽂혀도
 *           내장 패널이 주 출력), 출력에 있는 모드만
 *           설정 + ModeChanged, 회전은 0·90·180·270 만 + RotationChanged,
 *           스케일 0.5-4(NaN 거부), 자동 회전 — 0.5 초 유지해야 돌림, 눕히면 그대로,
 *           돌아왔던 기울임은 처음부터, 끄면·다른 센서면 무시, 버스의 SensorEvent
 *           (형식이 틀린 것은 무시)
 * 의존방향: runtime/services/display/display.c (ZYL_TEST), runtime/hal/hal_display.c
 *           (ZYL_SYSFS_ROOT 로 빌드), sensors.h (시그널 이름), gio — 버스 데몬 없이
 *           소켓 쌍의 D-Bus 로 시그널을 주고받음
 * SOLID: SRP — 모드·회전·스케일 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include "../runtime/services/display/display.c"
#include "../runtime/services/sensors/sensors.h"
#include <assert.h>
#include <sys/socket.h>

#define DRM ZYL_SYSFS_ROOT "/sys/class/drm/"
static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(true); fn(); tests_pass++; printf("PASS\n"); } while (0)

static ZylDisplayService *svc;
static GDBusConnection *link_svc, *link_peer;   /* the service's end, and the compositor's */
static GString *heard;

static void put(const char *path, const char *text) {
    char *dir = g_path_get_dirname(path);
    assert(g_mkdir_with_parents(dir, 0755) == 0);
    assert(g_file_set_contents(path, text, -1, NULL));
    g_free(dir);
}

static void on_signal(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                      const gchar *name, GVariant *params, gpointer data) {
    (void)c; (void)sender; (void)data;
    assert(strcmp(path, ZYL_DISPLAY_DBUS_PATH) == 0 && strcmp(iface, ZYL_DISPLAY_DBUS_NAME) == 0);
    char *args = g_variant_print(params, FALSE);
    g_string_append_printf(heard, " %s %s", name, args);
    g_free(args);
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

/* Everything sent either way has arrived and been handled: a round trip
   behind it (Peer.Ping is answered by GDBus itself), then the main loop */
static void settle(void) {
    GVariant *r = g_dbus_connection_call_sync(link_peer, NULL, "/", "org.freedesktop.DBus.Peer", "Ping",
                                              NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    assert(r);
    g_variant_unref(r);
    while (g_main_context_iteration(NULL, FALSE)) ;
}

/* The signals heard since the last look, "" for none */
static bool saw(const char *want) {
    settle();
    const char *was = heard->str + (heard->len > 0);
    bool ok = strcmp(was, want) == 0;
    if (!ok) fprintf(stderr, "\n    heard '%s', want '%s'\n", was, want);
    g_string_truncate(heard, 0);
    return ok;
}

/* An accelerometer reading as the sensor service sends it (m/s², x then y) */
static void accel(int type, double x, double y) {
    GVariant *v = g_variant_ref_sink(g_variant_new("(idddt)", type, x, y, 9.8, (guint64)0));
    on_sensor_event(NULL, NULL, NULL, NULL, NULL, v, svc);
    g_variant_unref(v);
}

static void steady(void) { svc->pending_since_ns -= HYSTERESIS_NS; }   /* half a second passes */

static void reset(bool panel) {
    if (svc) zyl_display_destroy(svc);
    assert(system("rm -rf " ZYL_SYSFS_ROOT) == 0);
    put(DRM "card0-HDMI-A-1/status", "disconnected\n");
    put(DRM "card0-HDMI-A-1/modes", "1920x1080\n");
    put(DRM "card0-DSI-1/status", panel ? "connected\n" : "disconnected\n");
    put(DRM "card0-DSI-1/modes", "1080x2400\n720x1600\n");
    put(DRM "card0/dev", "226:0\n");
    svc = zyl_display_create();
    svc->dbus = link_svc;
    settle();
    g_string_truncate(heard, 0);
}

static void test_the_connected_panel_gives_the_modes_preferred_first(void) {
    assert(svc->mode_count == 2);                          /* not the unplugged HDMI's */
    assert(svc->current_mode.width == 1080 && svc->current_mode.height == 2400);
    assert(svc->current_mode.refresh_hz == 60 && strcmp(svc->current_mode.connector, "DSI-1") == 0);
    assert(svc->scale == 1.0f && svc->rotation == ZYL_ROTATION_0 && svc->auto_rotate);
}

static void test_a_mode_is_set_only_if_the_output_has_it(void) {
    assert(zyl_display_set_mode(svc, 720, 1600, 0) == 0);  /* any rate */
    assert(svc->current_mode.width == 720 && saw("ModeChanged (720, 1600, 60)"));
    assert(zyl_display_set_mode(svc, 1080, 2400, 90) == -1);
    assert(zyl_display_set_mode(svc, 1920, 1080, 0) == -1);   /* the HDMI's, not this output's */
    assert(svc->current_mode.width == 720 && saw(""));
    assert(zyl_display_set_mode(svc, 1080, 2400, 60) == 0 && saw("ModeChanged (1080, 2400, 60)"));
}

static void test_no_connected_output_means_no_modes(void) {
    reset(false);
    assert(svc->mode_count == 0 && svc->current_mode.width == 0);
    assert(zyl_display_set_mode(svc, 1080, 2400, 0) == -1 && saw(""));
}

/* The built-in panel (eDP, DSI, LVDS, DPI) is the screen, whatever order the
   directory lists the outputs in and however many are plugged in; with no
   panel lit, the first connected output */
static void test_the_built_in_panel_wins_over_plugged_in_screens(void) {
    const char *plugged[] = { "HDMI-A-1", "HDMI-A-2", "DP-1", "DP-2", "DVI-D-1" };
    for (size_t i = 0; i < G_N_ELEMENTS(plugged); i++) {
        char *dir = g_strdup_printf(DRM "card0-%s/", plugged[i]), *status = g_strconcat(dir, "status", NULL),
             *modes = g_strconcat(dir, "modes", NULL);
        put(status, "connected\n");
        put(modes, "1920x1080\n");
        g_free(dir); g_free(status); g_free(modes);
    }
    zyl_display_destroy(svc);
    svc = zyl_display_create();
    assert(strcmp(svc->current_mode.connector, "DSI-1") == 0 && svc->current_mode.width == 1080);
    assert(svc->mode_count == 2);

    put(DRM "card0-DSI-1/status", "disconnected\n");
    put(DRM "card1-eDP-1/status", "connected\n");
    put(DRM "card1-eDP-1/modes", "1366x768\n");
    zyl_display_destroy(svc);
    svc = zyl_display_create();
    assert(strcmp(svc->current_mode.connector, "eDP-1") == 0 && svc->current_mode.width == 1366);

    put(DRM "card1-eDP-1/status", "disconnected\n");
    zyl_display_destroy(svc);
    svc = zyl_display_create();
    assert(svc->mode_count == 1 && svc->current_mode.width == 1920);
    assert(strcmp(svc->current_mode.connector, "DSI-1") != 0 && strcmp(svc->current_mode.connector, "eDP-1") != 0);
    svc->dbus = link_svc;
}

static void test_rotation_takes_the_four_right_angles_only(void) {
    assert(zyl_display_set_rotation(svc, ZYL_ROTATION_90) == 0 && saw("RotationChanged (90,)"));
    assert(zyl_display_set_rotation(svc, (ZylRotation)45) == -1);
    assert(zyl_display_set_rotation(svc, (ZylRotation)-90) == -1);
    assert(zyl_display_set_rotation(svc, (ZylRotation)360) == -1);
    assert(svc->rotation == ZYL_ROTATION_90 && saw(""));
    assert(zyl_display_set_rotation(svc, ZYL_ROTATION_270) == 0 && saw("RotationChanged (270,)"));
}

static void test_scale_stays_between_half_and_four(void) {
    assert(zyl_display_set_scale(svc, 2.0f) == 0);
    assert(zyl_display_set_scale(svc, 0.4f) == -1 && zyl_display_set_scale(svc, 4.5f) == -1);
    assert(zyl_display_set_scale(svc, NAN) == -1 && zyl_display_set_scale(svc, INFINITY) == -1);
    assert(svc->scale == 2.0f);
    assert(zyl_display_set_scale(svc, 0.5f) == 0 && zyl_display_set_scale(svc, 4.0f) == 0);
}

static void test_auto_rotate_waits_for_half_a_second_of_steady_tilt(void) {
    accel(0, -9.8, 0);                                     /* turned onto its side */
    accel(0, -9.8, 0);
    assert(svc->rotation == ZYL_ROTATION_0 && saw(""));    /* not yet */
    steady();
    accel(0, -9.8, 0);
    assert(svc->rotation == ZYL_ROTATION_90 && saw("RotationChanged (90,)"));
    accel(0, 0, 9.8);                                      /* upside down */
    steady();
    accel(0, 0, 9.8);
    assert(svc->rotation == ZYL_ROTATION_180 && saw("RotationChanged (180,)"));
}

static void test_lying_flat_keeps_the_rotation(void) {
    zyl_display_set_rotation(svc, ZYL_ROTATION_90);
    saw("RotationChanged (90,)");
    accel(0, 0.3, -0.2);                                   /* on the table: gravity is all z */
    steady();
    accel(0, 0.3, -0.2);
    assert(svc->rotation == ZYL_ROTATION_90 && saw(""));
}

static void test_a_tilt_that_came_back_starts_over(void) {
    accel(0, -9.8, 0);                                     /* a brief tilt... */
    accel(0, 0, -9.8);                                     /* ...and back upright */
    steady();                                              /* long after */
    accel(0, -9.8, 0);                                     /* one new sample is not half a second */
    assert(svc->rotation == ZYL_ROTATION_0 && saw(""));
    steady();
    accel(0, -9.8, 0);
    assert(svc->rotation == ZYL_ROTATION_90 && saw("RotationChanged (90,)"));
}

static void test_auto_rotate_off_or_another_sensor_changes_nothing(void) {
    zyl_display_set_auto_rotate(svc, false);
    accel(0, -9.8, 0);
    steady();
    accel(0, -9.8, 0);
    assert(svc->rotation == ZYL_ROTATION_0);
    zyl_display_set_auto_rotate(svc, true);
    accel(1, -9.8, 0);                                     /* a gyroscope reading */
    steady();
    accel(1, -9.8, 0);
    assert(svc->rotation == ZYL_ROTATION_0 && saw(""));
}

static void sensor_signal(GVariant *args) {
    assert(g_dbus_connection_emit_signal(link_peer, NULL, ZYL_SENSOR_DBUS_PATH, ZYL_SENSOR_DBUS_NAME,
                                         "SensorEvent", args, NULL));
    settle();
}

static void test_sensor_events_on_the_bus_turn_the_screen(void) {
    subscribe_sensor_signals(svc);
    assert(svc->sensor_sub_id);
    sensor_signal(g_variant_new("(i)", 0));                /* the wrong shape: ignored, no crash */
    sensor_signal(g_variant_new("(idddt)", ZYL_SENSOR_ACCELEROMETER, 9.8, 0.0, 0.0, (guint64)1));
    steady();
    sensor_signal(g_variant_new("(idddt)", ZYL_SENSOR_ACCELEROMETER, 9.8, 0.0, 0.0, (guint64)2));
    assert(svc->rotation == ZYL_ROTATION_270 && saw("RotationChanged (270,)"));
}

int main(void) {
    /* No system bus: the service must not own a real name */
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/zyl-display-test", 1);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_log_set_handler(NULL, G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_WARNING, quiet, NULL);
    heard = g_string_new("");
    bus_link(&link_svc, &link_peer);
    g_dbus_connection_signal_subscribe(link_peer, NULL, ZYL_DISPLAY_DBUS_NAME, NULL, NULL, NULL,
                                       G_DBUS_SIGNAL_FLAGS_NONE, on_signal, NULL, NULL);

    printf("=== Display service ===\n");
    RUN_TEST(test_the_connected_panel_gives_the_modes_preferred_first);
    RUN_TEST(test_a_mode_is_set_only_if_the_output_has_it);
    RUN_TEST(test_no_connected_output_means_no_modes);
    RUN_TEST(test_the_built_in_panel_wins_over_plugged_in_screens);
    RUN_TEST(test_rotation_takes_the_four_right_angles_only);
    RUN_TEST(test_scale_stays_between_half_and_four);
    RUN_TEST(test_auto_rotate_waits_for_half_a_second_of_steady_tilt);
    RUN_TEST(test_lying_flat_keeps_the_rotation);
    RUN_TEST(test_a_tilt_that_came_back_starts_over);
    RUN_TEST(test_auto_rotate_off_or_another_sensor_changes_nothing);
    RUN_TEST(test_sensor_events_on_the_bus_turn_the_screen);
    zyl_display_destroy(svc);
    assert(system("rm -rf " ZYL_SYSFS_ROOT) == 0);
    printf("\n%d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
