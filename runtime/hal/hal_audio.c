/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Implementation
 *
 * 역할: Audio HAL Linux 구현체 — PipeWire (wpctl) / ALSA 폴백
 * 수행범위: 볼륨 조절 (stream별), 진동/무음 모드, 상태 조회
 * 의존방향: hal.h (Domain), stdio/stdlib (Platform)
 * SOLID: DIP — hal.h 인터페이스 구현, SRP — 오디오 제어만 담당
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

#define SETTINGS_DIR "/data/settings"

static ZylAudioState g_audio = {
    .media_volume        = 70,
    .notification_volume = 80,
    .alarm_volume        = 90,
    .call_volume         = 70,
    .vibration           = true,
    .silent_mode         = false,
};

static bool g_has_wpctl = false;
static bool g_has_amixer = false;

/* ─── Helper: run command, capture first line ─── */
static int run_cmd(const char *cmd, char *out, size_t out_len) {
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    if (out && out_len > 0) {
        if (fgets(out, (int)out_len, p) == NULL) out[0] = '\0';
        size_t len = strlen(out);
        if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
    }
    return pclose(p);
}

/* ─── Helper: check if command exists ─── */
static bool cmd_exists(const char *cmd) {
    char buf[512];
    snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

/* ─── Settings persistence ─── */
static void save_audio_settings(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/audio_state", SETTINGS_DIR);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "media=%d\n", g_audio.media_volume);
    fprintf(f, "notification=%d\n", g_audio.notification_volume);
    fprintf(f, "alarm=%d\n", g_audio.alarm_volume);
    fprintf(f, "call=%d\n", g_audio.call_volume);
    fprintf(f, "vibration=%d\n", g_audio.vibration ? 1 : 0);
    fprintf(f, "silent=%d\n", g_audio.silent_mode ? 1 : 0);
    fclose(f);
}

static void load_audio_settings(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/audio_state", SETTINGS_DIR);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int val = 0;
        if (sscanf(line, "media=%d", &val) == 1) g_audio.media_volume = val;
        else if (sscanf(line, "notification=%d", &val) == 1) g_audio.notification_volume = val;
        else if (sscanf(line, "alarm=%d", &val) == 1) g_audio.alarm_volume = val;
        else if (sscanf(line, "call=%d", &val) == 1) g_audio.call_volume = val;
        else if (sscanf(line, "vibration=%d", &val) == 1) g_audio.vibration = (val != 0);
        else if (sscanf(line, "silent=%d", &val) == 1) g_audio.silent_mode = (val != 0);
    }
    fclose(f);
}

/* ─── Apply volume to system mixer ─── */
static int apply_volume(const char *stream, int percent) {
    (void)stream; /* All streams map to master for now — future: PipeWire node routing */
    char cmd[256];

    if (g_has_wpctl) {
        /* wpctl: volume is float 0.0-1.0 */
        snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f",
                 percent / 100.0);
        return run_cmd(cmd, NULL, 0);
    }
    if (g_has_amixer) {
        snprintf(cmd, sizeof(cmd), "amixer set Master %d%% 2>/dev/null", percent);
        return run_cmd(cmd, NULL, 0);
    }
    return -1;
}

/* ─── HAL implementation ─── */
static int audio_init(void) {
    g_has_wpctl = cmd_exists("wpctl");
    g_has_amixer = cmd_exists("amixer");
    load_audio_settings();
    return 0;
}

static void audio_shutdown(void) {
    save_audio_settings();
}

static int audio_set_volume(const char *stream, int percent) {
    if (!stream) return -1;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (strcmp(stream, "media") == 0)             g_audio.media_volume = percent;
    else if (strcmp(stream, "notification") == 0)  g_audio.notification_volume = percent;
    else if (strcmp(stream, "alarm") == 0)         g_audio.alarm_volume = percent;
    else if (strcmp(stream, "call") == 0)          g_audio.call_volume = percent;
    else return -1;

    save_audio_settings();
    return apply_volume(stream, percent);
}

static int audio_get_volume(const char *stream, int *out_percent) {
    if (!stream || !out_percent) return -1;

    if (strcmp(stream, "media") == 0)             *out_percent = g_audio.media_volume;
    else if (strcmp(stream, "notification") == 0)  *out_percent = g_audio.notification_volume;
    else if (strcmp(stream, "alarm") == 0)         *out_percent = g_audio.alarm_volume;
    else if (strcmp(stream, "call") == 0)          *out_percent = g_audio.call_volume;
    else return -1;

    return 0;
}

static int audio_set_vibration(bool enabled) {
    g_audio.vibration = enabled;
    save_audio_settings();
    return 0;
}

static int audio_set_silent_mode(bool enabled) {
    g_audio.silent_mode = enabled;
    save_audio_settings();
    if (enabled && (g_has_wpctl || g_has_amixer)) {
        apply_volume("media", 0);
    }
    return 0;
}

static int audio_get_state(ZylAudioState *out) {
    if (!out) return -1;
    *out = g_audio;
    return 0;
}

/* ─── HAL 인스턴스 ─── */
static ZylAudioHal audio_hal_instance = {
    .init            = audio_init,
    .shutdown        = audio_shutdown,
    .set_volume      = audio_set_volume,
    .get_volume      = audio_get_volume,
    .set_vibration   = audio_set_vibration,
    .set_silent_mode = audio_set_silent_mode,
    .get_state       = audio_get_state,
};

ZylAudioHal *zyl_hal_audio_linux(void) {
    return &audio_hal_instance;
}
