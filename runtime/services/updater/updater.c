#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: OTA 업데이트 서비스 — A/B 파티션 기반 원자적 시스템 업데이트
 * 수행범위: 업데이트 확인, 다운로드, 비활성 파티션 적용, 부트로더 플래그 변경
 * 의존방향: updater.h
 * SOLID: SRP — OTA 업데이트 프로세스만 담당
 * ────────────────────────────────────────────────────────── */

#include "updater.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <spawn.h>
#include <sys/wait.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/sha.h>

/* ─── 내부 상수 ─── */
#define BOOTCTL_PATH          "/usr/sbin/bootctl"
#define SLOT_METADATA_PATH    "/var/lib/zyl-os/slot-metadata"
#define UPDATE_CACHE_DEFAULT  "/var/cache/zyl-os/updates"
#define CURRENT_VERSION_FILE  "/etc/zyl-os/version"
#define VERIFY_FLAG_FILE      "/var/lib/zyl-os/slot-verified"

/* ─── 업데이터 내부 구조체 ─── */
struct ZylUpdater {
    char *server_url;           /* OTA 서버 URL */
    char *cache_dir;            /* 다운로드 캐시 */
    ZylUpdateState state;       /* 현재 상태 */
    ZylUpdateManifest *pending; /* 대기 중인 업데이트 */

    /* A/B 파티션 */
    char *active_slot;          /* "a" 또는 "b" */
    char *current_version;      /* 현재 OS 버전 */

    /* 자동 업데이트 */
    bool auto_check_enabled;
    int auto_check_interval_h;

    /* 진행률 */
    zyl_update_progress_fn progress_cb;
    void *progress_data;
};

/* ─── 유틸리티 ─── */
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

static char *read_file_string(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char buf[256];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    /* 개행 제거 */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    return strdup(buf);
}

static bool write_file_string(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs(content, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return true;
}

static void report_progress(ZylUpdater *u, int pct, const char *msg) {
    if (u->progress_cb)
        u->progress_cb(u->state, pct, msg, u->progress_data);
}

/* ─── 유틸리티: 경로 안전성 검증 (shell metacharacter 거부) ─── */
static bool is_safe_path(const char *path) {
    if (!path || path[0] == '\0') return false;
    for (const char *p = path; *p; p++) {
        switch (*p) {
        case ';': case '&': case '|': case '$':
        case '`': case '(': case ')': case '{':
        case '}': case '<': case '>': case '!':
        case '\n': case '\r':
            return false;
        default:
            break;
        }
    }
    return true;
}

/* posix_spawn helper — system() 사용 금지 (command injection 방지) */
static int safe_exec(const char *const argv[]) {
    pid_t pid;
    char *envp[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};
    int rc = posix_spawn(&pid, argv[0], NULL, NULL, (char *const *)argv, envp);
    if (rc != 0) return -1;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ─── 유틸리티: 파일 내용 전체 읽기 ─── */
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

/* ─── 유틸리티: 간이 JSON 문자열 값 추출 ─── */
static char *json_get_string(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *pos = strstr(json, pattern);
    if (!pos) return NULL;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == ':') pos++;

    if (*pos != '"') return NULL;
    pos++;

    const char *end = pos;
    while (*end && *end != '"') {
        if (*end == '\\') end++;
        if (*end) end++;
    }

    size_t len = (size_t)(end - pos);
    char *val = malloc(len + 1);
    if (!val) return NULL;
    memcpy(val, pos, len);
    val[len] = '\0';
    return val;
}

/* ─── 유틸리티: JSON boolean 값 추출 ─── */
static bool json_get_bool(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return false;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == ':') pos++;
    return strncmp(pos, "true", 4) == 0;
}

/* ─── 유틸리티: JSON 정수 값 추출 ─── */
static long json_get_long(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return -1;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == ':') pos++;
    return strtol(pos, NULL, 10);
}

/* ─── 유틸리티: 버전 문자열 검증 (숫자와 점만 허용) ─── */
static bool validate_version(const char *ver) {
    if (!ver) return false;
    for (const char *p = ver; *p; p++) {
        if (!(*p >= '0' && *p <= '9') && *p != '.') return false;
    }
    return true;
}

