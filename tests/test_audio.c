/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 오디오 서비스(runtime/services/audio) 소리 정책 단위 테스트
 * 수행범위: 스트림 기본 볼륨(media 는 기기 출력에서 시작), 볼륨 0~100 클램프와
 *           잘못된 스트림 거부, media 만 기기 출력을 움직임, D-Bus 스트림 이름
 *           (모르는 이름은 InvalidArgs), 무음 모드(키 클릭은 조용, 경보음은 울림,
 *           시작 때 설정의 sound.silentMode 를 이어받음), 시작 때 설정의 스트림
 *           볼륨(sound.<스트림>Volume, 정수·실수 어느 쪽이든)을 이어받음, 출력 음소거, 진동은
 *           설정 sound.vibration 을 따름(없으면 켜짐, 빈 패턴은 안 함), 마이크
 *           음소거·기능 보고는 기기(HAL) 대로
 * 의존방향: runtime/services/audio/audio.c (ZYL_TEST), 이 파일의 가짜 Audio·
 *           Vibrator HAL, gio (소켓 쌍 위의 피어 D-Bus + 그 위의 가짜 Settings)
 * SOLID: SRP — 소리 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-audio-test"
#include <assert.h>
#include <sys/socket.h>
#include "../runtime/services/audio/audio.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); fflush(stdout); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

/* ─── The device: a fake Audio and Vibrator HAL ─── */
static int device_volume = 35, set_volume_to, tones, tone_freq, tone_ms, tone_volume, vibrations, vibrated_steps;
static bool output_muted, mic_muted;
static int  hal_get_volume(void) { return device_volume; }
static bool hal_set_volume(int p) { set_volume_to = p; return true; }
static bool hal_set_mute(bool m) { output_muted = m; return true; }
static bool hal_set_mic_mute(bool m) { mic_muted = m; return true; }
static bool hal_tone(int f, int ms, int v) { tones++; tone_freq = f; tone_ms = ms; tone_volume = v; return true; }
static bool hal_no_speakerphone(void) { return false; }
static bool hal_set_speakerphone(bool on) { (void)on; return false; }
static const ZylAudioHal fake_audio = {
    .get_output_volume = hal_get_volume, .set_output_volume = hal_set_volume,
    .set_output_mute = hal_set_mute, .set_input_mute = hal_set_mic_mute, .play_tone = hal_tone,
    .speakerphone_supported = hal_no_speakerphone, .set_speakerphone = hal_set_speakerphone,
};
const ZylAudioHal *zyl_hal_audio(void) { return &fake_audio; }
static bool hal_motor(void) { return true; }
static bool hal_vibrate(const int *pattern, int n) { (void)pattern; vibrations++; vibrated_steps = n; return true; }
static const ZylVibratorHal fake_vibrator = { .present = hal_motor, .vibrate = hal_vibrate };
const ZylVibratorHal *zyl_hal_vibrator(void) { return &fake_vibrator; }

static void reset(void) {
    set_volume_to = -1;
    tones = vibrations = vibrated_steps = 0;
}

/* ─── A peer-to-peer bus over a socket pair: the service on one end, the
       caller and the Settings service on the other ─── */
static ZylAudioService *svc;
static GDBusConnection *server, *client;

static void on_server(GObject *src, GAsyncResult *res, gpointer data) {
    (void)src; (void)data;
    server = g_dbus_connection_new_finish(res, NULL);
}

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

/* The "sound" category Settings keeps: 1 on, 0 off, -1 never set */
static gint saved_silent = 1, saved_vibration = -1, settings_up;
static gint updated_alarm = -1;   /* sound.alarmVolume as Update last wrote it */
static const char *SETTINGS_XML =
    "<node><interface name='org.zylos.Settings'><method name='Get'>"
    "<arg type='s' direction='in'/><arg type='a{sv}' direction='out'/>"
    "</method><method name='Update'>"
    "<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/>"
    "</method></interface></node>";

