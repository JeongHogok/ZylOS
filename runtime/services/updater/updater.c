#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: OTA 업데이트 흐름 — A/B 파티션 기반 원자적 시스템 업데이트
 * 수행범위: 업데이트 확인·다운로드, 언제 어느 슬롯에 설치하고 부트로더를 돌릴지
 *           (시험 부팅 보호, 대기 중 설치 취소, 확정·롤백). 신뢰 판정은
 *           ota_verify.c, 슬롯 기록은 ota_install.c, 부트로더 환경은 Boot HAL
 * 의존방향: updater.h, ota_verify.h, ota_install.h, hal.h (Boot HAL), curl
 * SOLID: SRP — OTA 흐름만. DIP — 부트로더·파티션은 ZylBootHal 로만
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_OS_VERSION
#define ZYL_OS_VERSION "unknown"   /* meson sets it from apps/system/release.js */
#endif

#include "updater.h"
#include "ota_verify.h"
#include "ota_install.h"
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/reboot.h>


/* ─── 내부 상수 ─── */
/* Overridable so tests install into scratch files */
#ifndef ZYL_STATE_DIR
#define ZYL_STATE_DIR         "/data/updater"
#endif
#ifndef ZYL_OTA_KEY
#define ZYL_OTA_KEY           "/etc/zyl-os/ota-signing-key.pem"
#endif
#define SLOT_METADATA_PATH    ZYL_STATE_DIR "/slot-metadata"
/* On /data: a package is as large as a rootfs and /var is RAM (tmpfs) */
#define UPDATE_CACHE_DEFAULT  ZYL_STATE_DIR "/cache"
#define CURRENT_VERSION_FILE  "/etc/zyl-os/version"
#define VERIFY_FLAG_FILE      ZYL_STATE_DIR "/slot-verified"

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

    bool pkg_verified;          /* update.pkg passed the hash and signature check */
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
    if (fsync(fileno(f)) != 0) {
        fprintf(stderr, "[UPDATER] fsync failed for %s: %s\n", path, strerror(errno));
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

static void report_progress(ZylUpdater *u, int pct, const char *msg) {
    if (u->progress_cb)
        u->progress_cb(u->state, pct, msg, u->progress_data);
}

/* ─── 유틸리티: 경로 안전성 검증 (shell metacharacter + path traversal 거부) ─── */
static bool is_safe_path(const char *path) {
    if (!path || path[0] == '\0') return false;
    /* Reject path traversal */
    if (strstr(path, "..") != NULL) return false;
    /* Reject shell metacharacters */
    for (const char *p = path; *p; p++) {
        switch (*p) {
        case ';': case '&': case '|': case '$':
        case '`': case '(': case ')': case '{':
        case '}': case '<': case '>': case '!':
        case '\n': case '\r': case '\'': case '"':
            return false;
        default:
            break;
        }
    }
    return true;
}

/**
 * Validate URL for safe use in shell commands.
 * Must start with https:// and contain no shell metacharacters.
 * HTTP (plaintext) is rejected to prevent MITM on OTA metadata.
 */
static bool is_safe_url(const char *url) {
    if (!url) return false;
    if (strncmp(url, "https://", 8) != 0)
        return false;
    return is_safe_path(url);
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

/* ─── 유틸리티: JSON boolean 값 추출 (json-glib) ─── */
static bool json_get_bool(const char *json_str, const char *key) {
    if (!json_str || !key) return false;
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_str, -1, NULL)) {
        g_object_unref(parser);
        return false;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return false;
    }
    JsonObject *obj = json_node_get_object(root);
    bool result = false;
    if (json_object_has_member(obj, key)) {
        result = json_object_get_boolean_member(obj, key);
    }
    g_object_unref(parser);
    return result;
}

/* ─── 유틸리티: JSON 정수 값 추출 (json-glib) ─── */
static long json_get_long(const char *json_str, const char *key) {
    if (!json_str || !key) return -1;
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_str, -1, NULL)) {
        g_object_unref(parser);
        return -1;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return -1;
    }
    JsonObject *obj = json_node_get_object(root);
    long result = -1;
    if (json_object_has_member(obj, key)) {
        result = (long)json_object_get_int_member(obj, key);
    }
    g_object_unref(parser);
    return result;
}