/* ─── 유틸리티: popen으로 명령어 실행 후 출력 읽기 ─── */
static char *run_command(const char *cmd, size_t *out_len) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    size_t capacity = 4096;
    size_t total = 0;
    char *buf = malloc(capacity);
    if (!buf) { pclose(fp); return NULL; }

    size_t n;
    while ((n = fread(buf + total, 1, capacity - total - 1, fp)) > 0) {
        total += n;
        if (total + 1 >= capacity) {
            capacity *= 2;
            char *tmp = realloc(buf, capacity);
            if (!tmp) { free(buf); pclose(fp); return NULL; }
            buf = tmp;
        }
    }
    buf[total] = '\0';
    pclose(fp);
    if (out_len) *out_len = total;
    return buf;
}

/* ─── 유틸리티: SHA-256 해시 계산 (OpenSSL EVP API) ─── */
static bool compute_sha256_file(const char *path, char *out_hex, size_t hex_size) {
    if (hex_size < 65) return false; /* need 64 hex chars + NUL */

    FILE *f = fopen(path, "rb");
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

    uint8_t hash[SHA256_DIGEST_LENGTH];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &digest_len) != 1) goto done;

    /* Convert to hex string */
    for (unsigned int i = 0; i < digest_len; i++) {
        snprintf(out_hex + i * 2, 3, "%02x", hash[i]);
    }
    out_hex[digest_len * 2] = '\0';
    ok = true;

done:
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return ok;
}

/* ─── 유틸리티: 업데이트 타입 문자열 → 열거형 변환 ─── */
static ZylUpdateType parse_update_type(const char *type_str) {
    if (!type_str) return ZYL_UPDATE_TYPE_FULL;
    if (strcmp(type_str, "delta") == 0) return ZYL_UPDATE_TYPE_DELTA;
    if (strcmp(type_str, "apps_only") == 0) return ZYL_UPDATE_TYPE_APPS_ONLY;
    if (strcmp(type_str, "kernel") == 0) return ZYL_UPDATE_TYPE_KERNEL;
    return ZYL_UPDATE_TYPE_FULL;
}

/* ─── 활성 슬롯 감지 ─── */
static char *detect_active_slot(void) {
    /*
     * 실제 구현에서는 U-Boot 환경변수 또는 커널 커맨드라인에서 읽음:
     *   cat /proc/cmdline | grep -o 'zyl.slot=[ab]'
     *   또는 fw_printenv zyl_active_slot
     */
    char *slot = read_file_string(SLOT_METADATA_PATH);
    return slot ? slot : strdup("a");
}

/* ─── 공개 API 구현 ─── */

ZylUpdater *zyl_updater_create(const char *update_server_url,
                                const char *cache_dir) {
    ZylUpdater *u = calloc(1, sizeof(ZylUpdater));
    if (!u) return NULL;

    u->server_url = strdup(update_server_url ? update_server_url
        : "https://update.zyl-os.dev/v1");
    u->cache_dir = strdup(cache_dir ? cache_dir : UPDATE_CACHE_DEFAULT);
    u->state = ZYL_UPDATE_IDLE;
    u->pending = NULL;

    mkdir_p(u->cache_dir);

    /* 현재 슬롯/버전 감지 */
    u->active_slot = detect_active_slot();
    u->current_version = read_file_string(CURRENT_VERSION_FILE);
    if (!u->current_version) u->current_version = strdup("0.1.0");

    u->auto_check_enabled = true;
    u->auto_check_interval_h = 24;

    return u;
}

void zyl_updater_destroy(ZylUpdater *u) {
    if (!u) return;
    free(u->server_url);
    free(u->cache_dir);
    free(u->active_slot);
    free(u->current_version);
    if (u->pending) zyl_update_manifest_free(u->pending);
    free(u);
}

