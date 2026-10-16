/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 전력 관리 서비스(runtime/services/power) 단위 테스트 (가짜 sysfs)
 * 수행범위: 화면 끄기·켜기와 사용자 밝기 복원, 대기 → 30% 어둡게 → 꺼짐,
 *           어둡게가 밝아지지 않음, 밝기 0-100 고정과 꺼진 동안 보류,
 *           웨이크락(화면 유지·서스펜드 차단·태그당 하나·32개 한도·10분 만료·
 *           마지막 해제 후 타이머 재개), 도즈(powersave·코어 절반 끄기) → 서스펜드
 *           → 떠난 상태로 복귀, StateChanged(이전, 새) 값, GetState, 설정의 저장 밝기
 * 의존방향: runtime/services/power/power{,_cpu,_display,_wakelock}.c (ZYL_TEST),
 *           runtime/hal/hal_{display,cpu,battery}.c (ZYL_SYSFS_ROOT 로 빌드),
 *           gio — 버스 데몬 없이 소켓 쌍의 D-Bus 로 시그널을 받음
 * SOLID: SRP — 전원 상태 기계와 밝기·웨이크락 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include "../runtime/services/power/power.c"
#include <assert.h>
#include <sys/socket.h>

#define R   ZYL_SYSFS_ROOT
#define BL  R "/sys/class/backlight/panel/"
#define CPU R "/sys/devices/system/cpu/"
static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static ZylPowerService *svc;
static GDBusConnection *link_svc, *link_peer;   /* the service's end, and where WAM listens */
static GString *heard;

static void put(const char *path, const char *text) {
    char *dir = g_path_get_dirname(path);
    assert(g_mkdir_with_parents(dir, 0755) == 0);
    assert(g_file_set_contents(path, text, -1, NULL));
    g_free(dir);
}

static char *got(const char *path) {
    static char buf[64];
    char *t = NULL;
    assert(g_file_get_contents(path, &t, NULL, NULL));
    g_strlcpy(buf, g_strstrip(t), sizeof(buf));
    g_free(t);
    return buf;
}

static int shown(void) { return atoi(got(BL "brightness")); }   /* max_brightness 100: raw = % */

static const char *NAMES[] = { "ACTIVE", "DIM", "OFF", "DOZE", "SUSPEND", "SHUTDOWN" };