/* ─── 유틸리티: 버전 문자열 검증 (숫자와 점만 허용) ─── */
static bool validate_version(const char *ver) {
    if (!ver) return false;
    for (const char *p = ver; *p; p++) {
        if (!(*p >= '0' && *p <= '9') && *p != '.') return false;
    }
    return true;
}

/* ─── 유틸리티: SHA-256 해시 계산 (OpenSSL EVP API) ─── */
/* ─── 유틸리티: 업데이트 타입 문자열 → 열거형 변환 ─── */
static ZylUpdateType parse_update_type(const char *type_str) {
    if (!type_str) return ZYL_UPDATE_TYPE_FULL;
    if (strcmp(type_str, "delta") == 0) return ZYL_UPDATE_TYPE_DELTA;
    if (strcmp(type_str, "apps_only") == 0) return ZYL_UPDATE_TYPE_APPS_ONLY;
    if (strcmp(type_str, "kernel") == 0) return ZYL_UPDATE_TYPE_KERNEL;
    return ZYL_UPDATE_TYPE_FULL;
}

/* ─── 활성 슬롯 감지 ─── */
/* The slot that actually booted: its signed FIT carries zyl.slot= (tools/
 * sign-image.sh), so this is the truth even while U-Boot is trying an update
 * and zyl_active_slot still names the previous slot. */
#ifndef ZYL_CMDLINE
#define ZYL_CMDLINE "/proc/cmdline"
#endif
static char *detect_active_slot(void) {
    /* The whole line: a signed one carries the verity values ahead of zyl.slot=
       and is longer than any fixed buffer — a cut-off line read slot "a" on
       slot B, so no update was ever confirmed and every one rolled back */
    gchar *cmdline = NULL;
    if (g_file_get_contents(ZYL_CMDLINE, &cmdline, NULL, NULL)) {
        const char *p = strstr(cmdline, "zyl.slot=");
        if (p && (p[9] == 'a' || p[9] == 'b')) {
            char *slot = strndup(p + 9, 1);
            g_free(cmdline);
            return slot;
        }
    }
    g_free(cmdline);
    char *slot = read_file_string(SLOT_METADATA_PATH);
    return slot ? slot : strdup("a");
}

/* ─── 공개 API 구현 ─── */

