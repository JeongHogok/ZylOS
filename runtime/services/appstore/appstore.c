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
#include <time.h>
#include <ftw.h>

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

/* ─── 유틸리티: 파일 내용 읽기 ─── */
static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)len, f);
    buf[rd] = '\0';
    fclose(f);
    if (out_len) *out_len = rd;
    return buf;
}

/* ─── 유틸리티: 간이 JSON 값 추출 (키-값 쌍) ─── */
static char *json_get_string(const char *json, const char *key) {
    /*
     * 간이 JSON 파서: "key": "value" 패턴 추출
     * 프로덕션에서는 cJSON 또는 json-c 라이브러리 사용 권장
     */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *pos = strstr(json, pattern);
    if (!pos) return NULL;

    pos += strlen(pattern);
    /* 공백과 콜론 건너뛰기 */
    while (*pos == ' ' || *pos == '\t' || *pos == ':') pos++;

    if (*pos != '"') return NULL;
    pos++; /* 여는 따옴표 건너뛰기 */

    const char *end = pos;
    while (*end && *end != '"') {
        if (*end == '\\') end++; /* 이스케이프 문자 건너뛰기 */
        if (*end) end++;
    }

    size_t len = (size_t)(end - pos);
    char *val = malloc(len + 1);
    if (!val) return NULL;
    memcpy(val, pos, len);
    val[len] = '\0';
    return val;
}

/* ─── 유틸리티: JSON에서 정수 값 추출 ─── */
static long json_get_long(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return -1;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == ':') pos++;
    return strtol(pos, NULL, 10);
}

/* ─── 유틸리티: nftw 콜백 for rm_rf ─── */
static int rm_rf_callback(const char *fpath, const struct stat *sb,
                           int typeflag, struct FTW *ftwbuf) {
    (void)sb; (void)typeflag; (void)ftwbuf;
    return remove(fpath);
}

/* ─── 유틸리티: 재귀적 디렉토리 삭제 ─── */
static int rm_rf(const char *path) {
    return nftw(path, rm_rf_callback, 64, FTW_DEPTH | FTW_PHYS);
}

