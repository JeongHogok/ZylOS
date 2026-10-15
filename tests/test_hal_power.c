/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: Display·Battery·Torch·CPU HAL Linux 구현 단위 테스트 (가짜 sysfs)
 * 수행범위: DRM 출력·모드 읽기(카드·렌더 노드 제외), 백라이트 백분율 ↔ 원시값,
 *           백라이트 없음, 배터리 잔량·충전·외부 전원과 배터리 없는 보드,
 *           플래시 LED 손전등 켜기·끄기와 없음, cpufreq 거버너·코어 온오프
 * 의존방향: runtime/hal/hal_{display,battery,torch,cpu}.c (ZYL_SYSFS_ROOT 로 빌드)
 * SOLID: SRP — 전원·화면 장치 접근 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "hal.h"

#define R ZYL_SYSFS_ROOT
static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); assert(system("rm -rf " R) == 0); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void put(const char *path, const char *text) {
    char *dir = g_path_get_dirname(path);
    assert(g_mkdir_with_parents(dir, 0755) == 0);
    assert(g_file_set_contents(path, text, -1, NULL));
    g_free(dir);
}

static bool is(const char *path, const char *want) {
    char *t = NULL;
    bool r = g_file_get_contents(path, &t, NULL, NULL) && strcmp(g_strstrip(t), want) == 0;
    g_free(t);
    return r;
}

static void test_outputs_and_their_modes(void) {
    put(R "/sys/class/drm/card0-DSI-1/status", "connected\n");
    put(R "/sys/class/drm/card0-DSI-1/modes", "720x1440\n640x480\n");
    put(R "/sys/class/drm/card0-HDMI-A-1/status", "disconnected\n");
    put(R "/sys/class/drm/card0/dev", "226:0\n");
    put(R "/sys/class/drm/renderD128/dev", "226:128\n");
    ZylDisplayOutput o[4];
    int n = zyl_hal_display()->list_outputs(o, 4);
    assert(n == 2);
    const ZylDisplayOutput *dsi = strcmp(o[0].name, "DSI-1") == 0 ? &o[0] : &o[1];
    assert(dsi->connected && dsi->mode_count == 2);
    assert(dsi->modes[0].width == 720 && dsi->modes[0].height == 1440 && dsi->modes[0].refresh_hz == 60);
}

static void test_backlight_percent_maps_to_the_device_range(void) {
    put(R "/sys/class/backlight/panel/max_brightness", "1023\n");
    put(R "/sys/class/backlight/panel/brightness", "512\n");
    const ZylDisplayHal *d = zyl_hal_display();
    assert(d->backlight_present() && d->get_brightness() == 50);
    assert(d->set_brightness(100) && is(R "/sys/class/backlight/panel/brightness", "1023"));
    assert(d->set_brightness(-5) && is(R "/sys/class/backlight/panel/brightness", "0"));
}

static void test_no_backlight_is_said_not_invented(void) {
    assert(!zyl_hal_display()->backlight_present());
    assert(zyl_hal_display()->get_brightness() == -1 && !zyl_hal_display()->set_brightness(50));
}

static void test_battery_level_charging_and_mains(void) {
    put(R "/sys/class/power_supply/BAT0/type", "Battery\n");
    put(R "/sys/class/power_supply/BAT0/capacity", "57\n");
    put(R "/sys/class/power_supply/BAT0/status", "Discharging\n");
    put(R "/sys/class/power_supply/AC/type", "Mains\n");
    put(R "/sys/class/power_supply/AC/online", "1\n");
    ZylBatteryState b;
    assert(zyl_hal_battery()->get(&b));
    assert(b.present && b.level_percent == 57 && !b.charging && b.plugged);
}

static void test_a_board_without_a_battery(void) {
    put(R "/sys/class/power_supply/usb/type", "USB\n");
    put(R "/sys/class/power_supply/usb/online", "1\n");
    ZylBatteryState b;
    assert(zyl_hal_battery()->get(&b));
    assert(!b.present && b.level_percent == -1 && b.plugged);
}

static void test_flash_led_is_the_torch(void) {
    const ZylTorchHal *t = zyl_hal_torch();
    put(R "/sys/class/leds/green:status/max_brightness", "1\n");
    assert(!t->present() && !t->set(true));          /* a status LED is not a torch */
    put(R "/sys/class/leds/white:flash/max_brightness", "255\n");
    put(R "/sys/class/leds/white:flash/brightness", "0\n");
    assert(t->present() && !t->get());
    assert(t->set(true) && is(R "/sys/class/leds/white:flash/brightness", "255") && t->get());
    assert(t->set(false) && is(R "/sys/class/leds/white:flash/brightness", "0"));
}

static void test_cpu_governor_and_cores(void) {
    for (int i = 0; i < 4; i++) {
        char p[256];
        snprintf(p, sizeof(p), R "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
        put(p, "schedutil\n");
        snprintf(p, sizeof(p), R "/sys/devices/system/cpu/cpu%d/online", i);
        put(p, "1\n");
    }
    put(R "/sys/devices/system/cpu/cpufreq/policy0", "");   /* not a core */
    const ZylCpuHal *c = zyl_hal_cpu();
    assert(c->core_count() == 4);
    assert(c->set_governor("powersave") && is(R "/sys/devices/system/cpu/cpu3/cpufreq/scaling_governor", "powersave"));
    assert(!c->set_core_online(0, false));             /* cpu0 always stays */
    assert(c->set_core_online(2, false) && is(R "/sys/devices/system/cpu/cpu2/online", "0"));
}

int main(void) {
    printf("=== Display / Battery / Torch / CPU HAL (linux) ===\n");
    RUN_TEST(test_outputs_and_their_modes);
    RUN_TEST(test_backlight_percent_maps_to_the_device_range);
    RUN_TEST(test_no_backlight_is_said_not_invented);
    RUN_TEST(test_battery_level_charging_and_mains);
    RUN_TEST(test_a_board_without_a_battery);
    RUN_TEST(test_flash_led_is_the_torch);
    RUN_TEST(test_cpu_governor_and_cores);
    assert(system("rm -rf " R) == 0);
    printf("Results: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
