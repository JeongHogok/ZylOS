/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Use Case
 *
 * 역할: 검증된 OTA 패키지를 한 슬롯에 설치 — rootfs, verity 트리, 서명 FIT
 * 수행범위: 패키지에서 그 슬롯의 멤버만 꺼내 Boot HAL 이 알려주는 장치·경로에
 *           바로 기록(tar -xO, 수 GiB 임시 복사 없음, 아카이브 경로가 기록 위치를
 *           정하지 못함), FIT 는 임시 이름으로 쓰고 교체해 반쯤 쓴 FIT 가 로드되지
 *           않게, 각 기록 fsync
 * 의존방향: hal.h (Boot HAL), tar
 * SOLID: SRP — 기록만. 언제·어느 슬롯에 쓰고 부트로더를 돌릴지는 updater.c
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_OTA_INSTALL_H
#define ZYL_OTA_INSTALL_H

#include <stdbool.h>

/* Progress of the install (pct 0-100 of this step) */
typedef void (*ZylOtaStep)(int pct, const char *msg, void *data);

/* Writes slot 'a' or 'b'. On failure *why says what did not get written */
bool zyl_ota_install_slot(const char *pkg_path, char slot,
                          ZylOtaStep step, void *data, const char **why);

#endif /* ZYL_OTA_INSTALL_H */