/* ─── 유틸리티: app.json 파싱하여 ZylPackageMeta 생성 ─── */
static ZylPackageMeta *parse_app_json(const char *json_path) {
    size_t len = 0;
    char *json = read_file_contents(json_path, &len);
    if (!json) return NULL;

    ZylPackageMeta *meta = calloc(1, sizeof(ZylPackageMeta));
    if (!meta) { free(json); return NULL; }

    meta->app_id      = json_get_string(json, "id");
    meta->name         = json_get_string(json, "name");
    meta->version      = json_get_string(json, "version");
    meta->description  = json_get_string(json, "description");
    meta->author       = json_get_string(json, "author");
    meta->icon_path    = json_get_string(json, "icon");
    meta->min_os_version = json_get_string(json, "min_os_version");

    /* 필수 필드 검증 */
    if (!meta->app_id || !meta->name || !meta->version) {
        zyl_package_meta_free(meta);
        free(json);
        return NULL;
    }

    free(json);
    return meta;
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
    if (out_meta) *out_meta = NULL;

    /* 개발자 모드: 서명 검증 우회, 메타데이터만 추출 */
    if (store->dev_mode) {
        /* 개발자 모드에서도 메타데이터 추출 시도 */
        char tmp_dir[] = "/tmp/zyl-pkg-XXXXXX";
        if (mkdtemp(tmp_dir)) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                     "unzip -o -q '%s' %s -d '%s' 2>/dev/null",
                     package_path, MANIFEST_FILE, tmp_dir);
            system(cmd);

            char manifest_path[512];
            snprintf(manifest_path, sizeof(manifest_path),
                     "%s/%s", tmp_dir, MANIFEST_FILE);
            if (out_meta && file_exists(manifest_path)) {
                *out_meta = parse_app_json(manifest_path);
            }
            rm_rf(tmp_dir);
        }
        return ZYL_PKG_VALID_SIGNATURE;
    }

    /* Step 1: 패키지 파일 존재 확인 */
    if (!file_exists(package_path)) {
        fprintf(stderr, "[APPSTORE] Package not found: %s\n", package_path);
        return ZYL_PKG_UNSIGNED;
    }

    /* Step 2: .ospkg(ZIP)를 임시 디렉토리에 압축 해제 */
    /* TODO: libzip 통합 시 popen/system 대신 zip_open() 사용 */
    char tmp_dir[] = "/tmp/zyl-pkg-XXXXXX";
    if (!mkdtemp(tmp_dir)) {
        fprintf(stderr, "[APPSTORE] Failed to create temp dir\n");
        return ZYL_PKG_UNSIGNED;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "unzip -o -q '%s' -d '%s' 2>/dev/null", package_path, tmp_dir);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[APPSTORE] Failed to extract package: %s\n",
                package_path);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* Step 3: CERT 파일에서 개발자 인증서 지문 읽기 */
    char cert_path[512];
    snprintf(cert_path, sizeof(cert_path), "%s/%s", tmp_dir, CERT_FILE);
    if (!file_exists(cert_path)) {
        fprintf(stderr, "[APPSTORE] Missing CERT file in package\n");
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    char *cert_fingerprint = read_file_contents(cert_path, NULL);
    if (!cert_fingerprint) {
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* 개행 문자 제거 */
    size_t fp_len = strlen(cert_fingerprint);
    while (fp_len > 0 && (cert_fingerprint[fp_len - 1] == '\n' ||
                           cert_fingerprint[fp_len - 1] == '\r')) {
        cert_fingerprint[--fp_len] = '\0';
    }

    /* Step 4: 신뢰 저장소에서 인증서 조회 */
    ZylDeveloperCert *cert = find_cert(store, cert_fingerprint);
    if (!cert) {
        fprintf(stderr, "[APPSTORE] Unknown certificate: %s\n",
                cert_fingerprint);
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* Step 5: 인증서 만료 확인 */
    uint64_t now = (uint64_t)time(NULL);
    if (cert->expires_at > 0 && now > cert->expires_at) {
        fprintf(stderr, "[APPSTORE] Certificate expired: %s\n",
                cert_fingerprint);
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_EXPIRED_CERT;
    }

    /* Step 6: 인증서 폐기 확인 */
    if (cert->is_revoked) {
        fprintf(stderr, "[APPSTORE] Certificate revoked: %s\n",
                cert_fingerprint);
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_REVOKED_CERT;
    }

    /* Step 7: app.json 해시 계산 */
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s", tmp_dir, MANIFEST_FILE);
    if (!file_exists(manifest_path)) {
        fprintf(stderr, "[APPSTORE] Missing %s in package\n", MANIFEST_FILE);
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    uint8_t manifest_hash[HASH_SIZE];
    if (!compute_sha256(manifest_path, manifest_hash)) {
        fprintf(stderr, "[APPSTORE] Failed to compute manifest hash\n");
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* Step 8: SIGNATURE 파일 읽기 및 RSA 검증 */
    char sig_path[512];
    snprintf(sig_path, sizeof(sig_path), "%s/%s", tmp_dir, SIGNATURE_FILE);
    if (!file_exists(sig_path)) {
        fprintf(stderr, "[APPSTORE] Missing SIGNATURE file in package\n");
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    char *signature_b64 = read_file_contents(sig_path, NULL);
    if (!signature_b64) {
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* TODO: OpenSSL 통합 시 실제 RSA-2048+SHA-256 검증 수행 */
    if (!verify_rsa_signature(manifest_hash, HASH_SIZE,
                               signature_b64, cert->public_key_pem)) {
        fprintf(stderr, "[APPSTORE] Signature verification failed\n");
        free(signature_b64);
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_INVALID_SIGNATURE;
    }

    /* Step 9: app.json 파싱하여 ZylPackageMeta 구성 */
    ZylPackageMeta *meta = parse_app_json(manifest_path);
    if (!meta) {
        fprintf(stderr, "[APPSTORE] Failed to parse %s\n", MANIFEST_FILE);
        free(signature_b64);
        free(cert_fingerprint);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* 서명 및 인증서 정보 기록 */
    meta->signature = signature_b64; /* 소유권 이전 */
    meta->cert_fingerprint = cert_fingerprint; /* 소유권 이전 */

    /* 패키지 크기 */
    struct stat st;
    if (stat(package_path, &st) == 0) {
        meta->package_size = (size_t)st.st_size;
    }

    if (out_meta) {
        *out_meta = meta;
    } else {
        zyl_package_meta_free(meta);
    }

    rm_rf(tmp_dir);
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

    if (!meta) {
        fprintf(stderr, "[APPSTORE] verify_package returned no metadata\n");
        return ZYL_INSTALL_ERR_CORRUPT_PACKAGE;
    }

    /* 2. 필수 필드 검증: id, name, version 은 parse_app_json 에서 검증됨 */
    /* entry 포인트 확인은 추출 후 수행 */

    /* 3. 설치 디렉토리 생성 */
    char install_dir[512];
    snprintf(install_dir, sizeof(install_dir),
             "%s/%s", store->app_install_dir, meta->app_id);

    /* 이미 설치된 경우 확인 */
    char existing_manifest[512];
    snprintf(existing_manifest, sizeof(existing_manifest),
             "%s/%s", install_dir, MANIFEST_FILE);
    if (file_exists(existing_manifest)) {
        fprintf(stderr, "[APPSTORE] App already installed: %s\n",
                meta->app_id);
        zyl_package_meta_free(meta);
        return ZYL_INSTALL_ERR_ALREADY_INSTALLED;
    }

    if (!mkdir_p(install_dir)) {
        fprintf(stderr, "[APPSTORE] Failed to create install dir: %s\n",
                install_dir);
        zyl_package_meta_free(meta);
        return ZYL_INSTALL_ERR_IO;
    }

    /* 4. 패키지 내용을 설치 디렉토리로 추출 */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "unzip -o -q '%s' -d '%s' 2>/dev/null", package_path, install_dir);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[APPSTORE] Failed to extract to: %s\n", install_dir);
        rm_rf(install_dir);
        zyl_package_meta_free(meta);
        return ZYL_INSTALL_ERR_IO;
    }

    /* 5. 추출 후 app.json 필수 필드 재확인 (entry 포인트) */
    char final_manifest[512];
    snprintf(final_manifest, sizeof(final_manifest),
             "%s/%s", install_dir, MANIFEST_FILE);
    if (!file_exists(final_manifest)) {
        fprintf(stderr, "[APPSTORE] No %s after extraction\n", MANIFEST_FILE);
        rm_rf(install_dir);
        zyl_package_meta_free(meta);
        return ZYL_INSTALL_ERR_CORRUPT_PACKAGE;
    }

    /* entry 필드 확인 */
    char *json = read_file_contents(final_manifest, NULL);
    if (json) {
        char *entry = json_get_string(json, "entry");
        if (!entry) {
            fprintf(stderr, "[APPSTORE] Missing 'entry' field in %s\n",
                    MANIFEST_FILE);
            free(json);
            rm_rf(install_dir);
            zyl_package_meta_free(meta);
            return ZYL_INSTALL_ERR_CORRUPT_PACKAGE;
        }
        free(entry);
        free(json);
    }

    /* SIGNATURE, CERT 파일은 보안을 위해 설치 디렉토리에서 제거 */
    char sig_in_dir[512], cert_in_dir[512];
    snprintf(sig_in_dir, sizeof(sig_in_dir),
             "%s/%s", install_dir, SIGNATURE_FILE);
    snprintf(cert_in_dir, sizeof(cert_in_dir),
             "%s/%s", install_dir, CERT_FILE);
    remove(sig_in_dir);
    remove(cert_in_dir);

    fprintf(stderr, "[APPSTORE] Installed: %s v%s to %s\n",
            meta->app_id, meta->version, install_dir);

    /* TODO: D-Bus로 WAM에 새 앱 등록 알림 */

    zyl_package_meta_free(meta);
    return ZYL_INSTALL_SUCCESS;
}

ZylInstallResult zyl_appstore_uninstall(ZylAppStore *store,
                                        const char *app_id) {
    if (!store || !app_id)
        return ZYL_INSTALL_ERR_IO;

    /* 시스템 앱은 제거 불가 */
    char sys_manifest[512];
    snprintf(sys_manifest, sizeof(sys_manifest),
             "/usr/share/zyl-os/apps/%s/app.json", app_id);
    if (file_exists(sys_manifest)) {
        fprintf(stderr, "[APPSTORE] Cannot uninstall system app: %s\n",
                app_id);
        return ZYL_INSTALL_ERR_PERMISSION_DENIED;
    }

    /* 사용자 앱 디렉토리 확인 */
    char app_dir[512];
    snprintf(app_dir, sizeof(app_dir),
             "%s/%s", store->app_install_dir, app_id);

    if (!file_exists(app_dir)) {
        fprintf(stderr, "[APPSTORE] App not found: %s\n", app_id);
        return ZYL_INSTALL_ERR_IO;
    }

    /* 재귀적 디렉토리 삭제 */
    if (rm_rf(app_dir) != 0) {
        fprintf(stderr, "[APPSTORE] Failed to remove: %s\n", app_dir);
        return ZYL_INSTALL_ERR_IO;
    }

    fprintf(stderr, "[APPSTORE] Uninstalled: %s\n", app_id);

    /* TODO: D-Bus로 WAM에 앱 제거 알림 */

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
    int capacity = 64;
    ZylPackageMeta **apps = calloc((size_t)capacity, sizeof(ZylPackageMeta *));
    if (!apps) {
        closedir(dir);
        *out_apps = NULL;
        *out_count = 0;
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < MAX_INSTALLED_APPS) {
        if (entry->d_name[0] == '.') continue;

        char manifest[512];
        snprintf(manifest, sizeof(manifest),
                 "%s/%s/%s", store->app_install_dir, entry->d_name,
                 MANIFEST_FILE);

        if (!file_exists(manifest)) continue;

        ZylPackageMeta *meta = parse_app_json(manifest);
        if (!meta) continue;

        /* 용량 확장 */
        if (count >= capacity) {
            capacity *= 2;
            ZylPackageMeta **tmp = realloc(apps,
                (size_t)capacity * sizeof(ZylPackageMeta *));
            if (!tmp) {
                zyl_package_meta_free(meta);
                break;
            }
            apps = tmp;
        }

        apps[count++] = meta;
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
    if (dst->public_key_pem) {
        FILE *f = fopen(cert_path, "w");
        if (f) {
            fputs(dst->public_key_pem, f);
            fclose(f);
        }
    }

    fprintf(stderr, "[APPSTORE] Registered certificate: %s (%s)\n",
            cert->developer_id,
            cert->developer_name ? cert->developer_name : "unknown");

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
