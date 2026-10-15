/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Audio HAL Linux 구현 — PipeWire 출력·마이크, 톤 재생, 통화 경로
 * 수행범위: wpctl 로 기본 출력의 볼륨·음소거와 기본 입력(마이크) 음소거,
 *           사인파 WAV 를 만들어 pw-play 로 재생(끝나면 임시 파일 삭제),
 *           pactl 포트 목록에서 이어피스·스피커가 둘 다 있는 출력을 찾아
 *           스피커폰 전환(set-sink-port). 둘 중 하나라도 없으면 전환 불가
 * 의존방향: hal.h (Domain), glib (g_spawn, child watch), json-glib (pactl -f json)
 * SOLID: SRP — 오디오 장치 접근만. 스트림별 볼륨·무음 정책은 audio 서비스 몫
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include "hal.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <json-glib/json-glib.h>

/* A tool on PATH (Debian keeps wpctl/pactl/pw-play in /usr/bin); stdout kept when out != NULL */
static bool run(const char *const argv[], char **out) {
    int status = -1;
    GSpawnFlags flags = G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL |
                        (out ? 0 : G_SPAWN_STDOUT_TO_DEV_NULL);
    return g_spawn_sync(NULL, (char **)argv, NULL, flags, NULL, NULL, out, NULL, &status, NULL) &&
           g_spawn_check_wait_status(status, NULL);
}

static int audio_get_output_volume(void) {
    const char *argv[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };
    char *out = NULL;
    float v = 0;
    int vol = -1;
    if (run(argv, &out) && sscanf(out, "Volume: %f", &v) == 1) vol = CLAMP((int)lroundf(v * 100), 0, 100);
    g_free(out);
    return vol;
}

static bool audio_set_output_volume(int percent) {
    char arg[16];
    snprintf(arg, sizeof(arg), "%.2f", CLAMP(percent, 0, 100) / 100.0);
    const char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", arg, NULL };
    return run(argv, NULL);
}

static bool set_mute(const char *target, bool mute) {
    const char *argv[] = { "wpctl", "set-mute", target, mute ? "1" : "0", NULL };
    return run(argv, NULL);
}

static bool audio_set_output_mute(bool mute) { return set_mute("@DEFAULT_AUDIO_SINK@", mute); }
static bool audio_set_input_mute(bool mute)  { return set_mute("@DEFAULT_AUDIO_SOURCE@", mute); }

/* ─── Tones ─── */
static bool write_tone_wav(int fd, int freq, int duration_ms, int volume_pct) {
    const int rate = 44100;
    if (duration_ms > 10000) duration_ms = 10000;
    int samples = rate * duration_ms / 1000;
    uint32_t data_size = (uint32_t)samples * 2, riff = 36 + data_size, byte_rate = rate * 2;
    uint8_t h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
                      0,0,0,0, 0,0,0,0, 2,0, 16,0, 'd','a','t','a', 0,0,0,0 };
    for (int i = 0; i < 4; i++) {
        h[4 + i]  = (uint8_t)(riff >> (8 * i));
        h[24 + i] = (uint8_t)((uint32_t)rate >> (8 * i));
        h[28 + i] = (uint8_t)(byte_rate >> (8 * i));
        h[40 + i] = (uint8_t)(data_size >> (8 * i));
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) return false;
    fwrite(h, 1, sizeof(h), f);
    float amp = 32767.0f * (float)CLAMP(volume_pct, 0, 100) / 100.0f * 0.5f;
    for (int i = 0; i < samples; i++) {
        int16_t s = (int16_t)(amp * sin(2.0 * G_PI * freq * i / rate));
        fwrite(&s, 2, 1, f);
    }
    return fclose(f) == 0;
}

static void on_tone_done(GPid pid, gint status, gpointer path) {
    (void)status;
    g_spawn_close_pid(pid);
    unlink(path);
    g_free(path);
}

