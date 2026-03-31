#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: PipeWire 오디오 서비스 — 볼륨 제어, 톤 재생
 * 수행범위: PipeWire 세션 연결, 볼륨 get/set, sine 톤 생성
 * 의존방향: audio.h, gio/gio.h, pipewire/pipewire.h
 * SOLID: SRP — 오디오 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <spawn.h>
#include <sys/wait.h>
#include <gio/gio.h>
#include <glib-unix.h>

/* PipeWire integration via wpctl (WirePlumber CLI).
 * Direct libpipewire API requires async event loops that conflict
 * with GMainLoop. wpctl provides synchronous volume control. */

#define TONE_TMP_TEMPLATE "/tmp/zyl-tone-XXXXXX.wav"

struct ZylAudioService {
    int volumes[5]; /* per-stream volume 0-100 */
    bool muted;
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* ─── PipeWire volume via wpctl ─── */

static int pw_set_volume(int percent) {
    float vol = (float)percent / 100.0f;
    char vol_str[16];
    snprintf(vol_str, sizeof(vol_str), "%.2f", vol);
    pid_t pid;
    char *argv[] = {"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", vol_str, NULL};
    char *envp[] = {"PATH=/usr/bin:/bin", NULL};
    int rc = posix_spawn(&pid, "/usr/bin/wpctl", NULL, NULL, argv, envp);
    if (rc != 0) return -1;
    int status;
    waitpid(pid, &status, 0);
    return status;
}

static int pw_get_volume(void) {
    FILE *fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null",
                     "r");
    if (!fp) return 50;
    char buf[128];
    int vol = 50;
    if (fgets(buf, sizeof(buf), fp)) {
        /* Output: "Volume: 0.70" */
        float v = 0.0f;
        if (sscanf(buf, "Volume: %f", &v) == 1) {
            vol = (int)(v * 100.0f);
        }
    }
    pclose(fp);
    return vol;
}

static void pw_set_mute(bool mute) {
    pid_t pid;
    char *argv[] = {"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", mute ? "1" : "0", NULL};
    char *envp[] = {"PATH=/usr/bin:/bin", NULL};
    int rc = posix_spawn(&pid, "/usr/bin/wpctl", NULL, NULL, argv, envp);
    if (rc == 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

/* ─── Tone generation via PipeWire (pw-play fallback) ─── */

static int generate_tone_wav(const char *path, int freq, int duration_ms,
                              int volume_pct) {
    int sample_rate = 44100;
    int num_samples = sample_rate * duration_ms / 1000;
    int data_size = num_samples * 2; /* 16-bit mono */
    int file_size = 44 + data_size;
    float amplitude = 32767.0f * ((float)volume_pct / 100.0f) * 0.5f;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* WAV header */
    uint8_t hdr[44] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        0,0,0,0, 0,0,0,0, 2,0, 16,0,
        'd','a','t','a', 0,0,0,0
    };
    /* Fill sizes */
    int riff_size = file_size - 8;
    hdr[4]  = riff_size & 0xFF; hdr[5]  = (riff_size >> 8) & 0xFF;
    hdr[6]  = (riff_size >> 16) & 0xFF; hdr[7]  = (riff_size >> 24) & 0xFF;
    hdr[24] = sample_rate & 0xFF; hdr[25] = (sample_rate >> 8) & 0xFF;
    hdr[26] = (sample_rate >> 16) & 0xFF; hdr[27] = (sample_rate >> 24) & 0xFF;
    int byte_rate = sample_rate * 2;
    hdr[28] = byte_rate & 0xFF; hdr[29] = (byte_rate >> 8) & 0xFF;
    hdr[30] = (byte_rate >> 16) & 0xFF; hdr[31] = (byte_rate >> 24) & 0xFF;
    hdr[40] = data_size & 0xFF; hdr[41] = (data_size >> 8) & 0xFF;
    hdr[42] = (data_size >> 16) & 0xFF; hdr[43] = (data_size >> 24) & 0xFF;
    fwrite(hdr, 1, 44, f);

    /* Generate sine wave */
    for (int i = 0; i < num_samples; i++) {
        double t = (double)i / (double)sample_rate;
        int16_t sample = (int16_t)(amplitude * sin(2.0 * M_PI * freq * t));
        fwrite(&sample, 2, 1, f);
    }
    fclose(f);
    return 0;
}

/* ─── D-Bus ─── */

static const char *audio_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_AUDIO_DBUS_NAME "'>"
    "    <method name='GetVolume'>"
    "      <arg type='i' name='stream' direction='in'/>"
    "      <arg type='i' name='volume' direction='out'/>"
    "    </method>"
    "    <method name='SetVolume'>"
    "      <arg type='i' name='stream' direction='in'/>"
    "      <arg type='i' name='percent' direction='in'/>"
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
    "  </interface>"
    "</node>";

static void handle_audio_method(GDBusConnection *conn, const gchar *sender,
                                 const gchar *path, const gchar *iface,
                                 const gchar *method, GVariant *params,
                                 GDBusMethodInvocation *inv, gpointer data) {
    ZylAudioService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "GetVolume") == 0) {
        gint32 stream;
        g_variant_get(params, "(i)", &stream);
        int vol = zyl_audio_get_volume(svc, (ZylAudioStream)stream);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(i)", vol));
    } else if (g_strcmp0(method, "SetVolume") == 0) {
        gint32 stream, percent;
        g_variant_get(params, "(ii)", &stream, &percent);
        zyl_audio_set_volume(svc, (ZylAudioStream)stream, percent);
        g_dbus_method_invocation_return_value(inv, NULL);
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
        zyl_audio_play_tone(svc, freq, dur);
        g_dbus_method_invocation_return_value(inv, NULL);
    }
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

    /* Sync with PipeWire */
    svc->volumes[ZYL_AUDIO_STREAM_MEDIA] = pw_get_volume();

    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_AUDIO_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_audio_bus, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_audio_destroy(ZylAudioService *svc) {
    if (!svc) return;
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
    /* Apply to PipeWire for media stream */
    if (stream == ZYL_AUDIO_STREAM_MEDIA) pw_set_volume(clamped);
    g_message("[Audio] Volume stream=%d → %d%%", stream, clamped);
    return clamped;
}

