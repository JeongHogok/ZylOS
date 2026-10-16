/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 오디오 서비스 (org.zylos.AudioService) — 소리 정책과 D-Bus
 * 수행범위: 스트림별 볼륨(이름: media·notif·alarm·ringtone·system, media 가
 *           기기 출력 — 시작 때 설정 sound.<스트림>Volume 을 따름), 출력 음소거,
 *           무음 모드(키 클릭 조용, 경보음은 울림 — 시작 때 설정 sound.silentMode 를
 *           따름), 마이크 음소거(통화 중 음소거), 톤,
 *           스피커폰, 진동(설정 sound.vibration 을 따름), 이
 *           기기에 있는 기능(스피커폰·진동 모터) 보고 — 없는 기능은 앱이 비활성
 * 의존방향: audio.h, hal.h (Audio·Vibrator HAL), gio/gio.h (D-Bus)
 * SOLID: SRP — 소리 정책만. DIP — 장치는 ZylAudioHal/ZylVibratorHal 로만
 * ────────────────────────────────────────────────────────── */

#include "audio.h"
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <glib-unix.h>

struct ZylAudioService {
    int volumes[5]; /* per-stream volume 0-100 */
    bool muted;     /* the output itself */
    bool silent;    /* silent mode: no key clicks or system sounds; alerts still sound */
    bool alerting;  /* an emergency alert sounds: the output is the alert's until alert_end */
    guint alert_end;
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* Stream names apps use (apps/system/services/audio.js), in ZylAudioStream order */
static const char *const STREAM_NAMES[] = {"media", "notif", "alarm", "ringtone", "system"};

static int stream_from_name(const char *name) {
    for (int i = 0; i < (int)G_N_ELEMENTS(STREAM_NAMES); i++)
        if (g_strcmp0(name, STREAM_NAMES[i]) == 0) return i;
    return -1;
}

/* Whether to vibrate is a setting (Settings > Sound writes sound.vibration):
   asked when needed, so a change applies at once with no second copy */
static bool vibration_wanted(ZylAudioService *svc) {
    if (!svc->dbus) return true;
    GVariant *r = g_dbus_connection_call_sync(svc->dbus, "org.zylos.Settings", "/org/zylos/Settings",
        "org.zylos.Settings", "Get", g_variant_new("(s)", "sound"), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
    gboolean on = TRUE;
    if (r) {
        GVariant *d = g_variant_get_child_value(r, 0);
        g_variant_lookup(d, "vibration", "b", &on);
        g_variant_unref(d);
        g_variant_unref(r);
    }
    return on;
}

/* A volume the caller set is the user's: Settings keeps it, the same as the
   emulator's audio service — otherwise the next start brings back the old one */
static void save_volume(ZylAudioService *svc, const char *stream, int value) {
    if (!svc->dbus) return;
    char key[32];
    snprintf(key, sizeof(key), "%sVolume", stream);
    g_dbus_connection_call(svc->dbus, "org.zylos.Settings", "/org/zylos/Settings", "org.zylos.Settings",
        "Update", g_variant_new("(ssv)", "sound", key, g_variant_new_int64(value)),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

/* A new notification: two short tones at the notification volume, as in the
   emulator — not in silent mode or with the output muted */
static gboolean second_chime(gpointer data) {
    ZylAudioService *svc = data;
    zyl_hal_audio()->play_tone(1100, 120, svc->volumes[ZYL_AUDIO_STREAM_NOTIF]);
    return G_SOURCE_REMOVE;
}

static void play_notification_sound(ZylAudioService *svc) {
    if (svc->silent || svc->muted) return;
    zyl_hal_audio()->play_tone(880, 150, svc->volumes[ZYL_AUDIO_STREAM_NOTIF]);
    g_timeout_add(180, second_chime, svc);
}

/* ─── D-Bus ─── */

static const char *audio_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_AUDIO_DBUS_NAME "'>"
    "    <method name='GetCapabilities'>"
    "      <arg type='a{sv}' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='GetVolume'>"
    "      <arg type='s' name='stream' direction='in'/>"
    "      <arg type='i' name='volume' direction='out'/>"
    "    </method>"
    "    <method name='SetVolume'>"
    "      <arg type='s' name='stream' direction='in'/>"
    "      <arg type='i' name='value' direction='in'/>"
    "      <arg type='a{sv}' name='result' direction='out'/>"
    "    </method>"
    "    <method name='SetMicMute'>"
    "      <arg type='b' name='muted' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <method name='SetSpeakerphone'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <method name='Vibrate'>"
    "      <arg type='ai' name='pattern' direction='in'/>"
    "      <arg type='b' name='vibrated' direction='out'/>"
    "    </method>"
    "    <method name='SetSilentMode'>"
    "      <arg type='b' name='enabled' direction='in'/>"
    "      <arg type='b' name='applied' direction='out'/>"
    "    </method>"
    "    <method name='GetMute'>"
    "      <arg type='b' name='muted' direction='out'/>"
    "    </method>"
    "    <method name='SetMute'>"
    "      <arg type='b' name='mute' direction='in'/>"
    "    </method>"
    "    <method name='PlayTone'>"
    "      <arg type='i' name='freq_hz' direction='in'/>"
    "      <arg type='i' name='duration_ms' direction='in'/>"
    "    </method>"
    "    <method name='PlayKeyClick'/>"
    "    <method name='PlayNotificationSound'/>"
    "  </interface>"
    "</node>";

/* What this device has — the same answer as the emulator's audio service */
static GVariant *capabilities_variant(void) {
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "speakerphone",
                          g_variant_new_boolean(zyl_hal_audio()->speakerphone_supported()));
    g_variant_builder_add(&d, "{sv}", "vibrator", g_variant_new_boolean(zyl_hal_vibrator()->present()));
    return g_variant_new("(a{sv})", &d);
}

static void handle_audio_method(GDBusConnection *conn, const gchar *sender,
                                 const gchar *path, const gchar *iface,
                                 const gchar *method, GVariant *params,
                                 GDBusMethodInvocation *inv, gpointer data) {
    ZylAudioService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "GetCapabilities") == 0) {
        g_dbus_method_invocation_return_value(inv, capabilities_variant());
    } else if (g_strcmp0(method, "GetVolume") == 0 || g_strcmp0(method, "SetVolume") == 0) {
        const gchar *name = NULL;
        gint32 value = 0;
        if (g_strcmp0(method, "GetVolume") == 0) g_variant_get(params, "(&s)", &name);
        else g_variant_get(params, "(&si)", &name, &value);
        int stream = stream_from_name(name);
        if (stream < 0) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                G_DBUS_ERROR_INVALID_ARGS, "Unknown stream: %s", name);
            return;
        }
        if (g_strcmp0(method, "GetVolume") == 0) {
            g_dbus_method_invocation_return_value(inv, g_variant_new("(i)",
                zyl_audio_get_volume(svc, (ZylAudioStream)stream)));
            return;
        }
        /* Same answer as the emulator's audio service: { stream, value } */
        GVariantBuilder d;
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "stream", g_variant_new_string(name));
        int set = zyl_audio_set_volume(svc, (ZylAudioStream)stream, value);
        g_variant_builder_add(&d, "{sv}", "value", g_variant_new_int32(set));
        save_volume(svc, name, set);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "SetMicMute") == 0) {
        gboolean muted = FALSE;
        g_variant_get(params, "(b)", &muted);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)",
            zyl_hal_audio()->set_input_mute(muted)));
    } else if (g_strcmp0(method, "SetSpeakerphone") == 0) {
        gboolean on = FALSE;
        g_variant_get(params, "(b)", &on);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)",
            zyl_hal_audio()->set_speakerphone(on)));
    } else if (g_strcmp0(method, "Vibrate") == 0) {
        GVariant *arr = g_variant_get_child_value(params, 0);
        gsize n = 0;
        const gint32 *pattern = g_variant_get_fixed_array(arr, &n, sizeof(gint32));
        bool done = n > 0 && vibration_wanted(svc) &&
                    zyl_hal_vibrator()->vibrate(pattern, (int)n);
        g_variant_unref(arr);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", done));
    } else if (g_strcmp0(method, "SetSilentMode") == 0) {
        gboolean on = FALSE;
        g_variant_get(params, "(b)", &on);
        svc->silent = on;
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", TRUE));
    } else if (g_strcmp0(method, "GetMute") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", zyl_audio_get_mute(svc)));
    } else if (g_strcmp0(method, "SetMute") == 0) {
        gboolean mute;
        g_variant_get(params, "(b)", &mute);
        zyl_audio_set_mute(svc, mute);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "PlayTone") == 0) {
        gint32 freq, dur;
        g_variant_get(params, "(ii)", &freq, &dur);
        zyl_audio_play_alert_tone(svc, freq, dur);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "PlayKeyClick") == 0) {
        /* Keyboard feedback stays quiet in silent mode, unlike PlayTone
           (the emergency alert tone, which sounds regardless) */
        if (!svc->silent && !svc->muted) zyl_audio_play_tone(svc, 800, 30);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "PlayNotificationSound") == 0) {
        play_notification_sound(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

/* Silent mode and the volumes the user left (Settings keeps sound.silentMode and
   sound.<stream>Volume — JSON numbers, so int64 or double, int32 from older writers) */
static void on_saved_sound(GObject *src, GAsyncResult *res, gpointer data) {
    ZylAudioService *svc = data;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    if (!r) return;
    GVariant *d = g_variant_get_child_value(r, 0);
    gboolean on = FALSE;
    if (g_variant_lookup(d, "silentMode", "b", &on)) svc->silent = on;
    for (int i = 0; i < (int)G_N_ELEMENTS(STREAM_NAMES); i++) {
        char key[32];
        snprintf(key, sizeof(key), "%sVolume", STREAM_NAMES[i]);
        GVariant *v = g_variant_lookup_value(d, key, NULL);
        if (!v) continue;
        if (g_variant_is_of_type(v, G_VARIANT_TYPE_INT64))
            zyl_audio_set_volume(svc, (ZylAudioStream)i, (int)CLAMP(g_variant_get_int64(v), 0, 100));
        else if (g_variant_is_of_type(v, G_VARIANT_TYPE_INT32))
            zyl_audio_set_volume(svc, (ZylAudioStream)i, g_variant_get_int32(v));
        else if (g_variant_is_of_type(v, G_VARIANT_TYPE_DOUBLE))
            zyl_audio_set_volume(svc, (ZylAudioStream)i, (int)CLAMP(g_variant_get_double(v), 0, 100));
        g_variant_unref(v);
    }
    g_variant_unref(d);
    g_variant_unref(r);
}

static const GDBusInterfaceVTable audio_vtable = {
    .method_call = handle_audio_method
};

static void on_audio_bus(GDBusConnection *conn, const gchar *name,
                          gpointer data) {
    ZylAudioService *svc = data;
    (void)name;
    svc->dbus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        audio_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_AUDIO_DBUS_PATH,
            info->interfaces[0], &audio_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Audio] D-Bus registered: %s", ZYL_AUDIO_DBUS_NAME);
    g_dbus_connection_call(conn, "org.zylos.Settings", "/org/zylos/Settings", "org.zylos.Settings",
        "Get", g_variant_new("(s)", "sound"), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, on_saved_sound, svc);
}

/* ─── Public API ─── */

ZylAudioService *zyl_audio_create(void) {
    ZylAudioService *svc = calloc(1, sizeof(ZylAudioService));
    if (!svc) return NULL;
    /* Default volumes */
    svc->volumes[ZYL_AUDIO_STREAM_MEDIA]    = 70;
    svc->volumes[ZYL_AUDIO_STREAM_NOTIF]    = 80;
    svc->volumes[ZYL_AUDIO_STREAM_ALARM]    = 90;
    svc->volumes[ZYL_AUDIO_STREAM_RINGTONE] = 80;
    svc->volumes[ZYL_AUDIO_STREAM_SYSTEM]   = 50;
    svc->muted = false;

    /* The media stream is the device output: start from what it is set to */
    int out = zyl_hal_audio()->get_output_volume();
    if (out >= 0) svc->volumes[ZYL_AUDIO_STREAM_MEDIA] = out;

    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
        ZYL_AUDIO_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_audio_bus, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_audio_destroy(ZylAudioService *svc) {
    if (!svc) return;
    if (svc->alert_end) g_source_remove(svc->alert_end);
    g_bus_unown_name(svc->dbus_owner_id);
    free(svc);
}

int zyl_audio_get_volume(ZylAudioService *svc, ZylAudioStream stream) {
    if (!svc || stream < 0 || stream > 4) return 0;
    return svc->volumes[stream];
}

int zyl_audio_set_volume(ZylAudioService *svc, ZylAudioStream stream,
                          int percent) {
    if (!svc || stream < 0 || stream > 4) return -1;
    int clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    svc->volumes[stream] = clamped;
    /* during an alert the output is the alert's; its end applies this */
    if (stream == ZYL_AUDIO_STREAM_MEDIA && !svc->alerting) zyl_hal_audio()->set_output_volume(clamped);
    g_message("[Audio] Volume stream=%d → %d%%", stream, clamped);
    return clamped;
}

bool zyl_audio_get_mute(ZylAudioService *svc) {
    return svc ? svc->muted : false;
}

void zyl_audio_set_mute(ZylAudioService *svc, bool muted) {
    if (!svc) return;
    svc->muted = muted;
    if (!svc->alerting) zyl_hal_audio()->set_output_mute(muted);
    g_message("[Audio] Mute: %s", muted ? "ON" : "OFF");
}

/* A tone at the system volume (key clicks) */
int zyl_audio_play_tone(ZylAudioService *svc, int freq_hz,
                         int duration_ms) {
    if (!svc) return -1;
    return zyl_hal_audio()->play_tone(freq_hz, duration_ms,
                                      svc->volumes[ZYL_AUDIO_STREAM_SYSTEM]) ? 0 : -1;
}

/* ─── Emergency alert tone ───
   Heard whatever the user turned down: the tone was made at the system volume
   and played on an output at the media volume, muted with it — a presidential
   alert came silent. While it sounds the output is unmuted and full, the tone
   at full amplitude; ALERT_TAIL_MS after the last tone the user's own volume
   and mute come back (a change made meanwhile is the one restored). Only
   system apps may ask (bridge METHOD_PERMISSIONS audio.playTone). */
#define ALERT_TAIL_MS   300
#define ALERT_MAX_MS    10000   /* one tone: the HAL writes it to /tmp (RAM) whole */

static gboolean alert_over(gpointer data) {
    ZylAudioService *svc = data;
    svc->alert_end = 0;
    svc->alerting = false;
    zyl_hal_audio()->set_output_volume(svc->volumes[ZYL_AUDIO_STREAM_MEDIA]);
    zyl_hal_audio()->set_output_mute(svc->muted);
    return G_SOURCE_REMOVE;
}

int zyl_audio_play_alert_tone(ZylAudioService *svc, int freq_hz, int duration_ms) {
    if (!svc || freq_hz < 20 || freq_hz > 20000 || duration_ms <= 0) return -1;
    if (duration_ms > ALERT_MAX_MS) duration_ms = ALERT_MAX_MS;
    if (!svc->alerting) {
        svc->alerting = true;
        zyl_hal_audio()->set_output_mute(false);
        zyl_hal_audio()->set_output_volume(100);
    }
    if (svc->alert_end) g_source_remove(svc->alert_end);
    svc->alert_end = g_timeout_add((guint)duration_ms + ALERT_TAIL_MS, alert_over, svc);
    return zyl_hal_audio()->play_tone(freq_hz, duration_ms, 100) ? 0 : -1;
}

/* ─── 데몬 진입점 ─── */

#ifndef ZYL_TEST
static GMainLoop *g_audio_loop = NULL;

static gboolean on_signal_audio(gpointer data) {
    (void)data;
    g_message("[Audio] Signal received, shutting down");
    if (g_audio_loop) g_main_loop_quit(g_audio_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ZylAudioService *svc = zyl_audio_create();
    if (!svc) { g_critical("[Audio] Failed to create service"); return 1; }
    g_message("[Audio] Zyl OS Audio Service started");
    g_audio_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_audio, NULL);
    g_unix_signal_add(SIGINT,  on_signal_audio, NULL);
    g_main_loop_run(g_audio_loop);
    g_main_loop_unref(g_audio_loop);
    zyl_audio_destroy(svc);
    return 0;
}
#endif /* ZYL_TEST */