static void settings_get(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                         const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)data;
    if (g_strcmp0(method, "Update") == 0) {
        const gchar *cat = NULL, *key = NULL;
        GVariant *v = NULL;
        g_variant_get(params, "(&s&sv)", &cat, &key, &v);
        if (g_strcmp0(cat, "sound") == 0 && g_strcmp0(key, "alarmVolume") == 0 &&
            g_variant_is_of_type(v, G_VARIANT_TYPE_INT64))
            g_atomic_int_set(&updated_alarm, (gint)g_variant_get_int64(v));
        g_variant_unref(v);
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    if (g_atomic_int_get(&saved_silent) >= 0)
        g_variant_builder_add(&d, "{sv}", "silentMode", g_variant_new_boolean(g_atomic_int_get(&saved_silent)));
    if (g_atomic_int_get(&saved_vibration) >= 0)
        g_variant_builder_add(&d, "{sv}", "vibration", g_variant_new_boolean(g_atomic_int_get(&saved_vibration)));
    /* Volumes Settings saved, in each number form the settings daemon may hand over */
    g_variant_builder_add(&d, "{sv}", "mediaVolume", g_variant_new_int64(55));
    g_variant_builder_add(&d, "{sv}", "notifVolume", g_variant_new_int32(33));
    g_variant_builder_add(&d, "{sv}", "alarmVolume", g_variant_new_double(66.6));
    g_variant_builder_add(&d, "{sv}", "systemVolume", g_variant_new_int64(250));
    g_variant_builder_add(&d, "{sv}", "ringtoneVolume", g_variant_new_string("40"));   /* not a number */
    g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
}

static const GDBusInterfaceVTable settings_vtable = { .method_call = settings_get };

/* Settings answers from its own thread: the service asks it with a blocking call */
static gpointer settings_thread(gpointer data) {
    (void)data;
    GMainContext *ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(SETTINGS_XML, NULL);
    assert(g_dbus_connection_register_object(client, "/org/zylos/Settings", info->interfaces[0],
                                             &settings_vtable, NULL, NULL, NULL));
    g_atomic_int_set(&settings_up, 1);
    g_main_loop_run(g_main_loop_new(ctx, FALSE));
    return NULL;
}

static GVariant *reply;
static GError *error;
static void on_reply(GObject *src, GAsyncResult *res, gpointer done) {
    reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &error);
    *(gboolean *)done = TRUE;
}

/* The service answers on this thread's main context: wait by running it */
static GVariant *call(const char *method, GVariant *args) {
    gboolean done = FALSE;
    g_clear_error(&error);
    g_dbus_connection_call(client, NULL, ZYL_AUDIO_DBUS_PATH, ZYL_AUDIO_DBUS_NAME, method, args,
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_reply, &done);
    while (!done) g_main_context_iteration(NULL, TRUE);
    return reply;
}

static void call_ok(const char *method, GVariant *args) {
    GVariant *r = call(method, args);
    assert(r);
    g_variant_unref(r);
}

static gboolean call_bool(const char *method, GVariant *args) {
    GVariant *r = call(method, args);
    gboolean b = FALSE;
    assert(r);
    g_variant_get(r, "(b)", &b);
    g_variant_unref(r);
    return b;
}

/* ─── Tests ─── */

static void test_silent_mode_and_volumes_saved_are_restored_at_start(void) {
    for (int i = 0; i < 500 && !svc->silent; i++) {                    /* Settings answers when it can */
        g_main_context_iteration(NULL, FALSE);
        g_usleep(10000);
    }
    assert(svc->silent);
    assert(zyl_audio_get_volume(svc, ZYL_AUDIO_STREAM_MEDIA) == 55 && set_volume_to == 55);  /* the device too */
    assert(zyl_audio_get_volume(svc, ZYL_AUDIO_STREAM_NOTIF) == 33);
    assert(zyl_audio_get_volume(svc, ZYL_AUDIO_STREAM_ALARM) == 66);
    assert(zyl_audio_get_volume(svc, ZYL_AUDIO_STREAM_SYSTEM) == 100);  /* clamped */
    assert(zyl_audio_get_volume(svc, ZYL_AUDIO_STREAM_RINGTONE) == 80); /* not a number: the default */
}

static void test_streams_start_at_defaults_and_media_at_the_device(void) {
    ZylAudioService *other = zyl_audio_create();                       /* no Settings to ask */
    assert(zyl_audio_get_volume(other, ZYL_AUDIO_STREAM_MEDIA) == 35);  /* what the device was set to */
    assert(zyl_audio_get_volume(other, ZYL_AUDIO_STREAM_NOTIF) == 80);
    assert(zyl_audio_get_volume(other, ZYL_AUDIO_STREAM_ALARM) == 90);
    assert(zyl_audio_get_volume(other, ZYL_AUDIO_STREAM_RINGTONE) == 80);
    assert(zyl_audio_get_volume(other, ZYL_AUDIO_STREAM_SYSTEM) == 50);
    zyl_audio_destroy(other);
    device_volume = -1;                                                 /* the device cannot say */
    other = zyl_audio_create();
    assert(zyl_audio_get_volume(other, ZYL_AUDIO_STREAM_MEDIA) == 70);
    zyl_audio_destroy(other);
    device_volume = 35;
}

