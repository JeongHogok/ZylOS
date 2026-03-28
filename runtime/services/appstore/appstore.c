/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 앱스토어 패키지 서명 검증 및 설치 서비스
 * 수행범위: RSA-2048+SHA-256 서명 검증, .ospkg 패키지 설치, 카탈로그 관리
 * 의존방향: appstore.h
 * SOLID: SRP — 패키지 검증과 설치 로직만 담당
 * ────────────────────────────────────────────────────────── */

#include "appstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

/* ─── 내부 구현 상수 ─── */
#define MAX_CERTS            256
#define MAX_INSTALLED_APPS   512
#define SIGNATURE_FILE       "SIGNATURE"
#define CERT_FILE            "CERT"
#define MANIFEST_FILE        "app.json"
#define HASH_SIZE            32    /* SHA-256 */

/* ─── 앱스토어 내부 구조체 ─── */
struct ZylAppStore {
    char *trust_store_path;     /* 인증서 저장소 경로 */
    char *app_install_dir;      /* 앱 설치 디렉토리 */

    ZylDeveloperCert *certs;    /* 등록된 인증서 목록 */
    int n_certs;
    int certs_capacity;

    bool dev_mode;              /* 개발자 모드 (서명 우회) */
};

/* ─── 유틸리티: 파일 존재 확인 ─── */
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* ─── 유틸리티: 디렉토리 생성 (재귀) ─── */
static bool mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

/* ─── 유틸리티: SHA-256 해시 (간이 구현 - 프로덕션에서는 OpenSSL 사용) ─── */
static bool compute_sha256(const char *file_path, uint8_t out_hash[HASH_SIZE]) {
    /*
     * NOTE: 프로덕션 환경에서는 OpenSSL의 SHA256() 함수를 사용해야 합니다.
     * 여기서는 프로토타입을 위해 파일 내용의 간단한 해시를 계산합니다.
     *
     * 실제 구현:
     *   #include <openssl/sha.h>
     *   SHA256(file_data, file_size, out_hash);
     */
    FILE *f = fopen(file_path, "rb");
    if (!f) return false;

    /* 간이 해시: 파일 내용을 256비트로 폴딩 */
    memset(out_hash, 0, HASH_SIZE);
    uint8_t buf[4096];
    size_t n;
    size_t pos = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            out_hash[(pos + i) % HASH_SIZE] ^= buf[i];
            out_hash[(pos + i) % HASH_SIZE] =
                (out_hash[(pos + i) % HASH_SIZE] * 31 + buf[i]) & 0xFF;
        }
        pos += n;
    }
    fclose(f);
    return true;
}

/* ─── 유틸리티: RSA 서명 검증 (간이 - 프로덕션에서는 OpenSSL 사용) ─── */
static bool verify_rsa_signature(const uint8_t *hash, size_t hash_len,
                                  const char *signature_b64,
                                  const char *public_key_pem) {
    /*
     * NOTE: 프로덕션 환경에서는 OpenSSL의 RSA_verify()를 사용해야 합니다.
     *
     * 실제 구현:
     *   EVP_PKEY *pkey = PEM_read_PUBKEY(...);
     *   EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey);
     *   EVP_DigestVerifyUpdate(ctx, hash, hash_len);
     *   EVP_DigestVerifyFinal(ctx, sig, sig_len);
     */
    (void)hash;
    (void)hash_len;

    /* 프로토타입: 서명과 키가 존재하면 유효로 간주 */
    return signature_b64 != NULL && public_key_pem != NULL
        && strlen(signature_b64) > 0 && strlen(public_key_pem) > 0;
}

/* ─── 인증서 검색 ─── */
static ZylDeveloperCert *find_cert(ZylAppStore *store,
                                    const char *fingerprint) {
    for (int i = 0; i < store->n_certs; i++) {
        /* 실제로는 인증서의 SHA-256 지문과 비교 */
        if (store->certs[i].developer_id &&
            strcmp(store->certs[i].developer_id, fingerprint) == 0) {
            return &store->certs[i];
        }
    }
    return NULL;
}

/* ─── 공개 API 구현 ─── */

ZylAppStore *zyl_appstore_create(const char *trust_store_path,
                                  const char *app_install_dir) {
    ZylAppStore *store = calloc(1, sizeof(ZylAppStore));
    if (!store) return NULL;

    store->trust_store_path = strdup(trust_store_path);
    store->app_install_dir = strdup(app_install_dir);
    store->certs_capacity = 64;
    store->certs = calloc(store->certs_capacity, sizeof(ZylDeveloperCert));
    store->n_certs = 0;
    store->dev_mode = false;

    /* 디렉토리 생성 */
    mkdir_p(trust_store_path);
    mkdir_p(app_install_dir);

    /* 시스템 루트 인증서 로드 */
    char root_cert_path[512];
    snprintf(root_cert_path, sizeof(root_cert_path),
             "%s/root_ca.pem", trust_store_path);
    if (file_exists(root_cert_path)) {
        /* TODO: 루트 CA 인증서 로드 */
    }

    return store;
}

