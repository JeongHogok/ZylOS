/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Facts
 *
 * 역할: 이 부팅이 복구 부팅인가 — /data 의 파티션을 열지 못해 zyl-recovery.service 가
 *       그 자리에 tmpfs 를 올렸다
 * 수행범위: 판정 하나. WAM 은 복구 화면만 띄우고(셸 정책 zyl_shell_recover), storage
 *           서비스는 그때만 사용자 데이터 파티션을 지운다(EraseUserData). 같은 판정을
 *           셸로: system/zyl-erase-data 의 findmnt
 * 의존방향: 없음 (헤더만)
 * SOLID: SRP — 사실 하나
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_RECOVERY_H
#define ZYL_RECOVERY_H

#include <stdbool.h>
#include <sys/vfs.h>
#include <linux/magic.h>

#ifndef ZYL_DATA_MOUNT
#define ZYL_DATA_MOUNT "/data"
#endif

/* The real /data is ext4 on its partition: tmpfs there is the recovery boot */
static inline bool zyl_recovery_mode(void) {
    struct statfs st;
    return statfs(ZYL_DATA_MOUNT, &st) == 0 && st.f_type == TMPFS_MAGIC;
}

#endif /* ZYL_RECOVERY_H */