static void on_signal(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                      const gchar *name, GVariant *params, gpointer data) {
    (void)c; (void)sender; (void)data;
    assert(strcmp(path, ZYL_POWER_DBUS_PATH) == 0 && strcmp(iface, ZYL_POWER_DBUS_NAME) == 0);
    if (strcmp(name, "StateChanged") == 0) {
        gint32 was, now;
        g_variant_get(params, "(ii)", &was, &now);
        assert(was >= 0 && was <= ZYL_POWER_STATE_SHUTDOWN && now >= 0 && now <= ZYL_POWER_STATE_SHUTDOWN);
        g_string_append_printf(heard, " %s>%s", NAMES[was], NAMES[now]);
    } else {
        const gchar *tag;
        g_variant_get(params, "(&s)", &tag);
        g_string_append_printf(heard, " %s:%s", name, tag);
    }
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

/* The signals heard since the last look, "" for none */
static bool saw(const char *want) {
    settle();
    const char *was = heard->str + (heard->len > 0);
    bool ok = strcmp(was, want) == 0;
    if (!ok) fprintf(stderr, "\n    heard '%s', want '%s'\n", was, want);
    g_string_truncate(heard, 0);
    return ok;
}

/* A pending timer runs now, through the main loop as when its time comes */
static void fire(guint id) {
    GSource *s = id ? g_main_context_find_source_by_id(NULL, id) : NULL;
    assert(s);
    g_source_set_ready_time(s, 0);
    while (g_main_context_iteration(NULL, FALSE)) ;
}

/* Seconds until a pending timer is due (second timers may round up by one) */
static bool due_in(guint id, int secs) {
    GSource *s = id ? g_main_context_find_source_by_id(NULL, id) : NULL;
    assert(s);
    gint64 left = (g_source_get_ready_time(s) - g_get_monotonic_time()) / G_USEC_PER_SEC;
    return left >= secs - 1 && left <= secs + 1;
}

static void reset(void) {
    if (svc) zyl_power_destroy(svc);
    assert(system("rm -rf " R) == 0);
    put(BL "max_brightness", "100\n");
    put(BL "brightness", "60\n");
    for (int i = 0; i < 4; i++) {
        char path[128];
        snprintf(path, sizeof(path), CPU "cpu%d/cpufreq/scaling_governor", i);
        put(path, "performance\n");
        snprintf(path, sizeof(path), CPU "cpu%d/online", i);
        put(path, "1\n");
    }
    svc = zyl_power_create();
    svc->dbus = link_svc;
    settle();
    g_string_truncate(heard, 0);
}

static void test_screen_off_goes_dark_and_on_brings_back_the_users_level(void) {
    assert(svc->state == ZYL_POWER_STATE_ACTIVE && svc->brightness == 60);  /* from the backlight */
    assert(zyl_power_request_screen_off(svc) == 0);
    assert(shown() == 0 && svc->brightness == 60);
    assert(saw("ACTIVE>OFF"));
    assert(zyl_power_request_screen_on(svc) == 0);
    assert(shown() == 60 && saw("OFF>ACTIVE"));
    zyl_power_request_screen_on(svc);                     /* already on: nothing to tell */
    assert(saw(""));
}

static void test_idle_dims_to_thirty_percent_then_turns_the_screen_off(void) {
    assert(due_in(svc->dim_timer_id, 25));
    fire(svc->dim_timer_id);
    assert(svc->state == ZYL_POWER_STATE_DIM && shown() == 18 && saw("ACTIVE>DIM"));
    assert(due_in(svc->screen_off_timer_id, 5));          /* 30 s timeout - 25 s dim */
    fire(svc->screen_off_timer_id);
    assert(svc->state == ZYL_POWER_STATE_SCREEN_OFF && shown() == 0 && saw("DIM>OFF"));
    zyl_power_request_screen_on(svc);                     /* the fired timers are not removed twice */
    assert(shown() == 60 && saw("OFF>ACTIVE") && svc->dim_timer_id);
}

/* A touch (the compositor's UserActivity) keeps the screen awake: the
   countdown starts over, a dimmed screen comes back, a dark one stays dark */
static void test_user_activity_keeps_the_screen_awake(void) {
    guint first = svc->dim_timer_id;
    assert(zyl_power_user_activity(svc) == 0);
    assert(svc->dim_timer_id && svc->dim_timer_id != first && due_in(svc->dim_timer_id, 25));
    assert(saw(""));
    fire(svc->dim_timer_id);
    assert(svc->state == ZYL_POWER_STATE_DIM && saw("ACTIVE>DIM"));
    zyl_power_user_activity(svc);
    assert(svc->state == ZYL_POWER_STATE_ACTIVE && shown() == 60 && saw("DIM>ACTIVE"));
    assert(!svc->screen_off_timer_id && due_in(svc->dim_timer_id, 25));
    zyl_power_request_screen_off(svc);
    assert(saw("ACTIVE>OFF"));
    zyl_power_user_activity(svc);
    assert(svc->state == ZYL_POWER_STATE_SCREEN_OFF && shown() == 0 && saw(""));
}

static void test_dimming_has_a_floor_and_never_brightens(void) {
    zyl_power_set_brightness(svc, 10);
    fire(svc->dim_timer_id);
    assert(shown() == 5);                                 /* 30% would be 3 */
    zyl_power_request_screen_on(svc);
    zyl_power_set_brightness(svc, 3);
    fire(svc->dim_timer_id);
    assert(svc->state == ZYL_POWER_STATE_DIM && shown() == 3);
}

static void test_brightness_is_clamped_and_waits_while_the_screen_is_dark(void) {
    zyl_power_set_brightness(svc, 150);
    assert(svc->brightness == 100 && shown() == 100);
    zyl_power_set_brightness(svc, -20);
    assert(svc->brightness == 0 && shown() == 0);
    zyl_power_set_brightness(svc, 70);
    fire(svc->dim_timer_id);
    assert(shown() == 21);
    zyl_power_set_brightness(svc, 90);                    /* dimmed: stays dim */
    assert(shown() == 21);
    zyl_power_request_screen_off(svc);
    zyl_power_set_brightness(svc, 40);                    /* off: stays off */
    assert(shown() == 0);
    zyl_power_request_screen_on(svc);
    assert(shown() == 40);
}

static void test_a_wakelock_keeps_the_screen_on_and_the_device_awake(void) {
    assert(zyl_power_acquire_wakelock(svc, "media") == 0);
    fire(svc->dim_timer_id);
    assert(svc->state == ZYL_POWER_STATE_ACTIVE && shown() == 60 && saw(""));
    zyl_power_request_screen_off(svc);                    /* the power key still turns it off */
    assert(svc->state == ZYL_POWER_STATE_SCREEN_OFF && svc->suspend_timer_id == 0);
    assert(zyl_power_request_suspend(svc) == -1);
    assert(svc->state == ZYL_POWER_STATE_SCREEN_OFF && saw("ACTIVE>OFF"));
}

static void test_the_last_wakelock_released_restarts_the_countdown(void) {
    zyl_power_acquire_wakelock(svc, "media");
    zyl_power_acquire_wakelock(svc, "download");
    fire(svc->dim_timer_id);                              /* held off */
    zyl_power_release_wakelock(svc, "media");
    assert(svc->dim_timer_id == 0);                       /* one still holds it */
    zyl_power_release_wakelock(svc, "download");
    assert(due_in(svc->dim_timer_id, 25));
    fire(svc->dim_timer_id);
    assert(svc->state == ZYL_POWER_STATE_DIM && saw("ACTIVE>DIM"));
    /* Screen off while held: doze waits for the release */
    zyl_power_request_screen_on(svc);
    zyl_power_acquire_wakelock(svc, "media");
    zyl_power_request_screen_off(svc);
    assert(svc->suspend_timer_id == 0);
    zyl_power_release_wakelock(svc, "media");
    assert(due_in(svc->suspend_timer_id, 300));
}

static void test_wakelocks_count_once_per_tag_and_are_capped(void) {
    assert(zyl_power_acquire_wakelock(svc, "a") == 0 && zyl_power_acquire_wakelock(svc, "a") == 0);
    assert(svc->wakelock_count == 1);
    for (int i = 1; i < MAX_WAKELOCKS; i++) {
        char tag[16];
        snprintf(tag, sizeof(tag), "t%d", i);
        assert(zyl_power_acquire_wakelock(svc, tag) == 0);
    }
    assert(zyl_power_acquire_wakelock(svc, "one-too-many") == -1 && svc->wakelock_count == MAX_WAKELOCKS);
    zyl_power_release_wakelock(svc, "never-taken");
    assert(svc->wakelock_count == MAX_WAKELOCKS);
    zyl_power_release_wakelock(svc, "a");
    assert(svc->wakelock_count == MAX_WAKELOCKS - 1);
    assert(zyl_power_acquire_wakelock(svc, NULL) == -1);
}

static void test_a_tag_too_long_to_expire_is_refused(void) {
    char tag[200];
    memset(tag, 'x', sizeof(tag) - 1);
    tag[sizeof(tag) - 1] = '\0';
    assert(zyl_power_acquire_wakelock(svc, tag) == -1 && svc->wakelock_count == 0);
    tag[127] = '\0';
    assert(zyl_power_acquire_wakelock(svc, tag) == 0);
    fire(svc->wakelock_timers[0]);
    assert(svc->wakelock_count == 0);
}

static void test_a_forgotten_wakelock_expires_after_ten_minutes(void) {
    zyl_power_acquire_wakelock(svc, "gps");
    zyl_power_acquire_wakelock(svc, "sync");
    fire(svc->dim_timer_id);                              /* held off */
    assert(due_in(svc->wakelock_timers[0], WAKELOCK_TIMEOUT_SEC));
    fire(svc->wakelock_timers[0]);                        /* "gps" — "sync" moves into its slot */
    assert(svc->wakelock_count == 1 && strcmp(svc->wakelocks[0], "sync") == 0);
    assert(saw("WakelockExpired:gps"));
    fire(svc->wakelock_timers[0]);                        /* "sync" kept its own timer */
    assert(svc->wakelock_count == 0 && saw("WakelockExpired:sync"));
    assert(svc->dim_timer_id);                            /* and the countdown runs again */
}

static void test_screen_off_dozes_and_screen_on_wakes_every_core(void) {
    zyl_power_request_screen_off(svc);
    assert(due_in(svc->suspend_timer_id, 300));
    fire(svc->suspend_timer_id);
    assert(svc->state == ZYL_POWER_STATE_DOZE && saw("ACTIVE>OFF OFF>DOZE"));
    assert(strcmp(got(CPU "cpu0/cpufreq/scaling_governor"), "powersave") == 0);
    assert(strcmp(got(CPU "cpu1/online"), "1") == 0);     /* the lower half stays */
    assert(strcmp(got(CPU "cpu2/online"), "0") == 0 && strcmp(got(CPU "cpu3/online"), "0") == 0);
    assert(due_in(svc->suspend_timer_id, 1800));
    zyl_power_request_screen_on(svc);
    assert(svc->state == ZYL_POWER_STATE_ACTIVE && shown() == 60 && saw("DOZE>ACTIVE"));
    assert(strcmp(got(CPU "cpu2/online"), "1") == 0 && strcmp(got(CPU "cpu3/online"), "1") == 0);
    assert(strcmp(got(CPU "cpu3/cpufreq/scaling_governor"), ACTIVE_GOVERNOR) == 0);
    assert(svc->suspend_timer_id == 0);
}

static void test_suspend_comes_back_to_the_state_it_left(void) {
    svc->config.doze_enabled = false;
    zyl_power_request_screen_off(svc);
    assert(due_in(svc->suspend_timer_id, 60));
    fire(svc->suspend_timer_id);                          /* logind is not there: comes straight back */
    assert(svc->state == ZYL_POWER_STATE_SCREEN_OFF && shown() == 0);
    assert(saw("ACTIVE>OFF OFF>SUSPEND SUSPEND>OFF"));    /* WAM still sees the screen off */
    zyl_power_request_screen_on(svc);
    assert(saw("OFF>ACTIVE"));
    /* From doze it returns to doze, so waking still brings the cores back */
    svc->config.doze_enabled = true;
    zyl_power_request_screen_off(svc);
    fire(svc->suspend_timer_id);
    fire(svc->suspend_timer_id);
    assert(saw("ACTIVE>OFF OFF>DOZE DOZE>SUSPEND SUSPEND>DOZE"));
    zyl_power_request_screen_on(svc);
    assert(strcmp(got(CPU "cpu3/online"), "1") == 0 && saw("DOZE>ACTIVE"));
}

static void test_state_says_what_the_screen_and_battery_are(void) {
    GVariant *r = state_variant(svc), *d = g_variant_get_child_value(r, 0);
    const char *state = NULL;
    gboolean on = FALSE, battery = TRUE;
    gint32 level = -1;
    assert(g_variant_lookup(d, "state", "&s", &state) && strcmp(state, "ACTIVE") == 0);
    assert(g_variant_lookup(d, "screenOn", "b", &on) && on);
    assert(g_variant_lookup(d, "brightness", "i", &level) && level == 60);
    assert(g_variant_lookup(d, "batteryPresent", "b", &battery) && !battery);
    assert(!g_variant_lookup(d, "batteryLevel", "i", &level));   /* no battery, no level */
    g_variant_unref(d);
    g_variant_unref(r);
    fire(svc->dim_timer_id);
    r = state_variant(svc), d = g_variant_get_child_value(r, 0);
    assert(g_variant_lookup(d, "state", "&s", &state) && strcmp(state, "DIM") == 0);
    assert(g_variant_lookup(d, "screenOn", "b", &on) && on);     /* dim is still on */
    g_variant_unref(d);
    g_variant_unref(r);
    zyl_power_request_screen_off(svc);
    r = state_variant(svc), d = g_variant_get_child_value(r, 0);
    assert(g_variant_lookup(d, "state", "&s", &state) && strcmp(state, "SCREEN_OFF") == 0);
    assert(g_variant_lookup(d, "screenOn", "b", &on) && !on);
    g_variant_unref(d);
    g_variant_unref(r);
}

static void settings_get(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                         const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)method; (void)params; (void)data;
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "brightness", g_variant_new_int64(45));  /* JSON numbers are x */
    g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &b));
}