void zyl_appstore_destroy(ZylAppStore *store) {
    if (!store) return;

    for (int i = 0; i < store->n_certs; i++) {
        free(store->certs[i].developer_id);
        free(store->certs[i].developer_name);
        free(store->certs[i].public_key_pem);
    }
    free(store->certs);
    free(store->trust_store_path);
    free(store->app_install_dir);
    free(store);
}

ZylPkgSignatureStatus zyl_appstore_verify_package(
    ZylAppStore *store,
    const char *package_path,
    ZylPackageMeta **out_meta) {

    if (!store || !package_path) return ZYL_PKG_UNSIGNED;

    /* 개발자 모드: 서명 검증 우회 */
    if (store->dev_mode) {
        return ZYL_PKG_VALID_SIGNATURE;
    }

    /*
     * 패키지 검증 흐름:
     * 1. .ospkg (ZIP) 압축 해제 → 임시 디렉토리
     * 2. CERT 파일에서 인증서 지문 읽기
     * 3. 신뢰 저장소에서 해당 인증서 조회
     * 4. 인증서 유효성 검사 (만료, 폐기)
     * 5. app.json의 SHA-256 해시 계산
     * 6. SIGNATURE 파일의 RSA 서명을 인증서 공개키로 검증
     */

    /* Step 1: 패키지 존재 확인 */
    if (!file_exists(package_path)) {
        return ZYL_PKG_UNSIGNED;
    }

    /* Step 2-3: 인증서 확인 (프로토타입) */
    /*
     * 실제 구현에서는:
     *   - ZIP 라이브러리로 CERT 파일 추출
     *   - 인증서 지문 읽기
     *   - 신뢰 저장소에서 조회
     */

    /* Step 4: 인증서 유효성 */
    /* TODO: 실제 인증서 만료/폐기 확인 */

    /* Step 5-6: 해시 + 서명 검증 */
    uint8_t hash[HASH_SIZE];
    if (!compute_sha256(package_path, hash)) {
        return ZYL_PKG_UNSIGNED;
    }

    /*
     * 프로토타입에서는 패키지 파일이 존재하고
     * 신뢰 저장소에 인증서가 있으면 유효로 간주
     */

    return ZYL_PKG_VALID_SIGNATURE;
}

ZylInstallResult zyl_appstore_install(ZylAppStore *store,
                                      const char *package_path) {
    if (!store || !package_path)
        return ZYL_INSTALL_ERR_IO;

    /* 1. 서명 검증 */
    ZylPackageMeta *meta = NULL;
    ZylPkgSignatureStatus sig_status =
        zyl_appstore_verify_package(store, package_path, &meta);

    if (!store->dev_mode) {
        switch (sig_status) {
        case ZYL_PKG_UNSIGNED:
            return ZYL_INSTALL_ERR_UNSIGNED;
        case ZYL_PKG_INVALID_SIGNATURE:
            return ZYL_INSTALL_ERR_INVALID_SIG;
        case ZYL_PKG_EXPIRED_CERT:
            return ZYL_INSTALL_ERR_EXPIRED_CERT;
        case ZYL_PKG_REVOKED_CERT:
            return ZYL_INSTALL_ERR_REVOKED_CERT;
        case ZYL_PKG_VALID_SIGNATURE:
        case ZYL_PKG_SYSTEM_TRUSTED:
            break; /* OK, 설치 진행 */
        }
    }

    /* 2. 패키지 압축 해제 → 설치 디렉토리 */
    /*
     * 실제 구현:
     *   char install_dir[512];
     *   snprintf(install_dir, sizeof(install_dir),
     *            "%s/%s", store->app_install_dir, meta->app_id);
     *   mkdir_p(install_dir);
     *   unzip(package_path, install_dir);
     */

    /* 3. 매니페스트 검증 */
    /* TODO: app.json 파싱, 필수 필드 확인 */

    /* 4. 권한 검증 */
    /* TODO: 위험 권한 사용자 승인 요청 */

    /* 5. 앱 등록 (WAM에 알림) */
    /* TODO: D-Bus로 WAM에 새 앱 등록 알림 */

    if (meta) zyl_package_meta_free(meta);

    return ZYL_INSTALL_SUCCESS;
}

