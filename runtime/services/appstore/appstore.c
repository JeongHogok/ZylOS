/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 앱스토어 패키지 서명 검증 및 설치 서비스
 * 수행범위: RSA-2048+SHA-256 서명 검증, .ospkg 패키지 설치, 카탈로그 관리
 * 의존방향: appstore.h
 * SOLID: SRP — 패키지 검증과 설치 로직만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE   /* nftw, strdup, mkdtemp */

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

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#ifdef HAVE_LIBZIP
#include <zip.h>
#endif

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

/* ─── 보안: ZIP 매직 바이트 검증 ─── */
static bool validate_zip_magic(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char magic[4];
    size_t rd = fread(magic, 1, 4, f);
    fclose(f);
    if (rd < 4) return false;
    /* PK\x03\x04 (local file header) or PK\x05\x06 (empty archive) */
    return magic[0] == 0x50 && magic[1] == 0x4B &&
           (magic[2] == 0x03 || magic[2] == 0x05);
}

/* ─── 보안: 경로 순회 공격 탐지 (재귀 디렉토리 스캔) ─── */
static bool scan_for_path_traversal(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return true; /* safe if can't open */
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.' && entry->d_name[1] == '\0') continue;
        if (entry->d_name[0] == '.' && entry->d_name[1] == '.' &&
            entry->d_name[2] == '\0') continue;

        /* Check for path traversal patterns */
        if (strstr(entry->d_name, "..") != NULL) {
            fprintf(stderr, "[SECURITY] Path traversal detected: %s\n",
                    entry->d_name);
            closedir(d);
            return false; /* UNSAFE */
        }

        /* Recurse into subdirectories */
        char subpath[512];
        snprintf(subpath, sizeof(subpath), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(subpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!scan_for_path_traversal(subpath)) {
                closedir(d);
                return false;
            }
        }
    }
    closedir(d);
    return true; /* SAFE */
}

/* ─── 보안: 패키지 파일 크기 제한 검사 ─── */
#define MAX_PACKAGE_SIZE (50 * 1024 * 1024) /* 50 MB */

static bool check_package_size(const char *path, size_t max_bytes) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return (size_t)st.st_size <= max_bytes;
}

/* ─── 보안: 쉘 인자에 대한 안전 경로 검증 ─── */
static bool is_safe_path(const char *path) {
    if (!path) return false;
    for (const char *p = path; *p; p++) {
        if (*p == '\'' || *p == '"' || *p == '`' || *p == '$' ||
            *p == '|' || *p == ';' || *p == '&' || *p == '\n') {
            fprintf(stderr, "[SECURITY] Unsafe character in path: 0x%02x\n",
                    (unsigned char)*p);
            return false;
        }
    }
    return true;
}

/* ─── 보안: 추출된 패키지 내용 검증 ─── */
static bool validate_package_contents(const char *extract_dir);

/* ─── 유틸리티: 파일 존재 확인 ─── */
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* ─── 유틸리티: 디렉토리 생성 (재귀) ─── */
static bool mkdir_p(const char *path) {
    if (!path || strlen(path) >= 512) return false;
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

/* ─── 유틸리티: SHA-256 해시 (OpenSSL EVP API) ─── */
static bool compute_sha256(const char *file_path, uint8_t out_hash[HASH_SIZE]) {
    FILE *f = fopen(file_path, "rb");
    if (!f) return false;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { fclose(f); return false; }

    bool ok = false;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) goto done;

    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (EVP_DigestUpdate(ctx, buf, n) != 1) goto done;
    }

    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, out_hash, &digest_len) != 1) goto done;
    ok = (digest_len == HASH_SIZE);

done:
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return ok;
}