bool zyl_audio_get_mute(ZylAudioService *svc) {
    return svc ? svc->muted : false;
}

void zyl_audio_set_mute(ZylAudioService *svc, bool muted) {
    if (!svc) return;
    svc->muted = muted;
    pw_set_mute(muted);
    g_message("[Audio] Mute: %s", muted ? "ON" : "OFF");
}

int zyl_audio_play_tone(ZylAudioService *svc, int freq_hz,
                         int duration_ms) {
    if (!svc || svc->muted) return -1;
    if (freq_hz <= 0 || duration_ms <= 0) return -1;

    char tmp[] = TONE_TMP_TEMPLATE;
    int fd = mkstemps(tmp, 4);
    if (fd < 0) return -1;
    close(fd);

    int vol = svc->volumes[ZYL_AUDIO_STREAM_SYSTEM];
    if (generate_tone_wav(tmp, freq_hz, duration_ms, vol) < 0) {
        unlink(tmp);
        return -1;
    }

    /* Play via pw-play (PipeWire) using posix_spawn (no shell) */
    pid_t play_pid;
    char *play_argv[] = {"pw-play", tmp, NULL};
    char *play_envp[] = {"PATH=/usr/bin:/bin", NULL};
    int play_rc = posix_spawn(&play_pid, "/usr/bin/pw-play", NULL, NULL, play_argv, play_envp);
    if (play_rc != 0) {
        /* ALSA fallback */
        char *aplay_argv[] = {"aplay", tmp, NULL};
        posix_spawn(&play_pid, "/usr/bin/aplay", NULL, NULL, aplay_argv, play_envp);
    }

    /* Schedule cleanup */
    /* Note: in production, use g_timeout_add for cleanup.
       Tone files are small (<100KB) and /tmp is tmpfs. */
    return 0;
}

/* ─── 데몬 진입점 ─── */

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
    g_message("[Audio] Zyl OS Audio Service started (PipeWire/ALSA)");
    g_audio_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_signal_audio, NULL);
    g_unix_signal_add(SIGINT,  on_signal_audio, NULL);
    g_main_loop_run(g_audio_loop);
    g_main_loop_unref(g_audio_loop);
    zyl_audio_destroy(svc);
    return 0;
}
