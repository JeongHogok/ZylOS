/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Storage HAL Linux 구현 — 사용자 데이터 파티션 용량
 * 수행범위: /data 의 statvfs (따로 마운트되지 않았으면 /)
 * 의존방향: hal.h (Domain), statvfs
 * SOLID: SRP — 용량 읽기만
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <string.h>
#include <sys/statvfs.h>

#ifndef ZYL_DATA_MOUNT
#define ZYL_DATA_MOUNT "/data"
#endif

static bool storage_get(ZylStorageState *out) {
    memset(out, 0, sizeof(*out));
    struct statvfs sv;
    if (statvfs(ZYL_DATA_MOUNT, &sv) != 0 && statvfs("/", &sv) != 0) return false;
    out->total_bytes     = (uint64_t)sv.f_blocks * sv.f_frsize;
    out->available_bytes = (uint64_t)sv.f_bavail * sv.f_frsize;
    out->used_bytes      = out->total_bytes - (uint64_t)sv.f_bfree * sv.f_frsize;
    return true;
}

static const ZylStorageHal storage_hal = { .get = storage_get };

const ZylStorageHal *zyl_hal_storage(void) { return &storage_hal; }