/* ─── 유틸리티: Base64 디코딩 (OpenSSL EVP) ─── */
static uint8_t *base64_decode_alloc(const char *input, size_t *out_len) {
    if (!input || !out_len) return NULL;
    size_t in_len = strlen(input);
    if (in_len == 0) return NULL;

    /* Strip trailing whitespace/newlines */
    while (in_len > 0 && (input[in_len - 1] == '\n' ||
                           input[in_len - 1] == '\r' ||
                           input[in_len - 1] == ' ')) {
        in_len--;
    }
    if (in_len == 0) return NULL;

    size_t max_out = (in_len * 3) / 4 + 4;
    uint8_t *output = malloc(max_out);
    if (!output) return NULL;

    EVP_ENCODE_CTX *ctx = EVP_ENCODE_CTX_new();
    if (!ctx) { free(output); return NULL; }

    int decoded_len = 0;
    int final_len = 0;
    EVP_DecodeInit(ctx);
    if (EVP_DecodeUpdate(ctx, output, &decoded_len,
                         (const unsigned char *)input, (int)in_len) < 0) {
        EVP_ENCODE_CTX_free(ctx);
        free(output);
        return NULL;
    }
    if (EVP_DecodeFinal(ctx, output + decoded_len, &final_len) < 0) {
        EVP_ENCODE_CTX_free(ctx);
        free(output);
        return NULL;
    }
    EVP_ENCODE_CTX_free(ctx);

    *out_len = (size_t)(decoded_len + final_len);
    return output;
}

/* ─── 유틸리티: RSA-2048+SHA-256 서명 검증 (OpenSSL EVP API) ─── */
static bool verify_rsa_signature(const uint8_t *hash, size_t hash_len,
                                  const char *signature_b64,
                                  const char *public_key_pem) {
    if (!hash || hash_len == 0 || !signature_b64 || !public_key_pem)
        return false;

    bool verified = false;

    /* Decode base64 signature */
    size_t sig_len = 0;
    uint8_t *sig_bytes = base64_decode_alloc(signature_b64, &sig_len);
    if (!sig_bytes || sig_len == 0) {
        fprintf(stderr, "[APPSTORE] Failed to decode base64 signature\n");
        free(sig_bytes);
        return false;
    }

    /* Load public key from PEM string */
    BIO *bio = BIO_new_mem_buf(public_key_pem, -1);
    if (!bio) { free(sig_bytes); return false; }

    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) {
        fprintf(stderr, "[APPSTORE] Failed to parse public key PEM\n");
        free(sig_bytes);
        return false;
    }

    /* Verify signature using EVP_DigestVerify */
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(pkey); free(sig_bytes); return false; }

    if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) != 1)
        goto cleanup;

    if (EVP_DigestVerifyUpdate(ctx, hash, hash_len) != 1)
        goto cleanup;

    int rc = EVP_DigestVerifyFinal(ctx, sig_bytes, sig_len);
    if (rc == 1) {
        verified = true;
    } else if (rc == 0) {
        fprintf(stderr, "[APPSTORE] RSA signature verification FAILED — "
                "signature does not match\n");
    } else {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        fprintf(stderr, "[APPSTORE] RSA verify error: %s\n", err_buf);
    }

cleanup:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    free(sig_bytes);
    return verified;
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

/* ─── 유틸리티: JSON 문자열 값 추출 (json-glib) ─── */
#include <json-glib/json-glib.h>