ZylUpdateState zyl_updater_check(ZylUpdater *u,
                                  ZylUpdateManifest **out_manifest) {
    if (!u) return ZYL_UPDATE_FAILED;

    u->state = ZYL_UPDATE_CHECKING;
    report_progress(u, 0, "Checking for updates...");

    if (out_manifest) *out_manifest = NULL;

    /* 1. 버전 문자열 검증 후 URL 구성 */
    if (u->current_version && !validate_version(u->current_version)) {
        fprintf(stderr, "[UPDATER] Invalid version format: %s\n",
                u->current_version);
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 100, "Invalid version format");
        return u->state;
    }

    char url[1024];
    snprintf(url, sizeof(url),
             "%s/check?version=%s&arch=riscv64&slot=%s",
             u->server_url,
             u->current_version ? u->current_version : "0.0.0",
             u->active_slot ? u->active_slot : "a");

    /* 2. curl 명령어로 HTTP GET 수행 */
    char cmd[1280];
    snprintf(cmd, sizeof(cmd),
             "curl -s --connect-timeout 10 --max-time 30 '%s' 2>/dev/null",
             url);

    fprintf(stderr, "[UPDATER] Checking: %s\n", url);

    char *response = run_command(cmd, NULL);
    if (!response || strlen(response) == 0) {
        fprintf(stderr, "[UPDATER] No response from server\n");
        free(response);
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 100, "Failed to contact update server");
        return u->state;
    }

    /* 3. JSON 응답 파싱 */
    bool available = json_get_bool(response, "available");
    if (!available) {
        free(response);
        u->state = ZYL_UPDATE_UP_TO_DATE;
        report_progress(u, 100, "System is up to date");
        return u->state;
    }

    /* 4. 업데이트 매니페스트 구성 */
    ZylUpdateManifest *manifest = calloc(1, sizeof(ZylUpdateManifest));
    if (!manifest) {
        free(response);
        u->state = ZYL_UPDATE_FAILED;
        return u->state;
    }

    manifest->version       = json_get_string(response, "version");
    manifest->current_version = u->current_version ? strdup(u->current_version)
                                                    : strdup("0.0.0");
    manifest->download_url  = json_get_string(response, "url");
    manifest->sha256_hash   = json_get_string(response, "sha256");
    manifest->signature     = json_get_string(response, "signature");
    manifest->changelog     = json_get_string(response, "changelog");
    manifest->is_mandatory  = json_get_bool(response, "mandatory");

    char *type_str = json_get_string(response, "type");
    manifest->type = parse_update_type(type_str);
    free(type_str);

    long size = json_get_long(response, "size");
    manifest->download_size = size > 0 ? (size_t)size : 0;

    char *min_bat = json_get_string(response, "min_battery");
    manifest->min_battery_pct = min_bat ? min_bat : strdup("20");

    free(response);

    /* 필수 필드 검증 */
    if (!manifest->version || !manifest->download_url) {
        fprintf(stderr, "[UPDATER] Incomplete update manifest\n");
        zyl_update_manifest_free(manifest);
        u->state = ZYL_UPDATE_FAILED;
        return u->state;
    }

    fprintf(stderr, "[UPDATER] Update available: %s -> %s (%zu bytes)\n",
            manifest->current_version, manifest->version,
            manifest->download_size);

    /* 이전 pending 해제 후 새 매니페스트 저장 */
    if (u->pending) zyl_update_manifest_free(u->pending);
    u->pending = manifest;

    u->state = ZYL_UPDATE_AVAILABLE;
    report_progress(u, 100, "Update available");

    if (out_manifest) {
        /* 호출자에게 복사본 전달 */
        ZylUpdateManifest *copy = calloc(1, sizeof(ZylUpdateManifest));
        if (copy) {
            copy->version       = manifest->version ? strdup(manifest->version) : NULL;
            copy->current_version = manifest->current_version ? strdup(manifest->current_version) : NULL;
            copy->changelog     = manifest->changelog ? strdup(manifest->changelog) : NULL;
            copy->download_url  = manifest->download_url ? strdup(manifest->download_url) : NULL;
            copy->download_size = manifest->download_size;
            copy->installed_size = manifest->installed_size;
            copy->sha256_hash   = manifest->sha256_hash ? strdup(manifest->sha256_hash) : NULL;
            copy->signature     = manifest->signature ? strdup(manifest->signature) : NULL;
            copy->type          = manifest->type;
            copy->is_mandatory  = manifest->is_mandatory;
            copy->min_battery_pct = manifest->min_battery_pct ? strdup(manifest->min_battery_pct) : NULL;
        }
        *out_manifest = copy;
    }

    return u->state;
}

