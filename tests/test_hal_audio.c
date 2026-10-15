/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: Audio HAL Linux 구현(runtime/hal/hal_audio.c) 단위 테스트
 * 수행범위: 이어피스·스피커가 둘 다 있는 출력만 스피커폰 지원(포트 전환
 *           인자), 스피커만 있는 보드·PipeWire 없는 기기는 지원 안 함, 출력
 *           볼륨 읽기·쓰기, 마이크 음소거가 기본 입력에 걸리는지
 * 의존방향: hal_audio.c, PATH 의 가짜 wpctl/pactl
 * SOLID: SRP — 오디오 장치 접근 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include "../runtime/hal/hal_audio.c"
#include <assert.h>
#include <sys/stat.h>

#define DIR_ "/tmp/zyl-hal-audio-test"
static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void put(const char *path, const char *text) {
    assert(g_file_set_contents(path, text, -1, NULL));
}

static char *calls(void) {
    char *t = NULL;
    if (!g_file_get_contents(DIR_ "/calls", &t, NULL, NULL)) t = g_strdup("");
    return t;
}

static void sinks(const char *json) {
    put(DIR_ "/sinks.json", json);
    unlink(DIR_ "/calls");
}

static const char *PHONE =
    "[{\"name\":\"alsa_output.platform-sound\",\"ports\":["
    "{\"name\":\"[Out] Earpiece\",\"type\":\"Earpiece\"},"
    "{\"name\":\"[Out] Speaker\",\"type\":\"Speaker\"}]},"
    "{\"name\":\"bluez_output.headset\",\"ports\":[{\"name\":\"headset-output\",\"type\":\"Headset\"}]}]";

static void test_speakerphone_switches_between_earpiece_and_speaker(void) {
    sinks(PHONE);
    const ZylAudioHal *a = zyl_hal_audio();
    assert(a->speakerphone_supported());
    assert(a->set_speakerphone(true) && a->set_speakerphone(false));
    char *c = calls();
    assert(strstr(c, "pactl set-sink-port alsa_output.platform-sound [Out] Speaker\n"));
    assert(strstr(c, "pactl set-sink-port alsa_output.platform-sound [Out] Earpiece\n"));
    g_free(c);
}

static void test_a_board_with_only_a_loudspeaker_has_no_speakerphone(void) {
    sinks("[{\"name\":\"alsa_output.hdmi\",\"ports\":[{\"name\":\"[Out] Speaker\",\"type\":\"Speaker\"}]}]");
    assert(!zyl_hal_audio()->speakerphone_supported());
    assert(!zyl_hal_audio()->set_speakerphone(true));
    char *c = calls();
    assert(!strstr(c, "set-sink-port"));       /* nothing switched */
    g_free(c);
}

static void test_no_pipewire_means_no_speakerphone(void) {
    unlink(DIR_ "/sinks.json");                /* fake pactl fails like a missing server */
    assert(!zyl_hal_audio()->speakerphone_supported());
}

static void test_output_volume_and_microphone_mute(void) {
    unlink(DIR_ "/calls");
    const ZylAudioHal *a = zyl_hal_audio();
    assert(a->get_output_volume() == 45);
    assert(a->set_output_volume(30) && a->set_input_mute(true));
    char *c = calls();
    assert(strstr(c, "wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.30\n"));
    assert(strstr(c, "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 1\n"));
    g_free(c);
}

int main(void) {
    printf("=== Audio HAL (linux) ===\n");
    assert(system("rm -rf " DIR_ " && mkdir -p " DIR_) == 0);
    put(DIR_ "/pactl", "#!/bin/sh\n"
        "[ \"$1\" = -f ] && { [ -f " DIR_ "/sinks.json ] || exit 1; exec cat " DIR_ "/sinks.json; }\n"
        "echo \"pactl $*\" >> " DIR_ "/calls\n");
    put(DIR_ "/wpctl", "#!/bin/sh\n"
        "[ \"$1\" = get-volume ] && { echo 'Volume: 0.45'; exit 0; }\n"
        "echo \"wpctl $*\" >> " DIR_ "/calls\n");
    assert(chmod(DIR_ "/pactl", 0755) == 0 && chmod(DIR_ "/wpctl", 0755) == 0);
    char *path = g_strconcat(DIR_ ":", g_getenv("PATH"), NULL);
    g_setenv("PATH", path, TRUE);
    g_free(path);
    RUN_TEST(test_speakerphone_switches_between_earpiece_and_speaker);
    RUN_TEST(test_a_board_with_only_a_loudspeaker_has_no_speakerphone);
    RUN_TEST(test_no_pipewire_means_no_speakerphone);
    RUN_TEST(test_output_volume_and_microphone_mute);
    assert(system("rm -rf " DIR_) == 0);
    printf("Results: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
