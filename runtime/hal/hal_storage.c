/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Data Layer - Implementation
 *
 * 역할: Storage HAL Linux 구현체 — statvfs()
 * 수행범위: 디스크 사용량 조회 (total, used, available)
 * 의존방향: hal.h (Domain), sys/statvfs.h (Platform)
 * SOLID: DIP — hal.h 인터페이스 구현, SRP — 스토리지 정보만 담당
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

#define DATA_MOUNT_POINT "/data"

static int storage_init(void) {
    /* statvfs requires no initialization */
    return 0;
}

static void storage_shutdown(void) {
    /* Nothing to release */
}

static int storage_get_state(ZylStorageState *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    struct statvfs sv;
    if (statvfs(DATA_MOUNT_POINT, &sv) != 0) {
        /* Fallback to root if /data not mounted separately */
        if (statvfs("/", &sv) != 0) return -1;
    }

    out->total_bytes     = (uint64_t)sv.f_blocks * sv.f_frsize;
    out->available_bytes = (uint64_t)sv.f_bavail * sv.f_frsize;
    out->used_bytes      = out->total_bytes - (uint64_t)sv.f_bfree * sv.f_frsize;

    return 0;
}

/* ─── HAL 인스턴스 ─── */
static ZylStorageHal storage_hal_instance = {
    .init      = storage_init,
    .shutdown  = storage_shutdown,
    .get_state = storage_get_state,
};

ZylStorageHal *zyl_hal_storage_linux(void) {
    return &storage_hal_instance;
}