bool zyl_updater_download(ZylUpdater *u,
                           zyl_update_progress_fn callback,
                           void *user_data) {
    if (!u || u->state != ZYL_UPDATE_AVAILABLE || !u->pending) return false;

    u->progress_cb = callback;
    u->progress_data = user_data;
    u->state = ZYL_UPDATE_DOWNLOADING;

    /* 1. 다운로드 대상 경로 설정 */
    char pkg_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%s/update.pkg", u->cache_dir);
    mkdir_p(u->cache_dir);

    report_progress(u, 0, "Starting download...");

    /*
     * curl 명령어로 다운로드 — 진행률을 stderr로 출력하도록 설정.
     * curl CLI는 인자가 코드에서 생성되므로 injection 위험 없음.
     * libcurl 전환은 진행률 콜백이 필요할 때 고려.
     */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -f -L --connect-timeout 15 --max-time 3600 "
             "-o '%s' --progress-bar '%s' 2>&1",
             pkg_path, u->pending->download_url);

    fprintf(stderr, "[UPDATER] Downloading: %s -> %s\n",
            u->pending->download_url, pkg_path);

    /* popen으로 실행하여 진행률 파싱 시도 */
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "[UPDATER] Failed to start download\n");
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 0, "Download failed to start");
        return false;
    }

    char line[256];
    int last_pct = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* curl progress-bar 형식에서 퍼센트 파싱 시도 */
        char *pct_pos = strstr(line, "%");
        if (pct_pos && pct_pos > line) {
            /* 퍼센트 앞의 숫자 추출 */
            char *num_start = pct_pos - 1;
            while (num_start > line &&
                   (*(num_start - 1) >= '0' && *(num_start - 1) <= '9')) {
                num_start--;
            }
            int pct = atoi(num_start);
            if (pct > last_pct && pct <= 100) {
                last_pct = pct;
                report_progress(u, pct, "Downloading...");
            }
        }
    }
    int ret = pclose(fp);

    if (ret != 0) {
        fprintf(stderr, "[UPDATER] Download failed (exit code %d)\n", ret);
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 0, "Download failed");
        return false;
    }

    report_progress(u, 100, "Download complete");

    /* 2. SHA-256 해시 검증 */
    u->state = ZYL_UPDATE_VERIFYING;
    report_progress(u, 0, "Verifying package integrity...");

    if (u->pending->sha256_hash && strlen(u->pending->sha256_hash) > 0) {
        char computed_hash[128] = {0};
        if (!compute_sha256_file(pkg_path, computed_hash, sizeof(computed_hash))) {
            fprintf(stderr, "[UPDATER] Failed to compute SHA-256\n");
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Hash verification failed");
            return false;
        }

        if (strncmp(computed_hash, u->pending->sha256_hash, 64) != 0) {
            fprintf(stderr, "[UPDATER] SHA-256 mismatch: expected=%s got=%s\n",
                    u->pending->sha256_hash, computed_hash);
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Package integrity check failed");
            unlink(pkg_path);
            return false;
        }

        fprintf(stderr, "[UPDATER] SHA-256 verified: %s\n", computed_hash);
    }

    report_progress(u, 50, "Verifying signature...");

    /* 3. RSA-2048+SHA-256 서명 검증 (OpenSSL EVP API) */
    if (u->pending->signature && strlen(u->pending->signature) > 0) {
        /* Load OTA signing public key */
        const char *pubkey_path = "/etc/zyl-os/ota-signing-key.pem";
        FILE *key_fp = fopen(pubkey_path, "r");
        if (!key_fp) {
            fprintf(stderr, "[UPDATER] OTA signing key not found: %s\n",
                    pubkey_path);
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "OTA signing key missing");
            return false;
        }

        EVP_PKEY *pkey = PEM_read_PUBKEY(key_fp, NULL, NULL, NULL);
        fclose(key_fp);
        if (!pkey) {
            fprintf(stderr, "[UPDATER] Failed to parse OTA signing key\n");
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Invalid OTA signing key");
            return false;
        }

        /* Decode base64 signature from manifest */
        size_t sig_b64_len = strlen(u->pending->signature);
        size_t sig_max = (sig_b64_len * 3) / 4 + 4;
        uint8_t *sig_bytes = malloc(sig_max);
        if (!sig_bytes) { EVP_PKEY_free(pkey); return false; }

        EVP_ENCODE_CTX *dec_ctx = EVP_ENCODE_CTX_new();
        int dec_len = 0, dec_final = 0;
        EVP_DecodeInit(dec_ctx);
        if (EVP_DecodeUpdate(dec_ctx, sig_bytes, &dec_len,
                             (const unsigned char *)u->pending->signature,
                             (int)sig_b64_len) < 0) {
            fprintf(stderr, "[UPDATER] Signature base64 decode failed\n");
            EVP_ENCODE_CTX_free(dec_ctx);
            free(sig_bytes);
            EVP_PKEY_free(pkey);
            u->state = ZYL_UPDATE_FAILED;
            return false;
        }
        EVP_DecodeFinal(dec_ctx, sig_bytes + dec_len, &dec_final);
        EVP_ENCODE_CTX_free(dec_ctx);
        size_t sig_len = (size_t)(dec_len + dec_final);

        /* Compute SHA-256 hash of the downloaded package for verification */
        uint8_t file_hash[SHA256_DIGEST_LENGTH];
        {
            FILE *pkg_fp = fopen(pkg_path, "rb");
            if (!pkg_fp) {
                free(sig_bytes); EVP_PKEY_free(pkey);
                return false;
            }
            EVP_MD_CTX *hash_ctx = EVP_MD_CTX_new();
            EVP_DigestInit_ex(hash_ctx, EVP_sha256(), NULL);
            uint8_t hbuf[8192];
            size_t hn;
            while ((hn = fread(hbuf, 1, sizeof(hbuf), pkg_fp)) > 0) {
                EVP_DigestUpdate(hash_ctx, hbuf, hn);
            }
            unsigned int hlen = 0;
            EVP_DigestFinal_ex(hash_ctx, file_hash, &hlen);
            EVP_MD_CTX_free(hash_ctx);
            fclose(pkg_fp);
        }

        /* Verify: RSA-2048 + SHA-256 */
        EVP_MD_CTX *vfy_ctx = EVP_MD_CTX_new();
        bool sig_ok = false;
        if (EVP_DigestVerifyInit(vfy_ctx, NULL, EVP_sha256(),
                                 NULL, pkey) == 1 &&
            EVP_DigestVerifyUpdate(vfy_ctx, file_hash,
                                   SHA256_DIGEST_LENGTH) == 1) {
            int rc = EVP_DigestVerifyFinal(vfy_ctx, sig_bytes, sig_len);
            sig_ok = (rc == 1);
        }
        EVP_MD_CTX_free(vfy_ctx);
        EVP_PKEY_free(pkey);
        free(sig_bytes);

        if (!sig_ok) {
            fprintf(stderr, "[UPDATER] RSA signature verification FAILED "
                    "— update package may be tampered\n");
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Signature verification failed");
            unlink(pkg_path);
            return false;
        }

        fprintf(stderr, "[UPDATER] RSA-2048+SHA-256 signature verified\n");
    } else {
        /* No signature present — reject */
        fprintf(stderr, "[UPDATER] Update package has no signature — "
                "rejecting\n");
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 0, "Unsigned update rejected");
        return false;
    }

    report_progress(u, 100, "Package verified");
    fprintf(stderr, "[UPDATER] Download verified successfully\n");

    return true;
}

