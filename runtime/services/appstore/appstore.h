/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱스토어 인터페이스 정의 — 패키지 서명 상태, 설치/검증 함수
 * 수행범위: ZylSignatureStatus 열거형, 패키지 검증/설치/카탈로그 함수 선언
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 앱스토어 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_APPSTORE_H
#define ZYL_APPSTORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ─── 패키지 서명 상태 ─── */
typedef enum {
    ZYL_PKG_UNSIGNED,          /* 서명 없음 - 설치 거부 */
    ZYL_PKG_INVALID_SIGNATURE, /* 서명 검증 실패 */
    ZYL_PKG_EXPIRED_CERT,      /* 인증서 만료 */
    ZYL_PKG_REVOKED_CERT,      /* 인증서 폐기됨 */
    ZYL_PKG_VALID_SIGNATURE,   /* 유효한 서명 */
    ZYL_PKG_SYSTEM_TRUSTED,    /* 시스템 앱 (최고 신뢰) */
} ZylPkgSignatureStatus;

/* ─── 개발자 인증서 ─── */
typedef struct {
    char *developer_id;       /* 개발자 고유 ID */
    char *developer_name;     /* 개발자 이름 */
    char *public_key_pem;     /* RSA-2048 공개 키 (PEM) */
    uint64_t issued_at;       /* 발급 시각 (Unix timestamp) */
    uint64_t expires_at;      /* 만료 시각 */
    bool is_revoked;          /* 폐기 여부 */
} ZylDeveloperCert;

/* ─── 패키지 메타데이터 ─── */
typedef struct {
    char *app_id;             /* 앱 ID (예: com.example.myapp) */
    char *name;               /* 앱 이름 */
    char *version;            /* 시맨틱 버전 (예: 1.2.3) */
    char *description;        /* 앱 설명 */
    char *author;             /* 개발자 이름 */
    char *icon_path;          /* 아이콘 경로 (패키지 내) */
    char **permissions;       /* 요청 권한 목록 */
    int n_permissions;
    size_t package_size;      /* 패키지 크기 (바이트) */
    char *min_os_version;     /* 최소 OS 버전 */
    char *signature;          /* Base64 인코딩된 서명 */
    char *cert_fingerprint;   /* 서명에 사용된 인증서 SHA-256 */
} ZylPackageMeta;

/* ─── 설치 결과 ─── */
typedef enum {
    ZYL_INSTALL_SUCCESS,
    ZYL_INSTALL_ERR_UNSIGNED,
    ZYL_INSTALL_ERR_INVALID_SIG,
    ZYL_INSTALL_ERR_EXPIRED_CERT,
    ZYL_INSTALL_ERR_REVOKED_CERT,
    ZYL_INSTALL_ERR_PERMISSION_DENIED,
    ZYL_INSTALL_ERR_INSUFFICIENT_STORAGE,
    ZYL_INSTALL_ERR_INCOMPATIBLE_OS,
    ZYL_INSTALL_ERR_ALREADY_INSTALLED,
    ZYL_INSTALL_ERR_CORRUPT_PACKAGE,
    ZYL_INSTALL_ERR_IO,
} ZylInstallResult;

/* ─── AppStore 서비스 인터페이스 ─── */
typedef struct ZylAppStore ZylAppStore;

/*
 * 앱스토어 서비스 생성
 * trust_store_path: 신뢰할 수 있는 인증서 저장소 경로
 * app_install_dir: 앱 설치 디렉토리
 */
ZylAppStore *zyl_appstore_create(const char *trust_store_path,
                                  const char *app_install_dir);

/* 앱스토어 서비스 해제 */
void zyl_appstore_destroy(ZylAppStore *store);

/*
 * 패키지 서명 검증 (설치 전 단계)
 * package_path: .ospkg 파일 경로
 * out_meta: 검증 성공 시 메타데이터 반환
 * Returns: 서명 상태
 */
ZylPkgSignatureStatus zyl_appstore_verify_package(
    ZylAppStore *store,
    const char *package_path,
    ZylPackageMeta **out_meta);

/*
 * 앱 설치 (서명 검증 포함)
 * package_path: .ospkg 파일 경로
 * Returns: 설치 결과
 */
ZylInstallResult zyl_appstore_install(ZylAppStore *store,
                                      const char *package_path);

/* 앱 제거 (시스템 앱 제외) */
ZylInstallResult zyl_appstore_uninstall(ZylAppStore *store,
                                        const char *app_id);

/* 설치된 앱 목록 조회 */
int zyl_appstore_list_installed(ZylAppStore *store,
                                ZylPackageMeta ***out_apps,
                                int *out_count);

/* 개발자 인증서 등록 */
bool zyl_appstore_register_cert(ZylAppStore *store,
                                 const ZylDeveloperCert *cert);

/* 개발자 인증서 폐기 */
bool zyl_appstore_revoke_cert(ZylAppStore *store,
                               const char *cert_fingerprint);

/* 개발자 모드 토글 (서명 검증 우회 - 개발 전용) */
void zyl_appstore_set_dev_mode(ZylAppStore *store, bool enabled);

/* 패키지 메타데이터 해제 */
void zyl_package_meta_free(ZylPackageMeta *meta);

#endif /* ZYL_APPSTORE_H */