static void test_volume_is_clamped_and_only_media_moves_the_device(void) {
    assert(zyl_audio_set_volume(svc, ZYL_AUDIO_STREAM_MEDIA, 150) == 100 && set_volume_to == 100);
    assert(zyl_audio_set_volume(svc, ZYL_AUDIO_STREAM_MEDIA, -20) == 0 && set_volume_to == 0);
    assert(zyl_audio_set_volume(svc, ZYL_AUDIO_STREAM_MEDIA, G_MININT) == 0);
    assert(zyl_audio_set_volume(svc, ZYL_AUDIO_STREAM_ALARM, G_MAXINT) == 100);
    set_volume_to = -1;
    assert(zyl_audio_set_volume(svc, ZYL_AUDIO_STREAM_NOTIF, 40) == 40 && set_volume_to == -1);
    assert(zyl_audio_get_volume(svc, ZYL_AUDIO_STREAM_NOTIF) == 40);
    /* Not a stream: refused, nothing written past the table */
    assert(zyl_audio_set_volume(svc, (ZylAudioStream)5, 10) == -1);
    assert(zyl_audio_set_volume(svc, (ZylAudioStream)-1, 10) == -1);
    assert(zyl_audio_get_volume(svc, (ZylAudioStream)5) == 0 && zyl_audio_get_volume(NULL, 0) == 0);
    assert(set_volume_to == -1);
}

static void test_dbus_names_the_streams_and_refuses_unknown_ones(void) {
    GVariant *r = call("SetVolume", g_variant_new("(si)", "ringtone", 250));
    assert(r);
    GVariant *d = g_variant_get_child_value(r, 0);
    const char *stream = NULL;
    gint32 value = -1;
    assert(g_variant_lookup(d, "stream", "&s", &stream) && strcmp(stream, "ringtone") == 0);
    assert(g_variant_lookup(d, "value", "i", &value) && value == 100);
    g_variant_unref(d);
    g_variant_unref(r);
    r = call("GetVolume", g_variant_new("(s)", "ringtone"));
    g_variant_get(r, "(i)", &value);
    assert(value == 100);
    g_variant_unref(r);

    const char *bad[] = { "bogus", "", "MEDIA", "media " };
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        assert(!call("SetVolume", g_variant_new("(si)", bad[i], 10)));
        assert(g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS));
        assert(!call("GetVolume", g_variant_new("(s)", bad[i])));
        assert(g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS));
    }
}

static void test_silent_mode_quiets_key_clicks_but_not_alerts(void) {
    assert(call_bool("SetSilentMode", g_variant_new("(b)", FALSE)));
    zyl_audio_set_volume(svc, ZYL_AUDIO_STREAM_SYSTEM, 60);
    call_ok("PlayKeyClick", NULL);
    assert(tones == 1 && tone_freq == 800 && tone_ms == 30 && tone_volume == 60);

    assert(call_bool("SetSilentMode", g_variant_new("(b)", TRUE)));
    call_ok("PlayKeyClick", NULL);
    assert(tones == 1);
    call_ok("PlayTone", g_variant_new("(ii)", 1000, 200));             /* an alert sounds regardless */
    assert(tones == 2 && tone_freq == 1000 && tone_ms == 200 && tone_volume == 60);
}

static void test_muted_output_also_quiets_key_clicks(void) {
    assert(call_bool("SetSilentMode", g_variant_new("(b)", FALSE)));
    call_ok("SetMute", g_variant_new("(b)", TRUE));
    assert(output_muted && call_bool("GetMute", NULL));
    call_ok("PlayKeyClick", NULL);
    assert(tones == 0);
    call_ok("SetMute", g_variant_new("(b)", FALSE));
    assert(!output_muted && !call_bool("GetMute", NULL));
    call_ok("PlayKeyClick", NULL);
    assert(tones == 1);
}