ZylUpdater *zyl_updater_create(const char *update_server_url,
                                const char *cache_dir) {
    ZylUpdater *u = calloc(1, sizeof(ZylUpdater));
    if (!u) return NULL;

    u->server_url = strdup(update_server_url ? update_server_url
        : "https://update.zyl-os.com/v1");
    u->cache_dir = strdup(cache_dir ? cache_dir : UPDATE_CACHE_DEFAULT);
    if (!u->server_url || !u->cache_dir) {
        free(u->server_url);
        free(u->cache_dir);
        free(u);
        return NULL;
    }
    u->state = ZYL_UPDATE_IDLE;
    u->pending = NULL;

    mkdir_p(u->cache_dir);

    /* 현재 슬롯/버전 감지 */
    u->active_slot = detect_active_slot();
    u->current_version = read_file_string(CURRENT_VERSION_FILE);
    if (!u->current_version) u->current_version = strdup(ZYL_OS_VERSION);

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

    /* Validate server_url before constructing shell command */
    if (!is_safe_url(u->server_url)) {
        fprintf(stderr, "[UPDATER] Unsafe server URL: %s\n", u->server_url);
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 100, "Invalid server URL");
        return u->state;
    }

    char url[1024];
    snprintf(url, sizeof(url),
             "%s/check?version=%s&arch=riscv64&slot=%s",
             u->server_url,
             u->current_version ? u->current_version : "0.0.0",
             u->active_slot ? u->active_slot : "a");

    /* 2. curl via posix_spawn (no shell) */
    fprintf(stderr, "[UPDATER] Checking: %s\n", url);

    int check_pfd[2];
    char *response = NULL;
    if (pipe(check_pfd) == 0) {
        pid_t check_pid;
        posix_spawn_file_actions_t check_acts;
        posix_spawn_file_actions_init(&check_acts);
        posix_spawn_file_actions_adddup2(&check_acts, check_pfd[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&check_acts, check_pfd[0]);

        const char *check_argv[] = {
            "/usr/bin/curl", "-s",
            "--connect-timeout", "10", "--max-time", "30",
            url, NULL
        };
        char *check_env[] = { "PATH=/usr/bin:/bin", NULL };
        int check_rc = posix_spawn(&check_pid, "/usr/bin/curl",
                                    &check_acts, NULL,
                                    (char *const *)check_argv, check_env);
        posix_spawn_file_actions_destroy(&check_acts);
        close(check_pfd[1]);

        if (check_rc == 0) {
            size_t cap = 8192, total = 0;
            response = malloc(cap);
            if (response) {
                ssize_t n;
                while ((n = read(check_pfd[0], response + total, cap - total - 1)) > 0) {
                    total += (size_t)n;
                    if (total + 1 >= cap) {
                        cap *= 2;
                        char *tmp = realloc(response, cap);
                        if (!tmp) break;
                        response = tmp;
                    }
                }
                response[total] = '\0';
                if (total == 0) { free(response); response = NULL; }
            }
            int wst = 0;
            waitpid(check_pid, &wst, 0);
        }
        close(check_pfd[0]);
    }
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
    u->pkg_verified = false;

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

static bool verify_package(ZylUpdater *u, const char *pkg_path);

bool zyl_updater_download(ZylUpdater *u,
                           zyl_update_progress_fn callback,
                           void *user_data) {
    if (!u || u->state != ZYL_UPDATE_AVAILABLE || !u->pending) return false;

    u->progress_cb = callback;
    u->progress_data = user_data;
    u->state = ZYL_UPDATE_DOWNLOADING;
    u->pkg_verified = false;

    /* 1. 다운로드 대상 경로 설정 */
    char pkg_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%s/update.pkg", u->cache_dir);
    mkdir_p(u->cache_dir);

    report_progress(u, 0, "Starting download...");

    /*
     * curl 명령어로 다운로드 — 진행률을 stderr로 출력하도록 설정.
     * download_url은 서버 응답에서 파싱되므로 반드시 검증 필요.
     */
    if (!is_safe_url(u->pending->download_url)) {
        fprintf(stderr, "[UPDATER] Unsafe download URL: %s\n",
                u->pending->download_url);
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 100, "Invalid download URL");
        return false;
    }

    fprintf(stderr, "[UPDATER] Downloading: %s -> %s\n",
            u->pending->download_url, pkg_path);

    /* curl via posix_spawn; capture progress output without a shell */
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        fprintf(stderr, "[UPDATER] Failed to create progress pipe\n");
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 0, "Download failed to start");
        return false;
    }

    pid_t dl_pid;
    posix_spawn_file_actions_t dl_acts;
    posix_spawn_file_actions_init(&dl_acts);
    posix_spawn_file_actions_adddup2(&dl_acts, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&dl_acts, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&dl_acts, pipefd[0]);

    const char *dl_argv[] = {
        "/usr/bin/curl", "-f", "-L",
        "--connect-timeout", "15",
        "--max-time", "3600",
        "-o", pkg_path,
        "--progress-bar",
        u->pending->download_url,
        NULL
    };
    char *dl_env[] = { "PATH=/usr/bin:/bin", NULL };
    int spawn_rc = posix_spawn(&dl_pid, "/usr/bin/curl", &dl_acts, NULL,
                               (char *const *)dl_argv, dl_env);
    posix_spawn_file_actions_destroy(&dl_acts);
    close(pipefd[1]);
    if (spawn_rc != 0) {
        close(pipefd[0]);
        fprintf(stderr, "[UPDATER] Failed to start curl download\n");
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 0, "Download failed to start");
        return false;
    }

    FILE *fp = fdopen(pipefd[0], "r");
    char line[256];
    int last_pct = 0;
    while (fp && fgets(line, sizeof(line), fp)) {
        char *pct_pos = strstr(line, "%");
        if (pct_pos && pct_pos > line) {
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
    if (fp) fclose(fp); else close(pipefd[0]);

    int status = 0;
    waitpid(dl_pid, &status, 0);
    int ret = (WIFEXITED(status) ? WEXITSTATUS(status) : -1);

    if (ret != 0) {
        fprintf(stderr, "[UPDATER] Download failed (exit code %d)\n", ret);
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 0, "Download failed");
        return false;
    }

    report_progress(u, 100, "Download complete");
    return verify_package(u, pkg_path);
}