static void test_the_level_saved_in_settings_is_used_on_start(void) {
    GDBusConnection *mine, *peer;
    bus_link(&mine, &peer);
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.zylos.Settings'><method name='Get'>"
        "<arg type='s' direction='in'/><arg type='a{sv}' direction='out'/>"
        "</method></interface></node>", NULL);
    static const GDBusInterfaceVTable vt = { .method_call = settings_get };
    assert(g_dbus_connection_register_object(peer, "/org/zylos/Settings", info->interfaces[0],
                                             &vt, NULL, NULL, NULL));
    on_power_bus_acquired(mine, ZYL_POWER_DBUS_NAME, svc);
    for (int i = 0; i < 2000 && svc->brightness != 45; i++) {   /* up to 2 s for the reply */
        g_usleep(1000);
        while (g_main_context_iteration(NULL, FALSE)) ;
    }
    assert(svc->brightness == 45 && shown() == 45);
    svc->dbus = link_svc;
    g_dbus_connection_close_sync(mine, NULL, NULL);
    g_dbus_connection_close_sync(peer, NULL, NULL);
    g_object_unref(mine);
    g_object_unref(peer);
    g_dbus_node_info_unref(info);
}

int main(void) {
    /* No system bus: the service must not own a real name or reach logind (Suspend) */
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/zyl-power-test", 1);
    GDBusConnection *real = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    assert(!real);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);  /* e.g. removing a gone timer */
    g_log_set_handler(NULL, G_LOG_LEVEL_MESSAGE, quiet, NULL);       /* the service's progress lines */
    heard = g_string_new("");
    bus_link(&link_svc, &link_peer);
    g_dbus_connection_signal_subscribe(link_peer, NULL, NULL, NULL, NULL, NULL,
                                       G_DBUS_SIGNAL_FLAGS_NONE, on_signal, NULL, NULL);

    printf("=== Power service ===\n");
    RUN_TEST(test_screen_off_goes_dark_and_on_brings_back_the_users_level);
    RUN_TEST(test_idle_dims_to_thirty_percent_then_turns_the_screen_off);
    RUN_TEST(test_user_activity_keeps_the_screen_awake);
    RUN_TEST(test_dimming_has_a_floor_and_never_brightens);
    RUN_TEST(test_brightness_is_clamped_and_waits_while_the_screen_is_dark);
    RUN_TEST(test_a_wakelock_keeps_the_screen_on_and_the_device_awake);
    RUN_TEST(test_the_last_wakelock_released_restarts_the_countdown);
    RUN_TEST(test_wakelocks_count_once_per_tag_and_are_capped);
    RUN_TEST(test_a_tag_too_long_to_expire_is_refused);
    RUN_TEST(test_a_forgotten_wakelock_expires_after_ten_minutes);
    RUN_TEST(test_screen_off_dozes_and_screen_on_wakes_every_core);
    RUN_TEST(test_suspend_comes_back_to_the_state_it_left);
    RUN_TEST(test_state_says_what_the_screen_and_battery_are);
    RUN_TEST(test_the_level_saved_in_settings_is_used_on_start);
    zyl_power_destroy(svc);
    assert(system("rm -rf " R) == 0);
    printf("\n%d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