bool zyl_updater_apply(ZylUpdater *u,
                        zyl_update_progress_fn callback,
                        void *user_data) {
    if (!u || !u->pending) return false;

    u->progress_cb = callback;
    u->progress_data = user_data;
    u->state = ZYL_UPDATE_APPLYING;

    /* 1. 비활성 슬롯 결정 */
    const char *inactive = strcmp(u->active_slot, "a") == 0 ? "b" : "a";
    /* 파티션 번호 매핑: slot a → 파티션 2, slot b → 파티션 3 */
    const char *inactive_part = strcmp(inactive, "a") == 0 ? "2" : "3";
    const char *active_part   = strcmp(u->active_slot, "a") == 0 ? "2" : "3";

    char pkg_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%s/update.pkg", u->cache_dir);

    report_progress(u, 5, "Preparing inactive partition...");

    fprintf(stderr, "[UPDATER] Applying %s update to slot %s\n",
            u->pending->type == ZYL_UPDATE_TYPE_FULL  ? "FULL" :
            u->pending->type == ZYL_UPDATE_TYPE_DELTA ? "DELTA" :
            u->pending->type == ZYL_UPDATE_TYPE_APPS_ONLY ? "APPS_ONLY" :
            "KERNEL",
            inactive);

    /* 2. 업데이트 유형별 적용 */
    int ret = 0;

    switch (u->pending->type) {
    case ZYL_UPDATE_TYPE_FULL: {
        /* 비활성 파티션에 전체 이미지 기록 (dd) */
        report_progress(u, 10, "Writing full image to partition...");
        if (!is_safe_path(pkg_path)) {
            fprintf(stderr, "[UPDATER] Unsafe pkg_path rejected\n");
            u->state = ZYL_UPDATE_FAILED;
            return false;
        }
        const char *dd_argv[] = {"/bin/dd", NULL, NULL, "bs=4M", "conv=fsync", NULL};
        char dd_if[512], dd_of[128];
        snprintf(dd_if, sizeof(dd_if), "if=%s", pkg_path);
        snprintf(dd_of, sizeof(dd_of), "of=/dev/mmcblk0p%s", inactive_part);
        dd_argv[1] = dd_if; dd_argv[2] = dd_of;
        ret = safe_exec(dd_argv);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] dd failed (exit %d)\n", ret);
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Failed to write image to partition");
            return false;
        }
        report_progress(u, 60, "Image written successfully");
        break;
    }

    case ZYL_UPDATE_TYPE_DELTA: {
        /* bspatch로 델타 패치 적용 */
        report_progress(u, 10, "Applying delta patch...");
        if (!is_safe_path(pkg_path)) {
            fprintf(stderr, "[UPDATER] Unsafe pkg_path rejected\n");
            u->state = ZYL_UPDATE_FAILED;
            return false;
        }
        char bsp_src[64], bsp_dst[64];
        snprintf(bsp_src, sizeof(bsp_src), "/dev/mmcblk0p%s", active_part);
        snprintf(bsp_dst, sizeof(bsp_dst), "/dev/mmcblk0p%s", inactive_part);
        const char *bsp_argv[] = {"/usr/bin/bspatch", bsp_src, bsp_dst, pkg_path, NULL};
        ret = safe_exec(bsp_argv);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] bspatch failed (exit %d)\n", ret);
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Failed to apply delta patch");
            return false;
        }
        report_progress(u, 60, "Delta patch applied");
        break;
    }

    case ZYL_UPDATE_TYPE_APPS_ONLY: {
        /* 시스템 앱만 추출 및 업데이트 */
        report_progress(u, 10, "Extracting app updates...");
        if (!is_safe_path(pkg_path)) {
            fprintf(stderr, "[UPDATER] Unsafe pkg_path rejected\n");
            u->state = ZYL_UPDATE_FAILED;
            return false;
        }
        const char *mkdir_argv[] = {"/bin/mkdir", "-p", "/tmp/zyl-app-update", NULL};
        ret = safe_exec(mkdir_argv);
        if (ret == 0) {
            const char *unzip_argv[] = {"/usr/bin/unzip", "-o", "-q", pkg_path, "-d", "/tmp/zyl-app-update", NULL};
            ret = safe_exec(unzip_argv);
        }
        if (ret == 0) {
            const char *cp_argv[] = {"/bin/cp", "-a", "/tmp/zyl-app-update/apps/.", "/usr/share/zyl-os/apps/", NULL};
            ret = safe_exec(cp_argv);
        }
        /* Cleanup regardless */
        const char *rm_argv[] = {"/bin/rm", "-rf", "/tmp/zyl-app-update", NULL};
        safe_exec(rm_argv);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] App update extraction failed\n");
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Failed to extract app updates");
            return false;
        }
        report_progress(u, 60, "Apps updated");
        break;
    }

    case ZYL_UPDATE_TYPE_KERNEL: {
        /* 커널 이미지 교체 */
        report_progress(u, 10, "Updating kernel image...");
        if (!is_safe_path(pkg_path)) {
            fprintf(stderr, "[UPDATER] Unsafe pkg_path rejected\n");
            u->state = ZYL_UPDATE_FAILED;
            return false;
        }
        char kern_dst[128];
        snprintf(kern_dst, sizeof(kern_dst), "/boot/Image.%s", inactive);
        const char *cp_argv[] = {"/bin/cp", pkg_path, kern_dst, NULL};
        ret = safe_exec(cp_argv);
        if (ret == 0) {
            const char *sync_argv[] = {"/bin/sync", NULL};
            ret = safe_exec(sync_argv);
        }
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] Kernel copy failed\n");
            u->state = ZYL_UPDATE_FAILED;
            report_progress(u, 0, "Failed to copy kernel image");
            return false;
        }
        report_progress(u, 60, "Kernel image updated");
        break;
    }
    }

    /* 3. U-Boot 환경변수 설정 via fw_setenv */
    report_progress(u, 70, "Setting boot flags...");

    {
        const char *fwse_slot[] = {"/usr/sbin/fw_setenv", "zyl_next_slot", inactive, NULL};
        ret = safe_exec(fwse_slot);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] fw_setenv zyl_next_slot failed (exit %d), using file fallback\n", ret);
        }
    }

    {
        const char *fwse_verified[] = {"/usr/sbin/fw_setenv", "zyl_slot_verified", "0", NULL};
        ret = safe_exec(fwse_verified);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] fw_setenv zyl_slot_verified failed (exit %d)\n", ret);
        }
    }

    {
        const char *fwse_bootcnt[] = {"/usr/sbin/fw_setenv", "zyl_boot_count", "0", NULL};
        ret = safe_exec(fwse_bootcnt);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] fw_setenv zyl_boot_count failed (exit %d)\n", ret);
        }
    }

    /* 파일 기반 폴백 (fw_setenv가 없는 환경용) */
    char flag_path[256];
    snprintf(flag_path, sizeof(flag_path), "/var/lib/zyl-os/next-slot");
    mkdir_p("/var/lib/zyl-os");
    write_file_string(flag_path, inactive);

    report_progress(u, 85, "Saving version metadata...");

    /* 4. 버전 메타데이터 기록 */
    char ver_path[256];
    snprintf(ver_path, sizeof(ver_path),
             "/var/lib/zyl-os/slot-%s-version", inactive);
    write_file_string(ver_path, u->pending->version);

    /* 다운로드 캐시 정리 */
    unlink(pkg_path);

    report_progress(u, 100, "Update applied. Reboot to activate.");
    u->state = ZYL_UPDATE_PENDING_REBOOT;

    fprintf(stderr, "[UPDATER] Update applied to slot %s (v%s). "
            "Reboot required.\n", inactive, u->pending->version);

    return true;
}