/* The package is trusted only after this: its hash, and an RSA-SHA256
   signature over that hash by the OTA key the image ships with */
static bool verify_package(ZylUpdater *u, const char *pkg_path) {
    u->state = ZYL_UPDATE_VERIFYING;
    report_progress(u, 0, "Verifying package...");
    ZylOtaVerdict v = zyl_ota_verify(pkg_path, u->pending->sha256_hash,
                                     u->pending->signature, ZYL_OTA_KEY);
    if (v != ZYL_OTA_TRUSTED) {
        fprintf(stderr, "[UPDATER] Package rejected: %s\n", zyl_ota_verdict_message(v));
        u->state = ZYL_UPDATE_FAILED;
        report_progress(u, 0, zyl_ota_verdict_message(v));
        /* Corrupt or tampered: never keep it around to be installed later */
        if (v == ZYL_OTA_HASH_MISMATCH || v == ZYL_OTA_BAD_SIGNATURE) unlink(pkg_path);
        return false;
    }
    report_progress(u, 100, zyl_ota_verdict_message(v));
    u->pkg_verified = true;
    return true;
}

bool zyl_updater_supported(const ZylUpdater *u) {
    (void)u;
    return zyl_hal_boot()->present() && access(ZYL_OTA_KEY, R_OK) == 0;
}

bool zyl_updater_on_trial(const ZylUpdater *u) {
    char *next = zyl_hal_boot()->env_get("zyl_next_slot");
    bool trial = u && next && strcmp(next, u->active_slot) == 0;
    free(next);
    return trial;
}

/* ota_install's steps, inside the 5–80 % of the whole apply */
static void install_step(int pct, const char *msg, void *data) {
    report_progress(data, 5 + pct * 75 / 100, msg);
}

static bool apply_failed(ZylUpdater *u, const char *msg) {
    fprintf(stderr, "[UPDATER] %s\n", msg);
    u->state = ZYL_UPDATE_FAILED;
    report_progress(u, 0, msg);
    return false;
}

/* A slot boots only when its rootfs, its verity tree and its signed FIT
   (which carries that tree's root hash) all match, so a package (tools/
   build-ota.sh) carries all of them for both slots: rootfs.img,
   verity-hashtree-{a,b}.img, fit-image-{a,b}.itb. Only the inactive slot is
   written, and U-Boot is pointed at it only after every write landed. */
bool zyl_updater_apply(ZylUpdater *u,
                        zyl_update_progress_fn callback,
                        void *user_data) {
    if (!u || !u->pending) return false;

    u->progress_cb = callback;
    u->progress_data = user_data;
    u->state = ZYL_UPDATE_APPLYING;

    char pkg_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%s/update.pkg", u->cache_dir);
    if (!u->pkg_verified) return apply_failed(u, "Update package is not verified");
    if (!is_safe_path(pkg_path)) return apply_failed(u, "Unsafe package path");
    /* shortcut: full images only — a delta via bspatch holds a whole
       partition in RAM, and apps or a kernel alone cannot match a dm-verity
       rootfs; add a block-level delta format when download size matters */
    if (u->pending->type != ZYL_UPDATE_TYPE_FULL)
        return apply_failed(u, "Only full system images can be installed");

    /* The slot to write is the one U-Boot falls back to while the running
       slot is still on trial — never overwrite it before this boot is confirmed */
    if (zyl_updater_on_trial(u)) return apply_failed(u, "The previous update is not confirmed yet");

    const ZylBootHal *boot = zyl_hal_boot();
    const char *inactive = strcmp(u->active_slot, "a") == 0 ? "b" : "a";
    fprintf(stderr, "[UPDATER] Installing v%s into slot %s\n", u->pending->version, inactive);

    /* An earlier install still waiting for its reboot is about to be overwritten */
    if (!boot->env_set("zyl_next_slot", NULL))
        return apply_failed(u, "Cannot write the bootloader environment");

    const char *why = NULL;
    if (!zyl_ota_install_slot(pkg_path, inactive[0], install_step, u, &why))
        return apply_failed(u, why);

    /* U-Boot tries the new slot zyl_boot_limit times, then falls back */
    report_progress(u, 80, "Setting boot flags...");
    if (!boot->env_set("zyl_boot_count", "0") || !boot->env_set("zyl_next_slot", inactive))
        return apply_failed(u, "Cannot select the updated slot");

    char ver_path[256];
    snprintf(ver_path, sizeof(ver_path), ZYL_STATE_DIR "/slot-%s-version", inactive);
    mkdir_p(ZYL_STATE_DIR);
    write_file_string(ver_path, u->pending->version);
    unlink(pkg_path);
    u->pkg_verified = false;

    report_progress(u, 100, "Update applied. Reboot to activate.");
    u->state = ZYL_UPDATE_PENDING_REBOOT;
    fprintf(stderr, "[UPDATER] Update applied to slot %s (v%s). Reboot required.\n",
            inactive, u->pending->version);
    return true;
}

