/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Factory
 *
 * 역할: Linux HAL Registry 팩토리 — 모든 HAL 구현체를 조립
 * 수행범위: zyl_hal_create_linux() / zyl_hal_destroy() 구현
 * 의존방향: hal.h (Domain), 각 hal_*.c 구현체
 * SOLID: DIP — hal.h의 팩토리 함수 구현,
 *         OCP — 새 HAL 모듈 추가 시 이 파일만 수정
 * ────────────────────────────────────────────────────────── */

#include "../hal.h"

#include <stdlib.h>
#include <stdio.h>

/* 각 모듈의 Linux 구현체 팩토리 (해당 .c에서 정의) */
extern ZylWifiHal    *zyl_hal_wifi_linux(void);
extern ZylBtHal      *zyl_hal_bt_linux(void);
extern ZylDisplayHal *zyl_hal_display_linux(void);
extern ZylAudioHal   *zyl_hal_audio_linux(void);
extern ZylBatteryHal *zyl_hal_battery_linux(void);
extern ZylStorageHal *zyl_hal_storage_linux(void);

ZylHalRegistry *zyl_hal_create_linux(void) {
    ZylHalRegistry *reg = calloc(1, sizeof(ZylHalRegistry));
    if (!reg) return NULL;

    reg->wifi      = zyl_hal_wifi_linux();
    reg->bluetooth = zyl_hal_bt_linux();
    reg->display   = zyl_hal_display_linux();
    reg->audio     = zyl_hal_audio_linux();
    reg->battery   = zyl_hal_battery_linux();
    reg->storage   = zyl_hal_storage_linux();

    /* Initialize each module */
    int failures = 0;
    if (reg->wifi      && reg->wifi->init()      != 0) { fprintf(stderr, "[HAL] WiFi init failed\n");      failures++; }
    if (reg->bluetooth && reg->bluetooth->init()  != 0) { fprintf(stderr, "[HAL] Bluetooth init failed\n"); failures++; }
    if (reg->display   && reg->display->init()    != 0) { fprintf(stderr, "[HAL] Display init failed\n");   failures++; }
    if (reg->audio     && reg->audio->init()      != 0) { fprintf(stderr, "[HAL] Audio init failed\n");     failures++; }
    if (reg->battery   && reg->battery->init()    != 0) { fprintf(stderr, "[HAL] Battery init failed\n");   failures++; }
    if (reg->storage   && reg->storage->init()    != 0) { fprintf(stderr, "[HAL] Storage init failed\n");   failures++; }

    if (failures > 0) {
        fprintf(stderr, "[HAL] %d module(s) failed to initialize (non-fatal)\n", failures);
    }

    return reg;
}

void zyl_hal_destroy(ZylHalRegistry *hal) {
    if (!hal) return;

    if (hal->wifi      && hal->wifi->shutdown)      hal->wifi->shutdown();
    if (hal->bluetooth && hal->bluetooth->shutdown)  hal->bluetooth->shutdown();
    if (hal->display   && hal->display->shutdown)    hal->display->shutdown();
    if (hal->audio     && hal->audio->shutdown)      hal->audio->shutdown();
    if (hal->battery   && hal->battery->shutdown)    hal->battery->shutdown();
    if (hal->storage   && hal->storage->shutdown)    hal->storage->shutdown();

    free(hal);
}