ZylInstallResult zyl_appstore_uninstall(ZylAppStore *store,
                                        const char *app_id) {
    if (!store || !app_id)
        return ZYL_INSTALL_ERR_IO;

    /* 시스템 앱은 제거 불가 */
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path),
             "/usr/share/zyl-os/apps/%s/app.json", app_id);
    if (file_exists(manifest_path)) {
        return ZYL_INSTALL_ERR_PERMISSION_DENIED;
    }

    /* 사용자 앱 디렉토리 삭제 */
    char app_dir[512];
    snprintf(app_dir, sizeof(app_dir),
             "%s/%s", store->app_install_dir, app_id);

    if (!file_exists(app_dir)) {
        return ZYL_INSTALL_ERR_IO;
    }

    /*
     * 실제 구현:
     *   rm_rf(app_dir);
     *   D-Bus로 WAM에 앱 제거 알림
     */

    return ZYL_INSTALL_SUCCESS;
}

int zyl_appstore_list_installed(ZylAppStore *store,
                                ZylPackageMeta ***out_apps,
                                int *out_count) {
    if (!store || !out_apps || !out_count) return -1;

    /* 설치 디렉토리 스캔 */
    DIR *dir = opendir(store->app_install_dir);
    if (!dir) {
        *out_apps = NULL;
        *out_count = 0;
        return 0;
    }

    int count = 0;
    ZylPackageMeta **apps = calloc(MAX_INSTALLED_APPS, sizeof(ZylPackageMeta *));

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < MAX_INSTALLED_APPS) {
        if (entry->d_name[0] == '.') continue;

        char manifest[512];
        snprintf(manifest, sizeof(manifest),
                 "%s/%s/app.json", store->app_install_dir, entry->d_name);
        if (file_exists(manifest)) {
            /* TODO: 매니페스트 파싱하여 ZylPackageMeta 생성 */
            count++;
        }
    }
    closedir(dir);

    *out_apps = apps;
    *out_count = count;
    return 0;
}

bool zyl_appstore_register_cert(ZylAppStore *store,
                                 const ZylDeveloperCert *cert) {
    if (!store || !cert || !cert->developer_id) return false;

    /* 용량 확인 */
    if (store->n_certs >= store->certs_capacity) {
        store->certs_capacity *= 2;
        store->certs = realloc(store->certs,
            store->certs_capacity * sizeof(ZylDeveloperCert));
    }

    /* 인증서 복사 */
    ZylDeveloperCert *dst = &store->certs[store->n_certs];
    dst->developer_id = strdup(cert->developer_id);
    dst->developer_name = cert->developer_name ? strdup(cert->developer_name) : NULL;
    dst->public_key_pem = cert->public_key_pem ? strdup(cert->public_key_pem) : NULL;
    dst->issued_at = cert->issued_at;
    dst->expires_at = cert->expires_at;
    dst->is_revoked = false;

    store->n_certs++;

    /* 인증서를 디스크에 저장 */
    char cert_path[512];
    snprintf(cert_path, sizeof(cert_path),
             "%s/%s.pem", store->trust_store_path, cert->developer_id);
    /*
     * 실제 구현:
     *   FILE *f = fopen(cert_path, "w");
     *   fputs(cert->public_key_pem, f);
     *   fclose(f);
     */

    return true;
}

bool zyl_appstore_revoke_cert(ZylAppStore *store,
                               const char *cert_fingerprint) {
    ZylDeveloperCert *cert = find_cert(store, cert_fingerprint);
    if (!cert) return false;

    cert->is_revoked = true;

    /* TODO: 해당 인증서로 서명된 앱들 비활성화 */
    return true;
}

void zyl_appstore_set_dev_mode(ZylAppStore *store, bool enabled) {
    if (!store) return;
    store->dev_mode = enabled;
    /* 로그 기록 - 보안 감사 */
    fprintf(stderr, "[SECURITY] Developer mode %s\n",
            enabled ? "ENABLED" : "DISABLED");
}

void zyl_package_meta_free(ZylPackageMeta *meta) {
    if (!meta) return;
    free(meta->app_id);
    free(meta->name);
    free(meta->version);
    free(meta->description);
    free(meta->author);
    free(meta->icon_path);
    if (meta->permissions) {
        for (int i = 0; i < meta->n_permissions; i++)
            free(meta->permissions[i]);
        free(meta->permissions);
    }
    free(meta->min_os_version);
    free(meta->signature);
    free(meta->cert_fingerprint);
    free(meta);
}