static void test_vibration_follows_the_setting(void) {
    const gint32 pattern[] = { 300, 100, 300 };
#define PATTERN() g_variant_new("(@ai)", g_variant_new_fixed_array(G_VARIANT_TYPE_INT32, pattern, 3, sizeof(gint32)))
    g_atomic_int_set(&saved_vibration, -1);                            /* never set: on */
    assert(call_bool("Vibrate", PATTERN()) && vibrations == 1 && vibrated_steps == 3);
    g_atomic_int_set(&saved_vibration, 0);                             /* the user turned it off */
    assert(!call_bool("Vibrate", PATTERN()) && vibrations == 1);
    g_atomic_int_set(&saved_vibration, 1);
    assert(call_bool("Vibrate", PATTERN()) && vibrations == 2);
    assert(!call_bool("Vibrate", g_variant_new("(@ai)",                 /* nothing to play */
        g_variant_new_fixed_array(G_VARIANT_TYPE_INT32, NULL, 0, sizeof(gint32)))));
    assert(vibrations == 2);
#undef PATTERN
}

static void test_mic_mute_and_capabilities_are_the_devices(void) {
    assert(call_bool("SetMicMute", g_variant_new("(b)", TRUE)) && mic_muted);
    assert(call_bool("SetMicMute", g_variant_new("(b)", FALSE)) && !mic_muted);
    assert(!call_bool("SetSpeakerphone", g_variant_new("(b)", TRUE)));  /* this device has none */
    GVariant *r = call("GetCapabilities", NULL);
    GVariant *d = g_variant_get_child_value(r, 0);
    gboolean speakerphone = TRUE, vibrator = FALSE;
    assert(g_variant_lookup(d, "speakerphone", "b", &speakerphone) && !speakerphone);
    assert(g_variant_lookup(d, "vibrator", "b", &vibrator) && vibrator);
    g_variant_unref(d);
    g_variant_unref(r);
}

/* A volume an app sets is the user's: Settings keeps it (the next start restores it) */
static void test_a_volume_set_is_saved_in_settings(void) {
    call_ok("SetVolume", g_variant_new("(si)", "alarm", 144));        /* clamped first */
    for (int i = 0; i < 500 && g_atomic_int_get(&updated_alarm) != 100; i++)
        g_main_context_iteration(NULL, FALSE), g_usleep(1000);
    assert(g_atomic_int_get(&updated_alarm) == 100);
}

/* A new notification: two tones at the notification volume, none in silent mode */
static void test_the_notification_sound(void) {
    svc->silent = false;
    zyl_audio_set_mute(svc, false);
    zyl_audio_set_volume(svc, ZYL_AUDIO_STREAM_NOTIF, 40);
    call_ok("PlayNotificationSound", NULL);
    for (int i = 0; i < 500 && tones < 2; i++) g_main_context_iteration(NULL, FALSE), g_usleep(1000);
    assert(tones == 2 && tone_volume == 40);
    svc->silent = true;
    call_ok("PlayNotificationSound", NULL);
    g_usleep(250000);
    while (g_main_context_iteration(NULL, FALSE)) ;
    assert(tones == 2);
    svc->silent = false;
}

int main(void) {
    printf("=== Zyl OS Audio Service Tests ===\n");
    /* No system bus for the service to take a name on: the test is its bus */
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=" DIR_ "-no-bus", TRUE);
    svc = zyl_audio_create();
    assert(svc);
    peer_bus();
    g_thread_new("settings", settings_thread, NULL);
    while (!g_atomic_int_get(&settings_up)) g_usleep(1000);
    on_audio_bus(server, ZYL_AUDIO_DBUS_NAME, svc);                    /* registers, asks Settings */

    RUN_TEST(test_silent_mode_and_volumes_saved_are_restored_at_start);
    RUN_TEST(test_streams_start_at_defaults_and_media_at_the_device);
    RUN_TEST(test_volume_is_clamped_and_only_media_moves_the_device);
    RUN_TEST(test_dbus_names_the_streams_and_refuses_unknown_ones);
    RUN_TEST(test_silent_mode_quiets_key_clicks_but_not_alerts);
    RUN_TEST(test_a_volume_set_is_saved_in_settings);
    RUN_TEST(test_the_notification_sound);
    RUN_TEST(test_muted_output_also_quiets_key_clicks);
    RUN_TEST(test_vibration_follows_the_setting);
    RUN_TEST(test_mic_mute_and_capabilities_are_the_devices);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