bool zyl_updater_reboot_to_update(ZylUpdater *u) {
    if (!u || u->state != ZYL_UPDATE_PENDING_REBOOT) return false;

    fprintf(stderr, "[UPDATER] Initiating system reboot for update activation\n");

    /* Flush all dirty pages before rebooting */
    sync();

    /* Try systemd-logind Reboot() first (graceful, runs shutdown hooks) */
    {
        const char *argv[] = {
            "/usr/bin/systemctl", "reboot", NULL
        };
        pid_t pid;
        char *envp[] = { "PATH=/usr/bin:/sbin:/bin", NULL };
        int rc = posix_spawn(&pid, "/usr/bin/systemctl", NULL, NULL,
                              (char *const *)argv, envp);
        if (rc == 0) {
            int status;
            waitpid(pid, &status, 0);
            /* systemctl reboot typically does not return; if it returns successfully,
             * the reboot is already in progress. On non-zero exit, fall back. */
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return true;
            }
        }
    }

    /* Fallback: direct kernel reboot syscall */
    sync();
    reboot(RB_AUTOBOOT);

    /* If reboot() returns, treat as failure. */
    fprintf(stderr, "[UPDATER] reboot() returned unexpectedly: %s\n", strerror(errno));
    return false;
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

    /* 3. The try is over: this slot becomes the one U-Boot falls back to, and
          it stops counting boots (board/boot-policy.txt rolls back after zyl_boot_limit) */
    const ZylBootHal *boot = zyl_hal_boot();
    if (!boot->env_set("zyl_active_slot", u->active_slot) ||
        !boot->env_set("zyl_next_slot", NULL) || !boot->env_set("zyl_boot_count", "0")) {
        fprintf(stderr, "[UPDATER] fw_setenv could not confirm slot '%s'\n", u->active_slot);
        return false;
    }

    /* Read back by zyl_updater_get_partition_info */
    mkdir_p(ZYL_STATE_DIR);
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

    /* Tried like an update: if it does not come up, U-Boot returns here */
    if (!zyl_hal_boot()->env_set("zyl_boot_count", "0") ||
        !zyl_hal_boot()->env_set("zyl_next_slot", previous)) {
        fprintf(stderr, "[UPDATER] fw_setenv could not select slot '%s'\n", previous);
        u->state = ZYL_UPDATE_FAILED;
        return false;
    }

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
    if (!info) return NULL;
    info->active_slot = strdup(u->active_slot);
    info->inactive_slot = strdup(
        strcmp(u->active_slot, "a") == 0 ? "b" : "a");
    info->active_version = strdup(u->current_version);

    /* 비활성 슬롯 버전 */
    char ver_path[256];
    snprintf(ver_path, sizeof(ver_path),
             ZYL_STATE_DIR "/slot-%s-version", info->inactive_slot);
    info->inactive_version = read_file_string(ver_path);
    if (!info->inactive_version) info->inactive_version = strdup("(empty)");

    /* 검증 상태 */
    char *verified = read_file_string(VERIFY_FLAG_FILE);
    info->verified = verified && strcmp(verified, "1") == 0;
    free(verified);

    return info;
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
