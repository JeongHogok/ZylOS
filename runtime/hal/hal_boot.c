/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Boot HAL Linux 구현 — U-Boot 환경 읽기·쓰기, A/B 슬롯 장치 배치
 * 수행범위: fw_printenv -n / fw_setenv (PATH 검색 — Debian 은 /usr/bin, 다른
 *           배포판은 /usr/sbin), 슬롯별 rootfs·verity 파티션, FIT 가 있는 부트
 *           디렉터리. 환경 변수 이름과 판단(잠금·시험 부팅)은 서비스 몫
 * 의존방향: hal.h (Domain), glib (g_spawn)
 * SOLID: SRP — 부트로더 환경 접근만. DIP — bootctl·updater 는 ZylBootHal 에만 의존
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>

/* Slot partitions: runtime/hal/meson.build passes them from the board's
   partitions.conf. Without a layout there are no slots, so no Boot HAL */
#ifndef ZYL_BOOT_DIR
#define ZYL_BOOT_DIR  "/boot"          /* FAT boot partition (p1) */
#endif

static char *boot_env_get(const char *name) {
    const char *argv[] = { "fw_printenv", "-n", name, NULL };
    char *out = NULL;
    int status = -1;
    if (!g_spawn_sync(NULL, (char **)argv, NULL,
                      G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, &out, NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL)) {
        g_free(out);
        return NULL;
    }
    char *value = strdup(g_strstrip(out));
    g_free(out);
    return value;
}

static bool boot_env_set(const char *name, const char *value) {
    const char *argv[] = { "fw_setenv", name, value, NULL };
    int status = -1;
    return g_spawn_sync(NULL, (char **)argv, NULL,
                        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                        G_SPAWN_STDERR_TO_DEV_NULL,
                        NULL, NULL, NULL, NULL, &status, NULL) &&
           g_spawn_check_wait_status(status, NULL);
}

static bool boot_present(void) {
#ifdef ZYL_SLOT_A_ROOTFS
    char *slot = boot_env_get("zyl_active_slot");
    bool present = slot != NULL;
    free(slot);
    return present;
#else
    return false;
#endif
}

#ifdef ZYL_SLOT_A_ROOTFS
static const char *boot_slot_rootfs(char slot) { return slot == 'a' ? ZYL_SLOT_A_ROOTFS : ZYL_SLOT_B_ROOTFS; }
static const char *boot_slot_verity(char slot) { return slot == 'a' ? ZYL_SLOT_A_VERITY : ZYL_SLOT_B_VERITY; }
#else
static const char *boot_slot_rootfs(char slot) { (void)slot; return NULL; }
static const char *boot_slot_verity(char slot) { (void)slot; return NULL; }
#endif
static const char *boot_dir(void) { return ZYL_BOOT_DIR; }

static const ZylBootHal boot_hal = {
    .present     = boot_present,
    .env_get     = boot_env_get,
    .env_set     = boot_env_set,
    .slot_rootfs = boot_slot_rootfs,
    .slot_verity = boot_slot_verity,
    .boot_dir    = boot_dir,
};

const ZylBootHal *zyl_hal_boot(void) { return &boot_hal; }