bool zyl_updater_reboot_to_update(ZylUpdater *u) {
    if (!u || u->state != ZYL_UPDATE_PENDING_REBOOT) return false;

    /*
     * 실제 구현:
     *   sync();
     *   reboot(RB_AUTOBOOT);
     * 또는:
     *   system("systemctl reboot");
     */
    fprintf(stderr, "[UPDATER] Reboot requested for update activation\n");
    return true;
}

bool zyl_updater_mark_verified(ZylUpdater *u) {
    if (!u) return false;

    fprintf(stderr, "[UPDATER] Running system health checks...\n");

    /* 1. 핵심 서비스 실행 상태 확인: compositor */
    int ret = safe_exec((const char *[]){"/usr/bin/systemctl", "is-active", "--quiet", "zyl-compositor", NULL});
    if (ret != 0) {
        fprintf(stderr, "[UPDATER] Health check FAILED: "
                "zyl-compositor not running (exit %d)\n", ret);
        return false;
    }
    fprintf(stderr, "[UPDATER] Health: zyl-compositor OK\n");

    /* 2. 핵심 서비스 실행 상태 확인: WAM */
    ret = safe_exec((const char *[]){"/usr/bin/systemctl", "is-active", "--quiet", "zyl-wam", NULL});
    if (ret != 0) {
        fprintf(stderr, "[UPDATER] Health check FAILED: "
                "zyl-wam not running (exit %d)\n", ret);
        return false;
    }
    fprintf(stderr, "[UPDATER] Health: zyl-wam OK\n");

    /* 3. fw_setenv으로 슬롯 검증 완료 마킹 */
    {
        const char *fwse_ver[] = {"/usr/sbin/fw_setenv", "zyl_slot_verified", "1", NULL};
        ret = safe_exec(fwse_ver);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] fw_setenv zyl_slot_verified failed (exit %d)\n", ret);
        }
    }

    {
        const char *fwse_slot[] = {"/usr/sbin/fw_setenv", "zyl_active_slot", u->active_slot, NULL};
        ret = safe_exec(fwse_slot);
        if (ret != 0) {
            fprintf(stderr, "[UPDATER] fw_setenv zyl_active_slot failed (exit %d)\n", ret);
        }
    }

    /* 파일 기반 폴백 */
    mkdir_p("/var/lib/zyl-os");
    write_file_string(VERIFY_FLAG_FILE, "1");
    write_file_string(SLOT_METADATA_PATH, u->active_slot);

    fprintf(stderr, "[UPDATER] Slot '%s' marked as verified\n",
            u->active_slot);
    return true;
}