static bool audio_play_tone(int freq_hz, int duration_ms, int volume_percent) {
    if (freq_hz <= 0 || duration_ms <= 0) return false;
    char *path = g_strdup("/tmp/zyl-tone-XXXXXX.wav");
    int fd = g_mkstemp_full(path, O_RDWR, 0600);
    if (fd < 0 || !write_tone_wav(fd, freq_hz, duration_ms, volume_percent)) {
        if (fd >= 0) unlink(path);
        g_free(path);
        return false;
    }
    const char *argv[] = { "pw-play", path, NULL };
    GPid pid;
    if (!g_spawn_async(NULL, (char **)argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                       G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                       NULL, NULL, &pid, NULL)) {
        unlink(path);
        g_free(path);
        return false;
    }
    g_child_watch_add(pid, on_tone_done, path);
    return true;
}

/* ─── Call routing ─── */
typedef struct { char sink[256], earpiece[128], speaker[128]; } CallRoute;

/* ALSA UCM names the call outputs; pactl reports them as port types or names */
static bool port_is(JsonObject *port, const char *const kinds[]) {
    const char *type = json_object_has_member(port, "type") ? json_object_get_string_member(port, "type") : "";
    const char *name = json_object_get_string_member(port, "name");
    for (int i = 0; kinds[i]; i++) {
        if ((type && g_ascii_strcasecmp(type, kinds[i]) == 0) ||
            (name && strcasestr(name, kinds[i]))) return true;
    }
    return false;
}

static bool find_call_route(CallRoute *r) {
    static const char *const EARPIECE[] = { "earpiece", "handset", NULL };
    static const char *const SPEAKER[]  = { "speaker", NULL };
    const char *argv[] = { "pactl", "-f", "json", "list", "sinks", NULL };
    char *out = NULL;
    bool found = false;
    JsonParser *p = json_parser_new();
    if (run(argv, &out) && json_parser_load_from_data(p, out, -1, NULL) &&
        JSON_NODE_HOLDS_ARRAY(json_parser_get_root(p))) {
        JsonArray *sinks = json_node_get_array(json_parser_get_root(p));
        for (guint i = 0; i < json_array_get_length(sinks) && !found; i++) {
            JsonObject *sink = json_array_get_object_element(sinks, i);
            if (!sink || !json_object_has_member(sink, "ports")) continue;
            memset(r, 0, sizeof(*r));
            JsonArray *ports = json_object_get_array_member(sink, "ports");
            for (guint k = 0; ports && k < json_array_get_length(ports); k++) {
                JsonObject *port = json_array_get_object_element(ports, k);
                const char *name = port ? json_object_get_string_member(port, "name") : NULL;
                if (!name) continue;
                if (port_is(port, EARPIECE)) g_strlcpy(r->earpiece, name, sizeof(r->earpiece));
                else if (port_is(port, SPEAKER)) g_strlcpy(r->speaker, name, sizeof(r->speaker));
            }
            const char *sink_name = json_object_get_string_member(sink, "name");
            found = r->earpiece[0] && r->speaker[0] && sink_name;
            if (found) g_strlcpy(r->sink, sink_name, sizeof(r->sink));
        }
    }
    g_object_unref(p);
    g_free(out);
    return found;
}

static bool audio_speakerphone_supported(void) {
    CallRoute r;
    return find_call_route(&r);
}

static bool audio_set_speakerphone(bool on) {
    CallRoute r;
    if (!find_call_route(&r)) return false;
    const char *argv[] = { "pactl", "set-sink-port", r.sink, on ? r.speaker : r.earpiece, NULL };
    return run(argv, NULL);
}

static const ZylAudioHal audio_hal = {
    .get_output_volume      = audio_get_output_volume,
    .set_output_volume      = audio_set_output_volume,
    .set_output_mute        = audio_set_output_mute,
    .set_input_mute         = audio_set_input_mute,
    .play_tone              = audio_play_tone,
    .speakerphone_supported = audio_speakerphone_supported,
    .set_speakerphone       = audio_set_speakerphone,
};

const ZylAudioHal *zyl_hal_audio(void) { return &audio_hal; }
