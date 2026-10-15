/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: Vibrator HAL Linux 구현(runtime/hal/hal_vibrator.c) 단위 테스트
 * 수행범위: force-feedback 비트맵에서 FF_RUMBLE 을 가진 입력 장치만 진동
 *           모터로 인정, 모터가 없으면 진동 요청을 거절
 * 의존방향: hal_vibrator.c (ZYL_SYSFS_ROOT 로 가짜 sysfs)
 * SOLID: SRP — 진동 모터 판정 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-hal-vib-test"
#define ZYL_SYSFS_ROOT ROOT_
#define ZYL_DEV_ROOT ROOT_
#include "../runtime/hal/hal_vibrator.c"
#include <assert.h>

static void device(const char *event, const char *ff_bits) {
    char *dir = g_strdup_printf(ROOT_ "/sys/class/input/%s/device/capabilities", event);
    assert(g_mkdir_with_parents(dir, 0755) == 0);
    char *file = g_strconcat(dir, "/ff", NULL);
    assert(g_file_set_contents(file, ff_bits, -1, NULL));
    g_free(file);
    g_free(dir);
}

int main(void) {
    printf("=== Vibrator HAL (linux) ===\n");
    assert(system("rm -rf " ROOT_) == 0);
    int pulse[] = { 30 };

    printf("  %-56s ", "no input devices: no motor, nothing vibrates");
    assert(!zyl_hal_vibrator()->present() && !zyl_hal_vibrator()->vibrate(pulse, 1));
    printf("PASS\n");

    printf("  %-56s ", "a keyboard without force feedback is not a motor");
    device("event0", "0\n");
    assert(!zyl_hal_vibrator()->present());
    printf("PASS\n");

    printf("  %-56s ", "a device with FF_RUMBLE is the motor");
    /* The kernel prints the bitmap as longs, most significant first */
    unsigned per = sizeof(unsigned long) * 8;
    char bits[64];
    if (FF_RUMBLE / per == 1) snprintf(bits, sizeof(bits), "%lx 0\n", 1UL << (FF_RUMBLE % per));
    else snprintf(bits, sizeof(bits), "%lx 0 0\n", 1UL << (FF_RUMBLE % per));
    device("event3", bits);
    char dev[256];
    assert(zyl_hal_vibrator()->present() && find_vibrator(dev, sizeof(dev)));
    assert(strcmp(dev, ROOT_ "/dev/input/event3") == 0);
    printf("PASS\n");

    assert(system("rm -rf " ROOT_) == 0);
    printf("\nResults: 3/3 passed\n");
    return 0;
}