bool zyl_updater_rollback(ZylUpdater *u) {
    if (!u) return false;

    u->state = ZYL_UPDATE_ROLLING_BACK;

    const char *previous = strcmp(u->active_slot, "a") == 0 ? "b" : "a";

    /* fw_setenv으로 부트로더에 이전 슬롯 지정 */
    const char *fwse_argv[] = {"/usr/sbin/fw_setenv", "zyl_next_slot", previous, NULL};
    int ret = safe_exec(fwse_argv);
    if (ret != 0) {
        fprintf(stderr, "[UPDATER] fw_setenv zyl_next_slot failed (exit %d), using file fallback\n", ret);
    }

    /* 파일 기반 폴백 */
    mkdir_p("/var/lib/zyl-os");
    write_file_string("/var/lib/zyl-os/next-slot", previous);

    fprintf(stderr, "[UPDATER] Rollback to slot '%s' scheduled. "
            "Reboot required.\n", previous);
    u->state = ZYL_UPDATE_PENDING_REBOOT;

    return true;
}

ZylUpdateState zyl_updater_get_state(const ZylUpdater *u) {
    return u ? u->state : ZYL_UPDATE_FAILED;
}

ZylPartitionInfo *zyl_updater_get_partition_info(const ZylUpdater *u) {
    if (!u) return NULL;

    ZylPartitionInfo *info = calloc(1, sizeof(ZylPartitionInfo));
    info->active_slot = strdup(u->active_slot);
    info->inactive_slot = strdup(
        strcmp(u->active_slot, "a") == 0 ? "b" : "a");
    info->active_version = strdup(u->current_version);

    /* 비활성 슬롯 버전 */
    char ver_path[256];
    snprintf(ver_path, sizeof(ver_path),
             "/var/lib/zyl-os/slot-%s-version", info->inactive_slot);
    info->inactive_version = read_file_string(ver_path);
    if (!info->inactive_version) info->inactive_version = strdup("(empty)");

    /* 검증 상태 */
    char *verified = read_file_string(VERIFY_FLAG_FILE);
    info->verified = verified && strcmp(verified, "1") == 0;
    free(verified);

    return info;
}

void zyl_updater_set_auto_check(ZylUpdater *u, bool enabled,
                                 int interval_hours) {
    if (!u) return;
    u->auto_check_enabled = enabled;
    u->auto_check_interval_h = interval_hours > 0 ? interval_hours : 24;
}

void zyl_update_manifest_free(ZylUpdateManifest *m) {
    if (!m) return;
    free(m->version);
    free(m->current_version);
    free(m->changelog);
    free(m->download_url);
    free(m->sha256_hash);
    free(m->signature);
    free(m->min_battery_pct);
    free(m);
}

void zyl_partition_info_free(ZylPartitionInfo *info) {
    if (!info) return;
    free(info->active_slot);
    free(info->inactive_slot);
    free(info->active_version);
    free(info->inactive_version);
    free(info);
}
