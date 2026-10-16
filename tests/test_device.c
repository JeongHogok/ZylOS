/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 기기 정보 서비스(runtime/services/device) 단위 테스트
 * 수행범위: GetInfo 의 필드(에뮬레이터와 같은 이름), 보드가 말한 값의 표시
 *           형식(메모리 GB, "Linux <커널>"), 이미지의 OS 버전·보안 패치, 해상도는
 *           연결되고 모드가 있는 첫 출력의 첫(선호) 모드, 모르는 값은 지어내지 않고
 *           빈 문자열(출력 목록을 못 읽어도)
 * 의존방향: runtime/services/device/device.c (ZYL_TEST), 이 파일의 가짜 Board·Display HAL, gio
 * SOLID: SRP — 기기 정보 조회 결과 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <string.h>
#include "../runtime/services/device/device.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); fflush(stdout); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)

/* ─── The device: fake Board and Display HALs ─── */
static ZylBoardInfo board;
static ZylDisplayOutput outputs[4];
static int output_count;                             /* -1: unreadable */
static bool hal_board(ZylBoardInfo *out) { *out = board; return true; }
static const ZylBoardHal fake_board = { .get = hal_board };
const ZylBoardHal *zyl_hal_board(void) { return &fake_board; }
static int hal_outputs(ZylDisplayOutput *out, int max) {
    if (output_count < 0) return -1;
    int n = MIN(output_count, max);
    memcpy(out, outputs, (size_t)n * sizeof(*out));
    return n;
}
static const ZylDisplayHal fake_display = { .list_outputs = hal_outputs };
const ZylDisplayHal *zyl_hal_display(void) { return &fake_display; }

static void reset(void) {
    memset(&board, 0, sizeof(board));
    memset(outputs, 0, sizeof(outputs));
    output_count = 0;
}

static GVariant *info;
static const char *field(const char *key) {
    const char *v = NULL;
    assert(g_variant_lookup(info, key, "&s", &v));
    return v;
}

static void get_info(void) {
    if (info) g_variant_unref(info);
    GVariant *r = info_variant();
    assert(g_variant_is_of_type(r, G_VARIANT_TYPE("(a{sv})")));
    info = g_variant_get_child_value(r, 0);
    g_variant_unref(r);
}

static void output(int i, const char *name, bool connected, int modes, int w, int h) {
    g_strlcpy(outputs[i].name, name, sizeof(outputs[i].name));
    outputs[i].connected = connected;
    outputs[i].mode_count = modes;
    for (int m = 0; m < modes; m++) outputs[i].modes[m] = (ZylDisplayModeInfo){ w >> m, h >> m, 60 };
}

static void test_info_shows_what_the_board_says(void) {
    g_strlcpy(board.model, "Banana Pi BPI-F3", sizeof(board.model));
    g_strlcpy(board.soc, "spacemit,k1-x", sizeof(board.soc));
    g_strlcpy(board.kernel, "6.6.36", sizeof(board.kernel));
    g_strlcpy(board.device_id, "c0ffee0123456789", sizeof(board.device_id));
    board.ram_bytes = 4ULL * 1024 * 1024 * 1024;
    get_info();
    assert(strcmp(field("deviceName"), "Banana Pi BPI-F3") == 0);
    assert(strcmp(field("soc"), "spacemit,k1-x") == 0);
    assert(strcmp(field("ram"), "4.0 GB") == 0);
    assert(strcmp(field("kernel"), "Linux 6.6.36") == 0);
    assert(strcmp(field("deviceId"), "c0ffee0123456789") == 0);
    /* The image's release (meson passes it in) */
    assert(strcmp(field("osVersion"), ZYL_OS_VERSION) == 0 && strcmp(field("build"), ZYL_OS_VERSION) == 0);
    assert(strcmp(field("securityPatch"), ZYL_SECURITY_PATCH) == 0);
    char host[64] = "";
    gethostname(host, sizeof(host) - 1);
    assert(strcmp(field("hostname"), host) == 0);
    board.ram_bytes = 1536ULL * 1024 * 1024;
    get_info();
    assert(strcmp(field("ram"), "1.5 GB") == 0);
}

static void test_resolution_is_the_first_connected_output_with_a_mode(void) {
    output(0, "HDMI-A-1", false, 2, 3840, 2160);       /* nothing plugged in */
    output(1, "DSI-1", true, 0, 0, 0);                 /* connected, no modes read */
    output(2, "HDMI-A-2", true, 3, 1920, 1080);        /* preferred mode first */
    output(3, "DP-1", true, 1, 2560, 1440);
    output_count = 4;
    get_info();
    assert(strcmp(field("resolution"), "1920x1080") == 0);
}

static void test_what_is_not_known_is_empty_not_made_up(void) {
    output_count = -1;
    get_info();
    const char *keys[] = { "deviceName", "soc", "ram", "kernel", "resolution", "deviceId" };
    for (size_t i = 0; i < G_N_ELEMENTS(keys); i++) assert(*field(keys[i]) == '\0');
    output(0, "HDMI-A-1", false, 1, 1280, 720);
    output_count = 1;
    get_info();
    assert(*field("resolution") == '\0');
}

int main(void) {
    printf("=== Zyl OS Device Info Tests ===\n");
    RUN_TEST(test_info_shows_what_the_board_says);
    RUN_TEST(test_resolution_is_the_first_connected_output_with_a_mode);
    RUN_TEST(test_what_is_not_known_is_empty_not_made_up);
    if (info) g_variant_unref(info);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    (void)vtable; (void)device_xml;
    return tests_pass == tests_run ? 0 : 1;
}
