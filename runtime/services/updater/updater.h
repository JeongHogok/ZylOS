/*
 * BPI-OS Updater: Over-The-Air (OTA) Update Service
 *
 * A/B 파티션 기반 원자적 시스템 업데이트를 관리합니다.
 * 업데이트 실패 시 자동 롤백되며, 부분 업데이트로 인한
 * 시스템 손상을 방지합니다.
 *
 * 업데이트 흐름:
 *   1. 서버에서 업데이트 매니페스트 확인
 *   2. 델타 패키지 다운로드 (또는 전체 이미지)
 *   3. 비활성 파티션(B)에 적용
 *   4. 부트로더 플래그 설정 (다음 부팅 시 B 파티션 사용)
 *   5. 재부팅 후 검증 성공 시 A/B 스왑 확정
 *   6. 검증 실패 시 자동으로 이전 파티션으로 롤백
 *
 * Copyright (c) 2026 BPI-OS Project
 * SPDX-License-Identifier: MIT
 */

#ifndef BPI_UPDATER_H
#define BPI_UPDATER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ─── 업데이트 상태 ─── */
typedef enum {
    BPI_UPDATE_IDLE,            /* 대기 중 */
    BPI_UPDATE_CHECKING,        /* 업데이트 확인 중 */
    BPI_UPDATE_AVAILABLE,       /* 업데이트 있음 */
    BPI_UPDATE_DOWNLOADING,     /* 다운로드 중 */
    BPI_UPDATE_VERIFYING,       /* 검증 중 */
    BPI_UPDATE_APPLYING,        /* 적용 중 */
    BPI_UPDATE_PENDING_REBOOT,  /* 재부팅 대기 */
    BPI_UPDATE_ROLLING_BACK,    /* 롤백 중 */
    BPI_UPDATE_FAILED,          /* 실패 */
    BPI_UPDATE_UP_TO_DATE,      /* 최신 */
} BpiUpdateState;

/* ─── 업데이트 유형 ─── */
typedef enum {
    BPI_UPDATE_TYPE_FULL,       /* 전체 시스템 이미지 */
    BPI_UPDATE_TYPE_DELTA,      /* 차이분만 적용 */
    BPI_UPDATE_TYPE_APPS_ONLY,  /* 시스템 앱만 업데이트 */
    BPI_UPDATE_TYPE_KERNEL,     /* 커널만 업데이트 */
} BpiUpdateType;

/* ─── 업데이트 매니페스트 ─── */
typedef struct {
    char *version;              /* 새 버전 (예: 0.2.0) */
    char *current_version;      /* 현재 버전 */
    char *changelog;            /* 변경 로그 (다국어) */
    char *download_url;         /* 다운로드 URL */
    size_t download_size;       /* 다운로드 크기 (바이트) */
    size_t installed_size;      /* 설치 후 크기 */
    char *sha256_hash;          /* 패키지 SHA-256 */
    char *signature;            /* RSA 서명 */
    BpiUpdateType type;         /* 업데이트 유형 */
    bool is_mandatory;          /* 필수 업데이트 여부 */
    char *min_battery_pct;      /* 최소 배터리 요구량 */
} BpiUpdateManifest;

/* ─── 진행률 콜백 ─── */
typedef void (*bpi_update_progress_fn)(
    BpiUpdateState state,
    int progress_pct,       /* 0-100 */
    const char *message,    /* 상태 메시지 */
    void *user_data
);

/* ─── A/B 파티션 정보 ─── */
typedef struct {
    char *active_slot;      /* 현재 활성 슬롯: "a" 또는 "b" */
    char *inactive_slot;    /* 비활성 슬롯 */
    char *active_version;   /* 활성 슬롯 OS 버전 */
    char *inactive_version; /* 비활성 슬롯 OS 버전 */
    bool verified;          /* 현재 슬롯 검증 완료 여부 */
} BpiPartitionInfo;

/* ─── 업데이터 서비스 인터페이스 ─── */
typedef struct BpiUpdater BpiUpdater;

/*
 * 업데이터 생성
 * update_server_url: OTA 서버 URL
 * cache_dir: 다운로드 캐시 디렉토리
 */
BpiUpdater *bpi_updater_create(const char *update_server_url,
                                const char *cache_dir);

/* 업데이터 해제 */
void bpi_updater_destroy(BpiUpdater *updater);

/* 업데이트 확인 */
BpiUpdateState bpi_updater_check(BpiUpdater *updater,
                                  BpiUpdateManifest **out_manifest);

/* 업데이트 다운로드 시작 (비동기) */
bool bpi_updater_download(BpiUpdater *updater,
                           bpi_update_progress_fn callback,
                           void *user_data);

/* 다운로드된 업데이트 적용 (비활성 파티션에) */
bool bpi_updater_apply(BpiUpdater *updater,
                        bpi_update_progress_fn callback,
                        void *user_data);

/* 재부팅하여 업데이트 완료 */
bool bpi_updater_reboot_to_update(BpiUpdater *updater);

/* 현재 슬롯을 검증 완료로 마킹 (성공적 부팅 후) */
bool bpi_updater_mark_verified(BpiUpdater *updater);

/* 이전 슬롯으로 롤백 */
bool bpi_updater_rollback(BpiUpdater *updater);

/* 현재 상태 조회 */
BpiUpdateState bpi_updater_get_state(const BpiUpdater *updater);

/* 파티션 정보 조회 */
BpiPartitionInfo *bpi_updater_get_partition_info(const BpiUpdater *updater);

/* 자동 업데이트 스케줄 설정 */
void bpi_updater_set_auto_check(BpiUpdater *updater,
                                 bool enabled,
                                 int interval_hours);

/* 매니페스트 해제 */
void bpi_update_manifest_free(BpiUpdateManifest *manifest);

/* 파티션 정보 해제 */
void bpi_partition_info_free(BpiPartitionInfo *info);

#endif /* BPI_UPDATER_H */
