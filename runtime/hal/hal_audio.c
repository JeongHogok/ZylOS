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

/* Safe argv-based command execution with optional stdout capture. */
static int spawn_capture_first_line(const char *path,
                                    char *const argv[],
                                    char *out,
                                    size_t out_len) {
    int pipefd[2] = {-1, -1};
    posix_spawn_file_actions_t acts;
    posix_spawn_file_actions_init(&acts);

    if (out && out_len > 0) {
        if (pipe(pipefd) != 0) {
            posix_spawn_file_actions_destroy(&acts);
            return -1;
        }
        posix_spawn_file_actions_adddup2(&acts, pipefd[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&acts, pipefd[0]);
    }

    pid_t pid;
    char *envp[] = { "PATH=/usr/bin:/usr/sbin:/bin:/sbin", NULL };
    int rc = posix_spawn(&pid, path, &acts, NULL, argv, envp);
    posix_spawn_file_actions_destroy(&acts);
    if (out && out_len > 0) close(pipefd[1]);
    if (rc != 0) {
        if (out && out_len > 0) close(pipefd[0]);
        return -1;
    }

    if (out && out_len > 0) {
        ssize_t n = read(pipefd[0], out, out_len - 1);
        close(pipefd[0]);
        if (n > 0) {
            out[n] = '\0';
            size_t len = strlen(out);
            if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
        } else {
            out[0] = '\0';
        }
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

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

/* ─── Helper: check if command exists ─── */
static bool cmd_exists(const char *cmd) {
    /* Check common paths directly instead of using shell */
    const char *dirs[] = {"/usr/bin/", "/usr/sbin/", "/bin/", "/sbin/", NULL};
    char path[512];
    for (int i = 0; dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s%s", dirs[i], cmd);
        if (access(path, X_OK) == 0) return true;
    }
    return false;
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
    fflush(f);
    fsync(fileno(f));
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

    if (g_has_wpctl) {
        char vol_arg[16];
        snprintf(vol_arg, sizeof(vol_arg), "%.2f", percent / 100.0);
        char *argv[] = {
            "/usr/bin/wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", vol_arg, NULL
        };
        return spawn_capture_first_line("/usr/bin/wpctl", argv, NULL, 0);
    }
    if (g_has_amixer) {
        char pct_arg[16];
        snprintf(pct_arg, sizeof(pct_arg), "%d%%", percent);
        char *argv[] = {
            "/usr/bin/amixer", "set", "Master", pct_arg, NULL
        };
        if (access("/usr/bin/amixer", X_OK) == 0)
            return spawn_capture_first_line("/usr/bin/amixer", argv, NULL, 0);
        char *argv2[] = { "/bin/amixer", "set", "Master", pct_arg, NULL };
        return spawn_capture_first_line("/bin/amixer", argv2, NULL, 0);
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