static char *json_get_string(const char *json_str, const char *key) {
    if (!json_str || !key) return NULL;

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_str, -1, NULL)) {
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *obj = json_node_get_object(root);
    char *result = NULL;

    if (json_object_has_member(obj, key)) {
        const char *val = json_object_get_string_member(obj, key);
        if (val) result = strdup(val);
    }

    g_object_unref(parser);
    return result;
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

/* ─── 안전한 ZIP 추출 (libzip 또는 fork+execvp) ───
 * system() 사용 금지 — command injection 벡터.
 * libzip이 있으면 직접 API, 없으면 fork+execvp로 안전 호출.
 * ─── */

#ifdef HAVE_LIBZIP
/**
 * Extract ZIP archive to dest_dir using libzip.
 * Returns 0 on success, -1 on failure.
 * If specific_file is non-NULL, extract only that file.
 */
static int safe_extract_zip(const char *zip_path, const char *dest_dir,
                             const char *specific_file) {
    int errcode = 0;
    zip_t *za = zip_open(zip_path, ZIP_RDONLY, &errcode);
    if (!za) {
        fprintf(stderr, "[APPSTORE] zip_open failed: error %d\n", errcode);
        return -1;
    }

    zip_int64_t num_entries = zip_get_num_entries(za, 0);
    for (zip_int64_t i = 0; i < num_entries; i++) {
        const char *name = zip_get_name(za, (zip_uint64_t)i, 0);
        if (!name) continue;

        /* Security: reject path traversal */
        if (strstr(name, "..") != NULL) {
            fprintf(stderr, "[SECURITY] Path traversal in ZIP entry: %s\n",
                    name);
            zip_close(za);
            return -1;
        }

        /* If extracting specific file, skip others */
        if (specific_file && strcmp(name, specific_file) != 0) continue;

        /* Build destination path */
        char dest_path[512];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, name);

        /* If entry is a directory, create it */
        size_t name_len = strlen(name);
        if (name_len > 0 && name[name_len - 1] == '/') {
            mkdir_p(dest_path);
            continue;
        }

        /* Ensure parent directory exists */
        char *slash = strrchr(dest_path, '/');
        if (slash) {
            char parent[512];
            size_t plen = (size_t)(slash - dest_path);
            if (plen >= sizeof(parent)) plen = sizeof(parent) - 1;
            memcpy(parent, dest_path, plen);
            parent[plen] = '\0';
            mkdir_p(parent);
        }

        /* Extract file */
        zip_file_t *zf = zip_fopen_index(za, (zip_uint64_t)i, 0);
        if (!zf) continue;

        FILE *out = fopen(dest_path, "wb");
        if (!out) { zip_fclose(zf); continue; }

        char buf[8192];
        zip_int64_t rd;
        while ((rd = zip_fread(zf, buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, (size_t)rd, out);
        }
        fclose(out);
        zip_fclose(zf);
    }

    zip_close(za);
    return 0;
}

#else /* !HAVE_LIBZIP — fork+execvp fallback */

#include <sys/wait.h>
#include <spawn.h>

/**
 * Extract ZIP archive using fork+execvp (no system()).
 * Avoids shell interpretation of arguments — immune to injection.
 */
static int safe_extract_zip(const char *zip_path, const char *dest_dir,
                             const char *specific_file) {
    pid_t pid;
    int status;

    /* Build argument list — no shell involved */
    const char *argv[8];
    int argc = 0;
    argv[argc++] = "unzip";
    argv[argc++] = "-o";     /* overwrite */
    argv[argc++] = "-q";     /* quiet */
    argv[argc++] = zip_path;
    if (specific_file) {
        argv[argc++] = specific_file;
    }
    argv[argc++] = "-d";
    argv[argc++] = dest_dir;
    argv[argc] = NULL;

    extern char **environ;
    /* Use restricted environment */
    char *safe_env[] = {
        "PATH=/usr/bin:/bin",
        "HOME=/tmp",
        NULL
    };

    int rc = posix_spawn(&pid, "/usr/bin/unzip", NULL, NULL,
                         (char *const *)argv, safe_env);
    if (rc != 0) {
        fprintf(stderr, "[APPSTORE] posix_spawn(unzip) failed: %s\n",
                strerror(rc));
        return -1;
    }

    if (waitpid(pid, &status, 0) == -1) {
        fprintf(stderr, "[APPSTORE] waitpid failed: %s\n", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "[APPSTORE] unzip exited with status %d\n",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }

    return 0;
}
#endif /* HAVE_LIBZIP */

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

/* ─── 보안: 추출된 패키지 내용 검증 (구현) ─── */
static bool validate_package_contents(const char *extract_dir) {
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s/%s", extract_dir, MANIFEST_FILE);
    if (!file_exists(manifest_path)) {
        fprintf(stderr, "[SECURITY] Missing %s in extracted package\n",
                MANIFEST_FILE);
        return false;
    }

    /* Parse app.json and check required fields exist */
    ZylPackageMeta *meta = parse_app_json(manifest_path);
    if (!meta) {
        fprintf(stderr, "[SECURITY] Failed to parse %s in extracted package\n",
                MANIFEST_FILE);
        return false;
    }

    zyl_package_meta_free(meta);
    return true;
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
    if (!store->certs) {
        free(store->trust_store_path);
        free(store->app_install_dir);
        free(store);
        return NULL;
    }
    store->n_certs = 0;
    store->dev_mode = false;

    /* 디렉토리 생성 */
    mkdir_p(trust_store_path);
    mkdir_p(app_install_dir);

    /* 시스템 루트 인증서 로드 — scan trust_store for .pem files */
    {
        DIR *cert_dir = opendir(trust_store_path);
        if (cert_dir) {
            struct dirent *cert_entry;
            while ((cert_entry = readdir(cert_dir)) != NULL) {
                size_t nlen = strlen(cert_entry->d_name);
                if (nlen < 5 || strcmp(cert_entry->d_name + nlen - 4, ".pem") != 0)
                    continue;

                char cert_path[512];
                snprintf(cert_path, sizeof(cert_path), "%s/%s",
                         trust_store_path, cert_entry->d_name);

                char *pem_data = read_file_contents(cert_path, NULL);
                if (!pem_data) continue;

                /* Derive developer_id from filename (minus .pem) */
                char *dev_id = strdup(cert_entry->d_name);
                if (dev_id) {
                    dev_id[nlen - 4] = '\0';
                    ZylDeveloperCert cert = {0};
                    cert.developer_id = dev_id;
                    cert.developer_name = strdup(dev_id);
                    cert.public_key_pem = pem_data;
                    cert.issued_at = 0;
                    cert.expires_at = 0;
                    cert.is_revoked = false;
                    zyl_appstore_register_cert(store, &cert);
                    /* register_cert duplicates strings, so free originals */
                    free(dev_id);
                }
                free(pem_data);
            }
            closedir(cert_dir);
            fprintf(stderr, "[APPSTORE] Loaded %d certificates from %s\n",
                    store->n_certs, trust_store_path);
        }
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

    /* 보안: 경로에 쉘 인젝션 문자가 없는지 검증 */
    if (!is_safe_path(package_path)) {
        fprintf(stderr, "[SECURITY] Unsafe characters in package path\n");
        return ZYL_PKG_UNSIGNED;
    }

    /* 보안: ZIP 매직 바이트 검증 */
    if (!validate_zip_magic(package_path)) {
        fprintf(stderr, "[SECURITY] Invalid ZIP magic bytes: %s\n",
                package_path);
        return ZYL_PKG_UNSIGNED;
    }

    /* 보안: 패키지 크기 제한 (50MB) */
    if (!check_package_size(package_path, MAX_PACKAGE_SIZE)) {
        fprintf(stderr, "[SECURITY] Package exceeds size limit: %s\n",
                package_path);
        return ZYL_PKG_UNSIGNED;
    }

    /* 개발자 모드: 서명 검증 우회, 메타데이터만 추출 */
    if (store->dev_mode) {
        /* 개발자 모드에서도 메타데이터 추출 시도 */
        char tmp_dir[] = "/tmp/zyl-pkg-XXXXXX";
        if (mkdtemp(tmp_dir)) {
            safe_extract_zip(package_path, tmp_dir, MANIFEST_FILE);

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
    char tmp_dir[] = "/tmp/zyl-pkg-XXXXXX";
    if (!mkdtemp(tmp_dir)) {
        fprintf(stderr, "[APPSTORE] Failed to create temp dir\n");
        return ZYL_PKG_UNSIGNED;
    }

    if (safe_extract_zip(package_path, tmp_dir, NULL) != 0) {
        fprintf(stderr, "[APPSTORE] Failed to extract package: %s\n",
                package_path);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* 보안: 경로 순회 공격 탐지 */
    if (!scan_for_path_traversal(tmp_dir)) {
        fprintf(stderr, "[SECURITY] Path traversal detected in package: %s\n",
                package_path);
        rm_rf(tmp_dir);
        return ZYL_PKG_UNSIGNED;
    }

    /* 보안: 필수 패키지 파일 존재 확인 */
    if (!validate_package_contents(tmp_dir)) {
        fprintf(stderr, "[SECURITY] Package content validation failed: %s\n",
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

    /* RSA-2048+SHA-256 signature verification (OpenSSL EVP API) */
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
    if (safe_extract_zip(package_path, install_dir, NULL) != 0) {
        fprintf(stderr, "[APPSTORE] Failed to extract to: %s\n", install_dir);
        rm_rf(install_dir);
        zyl_package_meta_free(meta);
        return ZYL_INSTALL_ERR_IO;
    }

    /* 보안: 추출 후 경로 순회 공격 탐지 */
    if (!scan_for_path_traversal(install_dir)) {
        fprintf(stderr, "[SECURITY] Path traversal detected in installed package\n");
        rm_rf(install_dir);
        zyl_package_meta_free(meta);
        return ZYL_INSTALL_ERR_CORRUPT_PACKAGE;
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

    /* Notify WAM about newly installed app via D-Bus */
    {
        GError *err = NULL;
        GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
        if (conn) {
            g_dbus_connection_call(conn, "org.zylos.WebAppManager",
                "/org/zylos/WebAppManager", "org.zylos.WebAppManager",
                "Launch", g_variant_new("(s)", meta->app_id),
                NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL, NULL);
            g_object_unref(conn);
        }
        if (err) g_error_free(err);
    }

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

    /* 재귀적 설치 디렉토리 삭제 */
    if (rm_rf(app_dir) != 0) {
        fprintf(stderr, "[APPSTORE] Failed to remove: %s\n", app_dir);
        return ZYL_INSTALL_ERR_IO;
    }

    fprintf(stderr, "[APPSTORE] Uninstalled: %s\n", app_id);

    /* #9: 앱 데이터 디렉토리 삭제 — /data/apps/{app_id}/ */
    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "/data/apps/%s", app_id);
    if (file_exists(data_dir)) {
        if (rm_rf(data_dir) == 0) {
            fprintf(stderr, "[APPSTORE] Cleaned app data: %s\n", data_dir);
        } else {
            /* 데이터 삭제 실패는 경고만 — 앱 제거 자체는 성공으로 처리 */
            fprintf(stderr, "[APPSTORE] Warning: failed to remove app data: %s\n",
                    data_dir);
        }
    }

    /* #9: Documents/Contacts/{app_id}* 정리 (선택적) */
    {
        char contacts_dir[512];
        snprintf(contacts_dir, sizeof(contacts_dir),
                 "/data/user/Documents/Contacts");
        DIR *d = opendir(contacts_dir);
        if (d) {
            struct dirent *ent;
            size_t id_len = strlen(app_id);
            while ((ent = readdir(d)) != NULL) {
                if (strncmp(ent->d_name, app_id, id_len) == 0) {
                    char entry_path[512];
                    snprintf(entry_path, sizeof(entry_path),
                             "%s/%s", contacts_dir, ent->d_name);
                    remove(entry_path);
                    fprintf(stderr, "[APPSTORE] Cleaned app data: %s\n", entry_path);
                }
            }
            closedir(d);
        }
    }

    /* #9: Documents/Messages/{app_id}* 정리 (선택적) */
    {
        char messages_dir[512];
        snprintf(messages_dir, sizeof(messages_dir),
                 "/data/user/Documents/Messages");
        DIR *d = opendir(messages_dir);
        if (d) {
            struct dirent *ent;
            size_t id_len = strlen(app_id);
            while ((ent = readdir(d)) != NULL) {
                if (strncmp(ent->d_name, app_id, id_len) == 0) {
                    char entry_path[512];
                    snprintf(entry_path, sizeof(entry_path),
                             "%s/%s", messages_dir, ent->d_name);
                    remove(entry_path);
                    fprintf(stderr, "[APPSTORE] Cleaned app data: %s\n", entry_path);
                }
            }
            closedir(d);
        }
    }

    /* Notify WAM about app removal via D-Bus */
    {
        GError *err = NULL;
        GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
        if (conn) {
            g_dbus_connection_call(conn, "org.zylos.WebAppManager",
                "/org/zylos/WebAppManager", "org.zylos.WebAppManager",
                "Close", g_variant_new("(s)", app_id),
                NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, NULL, NULL);
            g_object_unref(conn);
        }
        if (err) g_error_free(err);
    }

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
        int new_capacity = store->certs_capacity * 2;
        ZylDeveloperCert *tmp = realloc(store->certs,
            (size_t)new_capacity * sizeof(ZylDeveloperCert));
        if (!tmp) {
            fprintf(stderr, "[APPSTORE] realloc failed for certs\n");
            return false;
        }
        store->certs = tmp;
        store->certs_capacity = new_capacity;
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

    /* Scan installed apps signed with this certificate and uninstall them */
    {
        ZylPackageMeta **apps = NULL;
        int app_count = 0;
        if (zyl_appstore_list_installed(store, &apps, &app_count) == 0) {
            for (int i = 0; i < app_count; i++) {
                if (apps[i] && apps[i]->cert_fingerprint &&
                    strcmp(apps[i]->cert_fingerprint, cert_fingerprint) == 0) {
                    fprintf(stderr, "[SECURITY] Disabling app signed with "
                            "revoked cert: %s\n", apps[i]->app_id);
                    zyl_appstore_uninstall(store, apps[i]->app_id);
                }
                zyl_package_meta_free(apps[i]);
            }
            free(apps);
        }
    }
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
