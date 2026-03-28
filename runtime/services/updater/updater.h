/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: OTA 업데이터 인터페이스 정의 — 업데이트 상태, 파티션, 검증 함수
 * 수행범위: ZylUpdateState/ZylPartition 타입, 업데이트 확인/적용/롤백 함수 선언
 * 의존방향: stdbool.h, stdint.h
 * SOLID: DIP — 업데이트 구현이 아닌 추상 인터페이스에 의존
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_UPDATER_H
#define ZYL_UPDATER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ─── 업데이트 상태 ─── */
typedef enum {
    ZYL_UPDATE_IDLE,            /* 대기 중 */
    ZYL_UPDATE_CHECKING,        /* 업데이트 확인 중 */
    ZYL_UPDATE_AVAILABLE,       /* 업데이트 있음 */
    ZYL_UPDATE_DOWNLOADING,     /* 다운로드 중 */
    ZYL_UPDATE_VERIFYING,       /* 검증 중 */
    ZYL_UPDATE_APPLYING,        /* 적용 중 */
    ZYL_UPDATE_PENDING_REBOOT,  /* 재부팅 대기 */
    ZYL_UPDATE_ROLLING_BACK,    /* 롤백 중 */
    ZYL_UPDATE_FAILED,          /* 실패 */
    ZYL_UPDATE_UP_TO_DATE,      /* 최신 */
} ZylUpdateState;

/* ─── 업데이트 유형 ─── */
typedef enum {
    ZYL_UPDATE_TYPE_FULL,       /* 전체 시스템 이미지 */
    ZYL_UPDATE_TYPE_DELTA,      /* 차이분만 적용 */
    ZYL_UPDATE_TYPE_APPS_ONLY,  /* 시스템 앱만 업데이트 */
    ZYL_UPDATE_TYPE_KERNEL,     /* 커널만 업데이트 */
} ZylUpdateType;

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
    ZylUpdateType type;         /* 업데이트 유형 */
    bool is_mandatory;          /* 필수 업데이트 여부 */
    char *min_battery_pct;      /* 최소 배터리 요구량 */
} ZylUpdateManifest;

/* ─── 진행률 콜백 ─── */
typedef void (*zyl_update_progress_fn)(
    ZylUpdateState state,
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
} ZylPartitionInfo;

/* ─── 업데이터 서비스 인터페이스 ─── */
typedef struct ZylUpdater ZylUpdater;

/*
 * 업데이터 생성
 * update_server_url: OTA 서버 URL
 * cache_dir: 다운로드 캐시 디렉토리
 */
ZylUpdater *zyl_updater_create(const char *update_server_url,
                                const char *cache_dir);

/* 업데이터 해제 */
void zyl_updater_destroy(ZylUpdater *updater);

/* 업데이트 확인 */
ZylUpdateState zyl_updater_check(ZylUpdater *updater,
                                  ZylUpdateManifest **out_manifest);

/* 업데이트 다운로드 시작 (비동기) */
bool zyl_updater_download(ZylUpdater *updater,
                           zyl_update_progress_fn callback,
                           void *user_data);

/* 다운로드된 업데이트 적용 (비활성 파티션에) */
bool zyl_updater_apply(ZylUpdater *updater,
                        zyl_update_progress_fn callback,
                        void *user_data);

/* 재부팅하여 업데이트 완료 */
bool zyl_updater_reboot_to_update(ZylUpdater *updater);

/* 현재 슬롯을 검증 완료로 마킹 (성공적 부팅 후) */
bool zyl_updater_mark_verified(ZylUpdater *updater);

/* 이전 슬롯으로 롤백 */
bool zyl_updater_rollback(ZylUpdater *updater);

/* 현재 상태 조회 */
ZylUpdateState zyl_updater_get_state(const ZylUpdater *updater);

/* 파티션 정보 조회 */
ZylPartitionInfo *zyl_updater_get_partition_info(const ZylUpdater *updater);

/* 자동 업데이트 스케줄 설정 */
void zyl_updater_set_auto_check(ZylUpdater *updater,
                                 bool enabled,
                                 int interval_hours);

/* 매니페스트 해제 */
void zyl_update_manifest_free(ZylUpdateManifest *manifest);

/* 파티션 정보 해제 */
void zyl_partition_info_free(ZylPartitionInfo *info);

#endif /* ZYL_UPDATER_H */
