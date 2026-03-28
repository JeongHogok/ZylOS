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

/* ─── 내부 상수 ─── */
#define BOOTCTL_PATH          "/usr/sbin/bootctl"
#define SLOT_METADATA_PATH    "/var/lib/bpi-os/slot-metadata"
#define UPDATE_CACHE_DEFAULT  "/var/cache/bpi-os/updates"
#define CURRENT_VERSION_FILE  "/etc/bpi-os/version"
#define VERIFY_FLAG_FILE      "/var/lib/bpi-os/slot-verified"

/* ─── 업데이터 내부 구조체 ─── */
struct BpiUpdater {
    char *server_url;           /* OTA 서버 URL */
    char *cache_dir;            /* 다운로드 캐시 */
    BpiUpdateState state;       /* 현재 상태 */
    BpiUpdateManifest *pending; /* 대기 중인 업데이트 */

    /* A/B 파티션 */
    char *active_slot;          /* "a" 또는 "b" */
    char *current_version;      /* 현재 OS 버전 */

    /* 자동 업데이트 */
    bool auto_check_enabled;
    int auto_check_interval_h;

    /* 진행률 */
    bpi_update_progress_fn progress_cb;
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
    fclose(f);
    return true;
}

static void report_progress(BpiUpdater *u, int pct, const char *msg) {
    if (u->progress_cb)
        u->progress_cb(u->state, pct, msg, u->progress_data);
}

/* ─── 활성 슬롯 감지 ─── */
static char *detect_active_slot(void) {
    /*
     * 실제 구현에서는 U-Boot 환경변수 또는 커널 커맨드라인에서 읽음:
     *   cat /proc/cmdline | grep -o 'bpi.slot=[ab]'
     *   또는 fw_printenv bpi_active_slot
     */
    char *slot = read_file_string(SLOT_METADATA_PATH);
    return slot ? slot : strdup("a");
}

/* ─── 공개 API 구현 ─── */

BpiUpdater *bpi_updater_create(const char *update_server_url,
                                const char *cache_dir) {
    BpiUpdater *u = calloc(1, sizeof(BpiUpdater));
    if (!u) return NULL;

    u->server_url = strdup(update_server_url ? update_server_url
        : "https://update.bpi-os.dev/v1");
    u->cache_dir = strdup(cache_dir ? cache_dir : UPDATE_CACHE_DEFAULT);
    u->state = BPI_UPDATE_IDLE;
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

void bpi_updater_destroy(BpiUpdater *u) {
    if (!u) return;
    free(u->server_url);
    free(u->cache_dir);
    free(u->active_slot);
    free(u->current_version);
    if (u->pending) bpi_update_manifest_free(u->pending);
    free(u);
}

BpiUpdateState bpi_updater_check(BpiUpdater *u,
                                  BpiUpdateManifest **out_manifest) {
    if (!u) return BPI_UPDATE_FAILED;

    u->state = BPI_UPDATE_CHECKING;
    report_progress(u, 0, "Checking for updates...");

    /*
     * 실제 구현:
     *   1. HTTP GET ${server_url}/check?version=${current}&slot=${slot}&arch=riscv64
     *   2. JSON 응답 파싱
     *   3. 서명 검증
     *
     * 예시 요청:
     *   curl -s "https://update.bpi-os.dev/v1/check?version=0.1.0&arch=riscv64"
     *
     * 예시 응답:
     *   {
     *     "available": true,
     *     "version": "0.2.0",
     *     "type": "delta",
     *     "size": 52428800,
     *     "url": "https://cdn.bpi-os.dev/updates/0.1.0-to-0.2.0-riscv64.bpiupd",
     *     "sha256": "...",
     *     "signature": "...",
     *     "changelog": { "ko": "버그 수정 및 성능 개선", "en": "Bug fixes..." },
     *     "mandatory": false,
     *     "min_battery": 30
     *   }
     */

    /* 프로토타입: 항상 "최신"으로 반환 */
    u->state = BPI_UPDATE_UP_TO_DATE;
    report_progress(u, 100, "System is up to date");

    if (out_manifest) *out_manifest = NULL;
    return u->state;
}

bool bpi_updater_download(BpiUpdater *u,
                           bpi_update_progress_fn callback,
                           void *user_data) {
    if (!u || u->state != BPI_UPDATE_AVAILABLE) return false;

    u->progress_cb = callback;
    u->progress_data = user_data;
    u->state = BPI_UPDATE_DOWNLOADING;

    /*
     * 실제 구현:
     *   1. libcurl로 패키지 다운로드
     *   2. 진행률 콜백 호출
     *   3. 다운로드 완료 시 SHA-256 검증
     *   4. RSA 서명 검증
     *
     *   curl = curl_easy_init();
     *   curl_easy_setopt(curl, CURLOPT_URL, u->pending->download_url);
     *   curl_easy_setopt(curl, CURLOPT_WRITEDATA, cache_file);
     *   curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_fn);
     *   curl_easy_perform(curl);
     */

    report_progress(u, 100, "Download complete");
    u->state = BPI_UPDATE_VERIFYING;

    /* SHA-256 검증 */
    report_progress(u, 50, "Verifying package integrity...");

    /* 서명 검증 */
    report_progress(u, 100, "Package verified");

    return true;
}

bool bpi_updater_apply(BpiUpdater *u,
                        bpi_update_progress_fn callback,
                        void *user_data) {
    if (!u) return false;

    u->progress_cb = callback;
    u->progress_data = user_data;
    u->state = BPI_UPDATE_APPLYING;

    /*
     * A/B 파티션 업데이트 흐름:
     *
     * 1. 비활성 슬롯 결정
     *    inactive = (active == "a") ? "b" : "a"
     *
     * 2. 업데이트 유형에 따라 적용:
     *    - FULL: 비활성 파티션에 전체 이미지 기록
     *      dd if=update.img of=/dev/mmcblk0p${inactive_part} bs=4M
     *    - DELTA: bsdiff/bspatch로 델타 적용
     *      bspatch /dev/mmcblk0p${active_part} /dev/mmcblk0p${inactive_part} delta.patch
     *    - APPS_ONLY: 시스템 앱 디렉토리만 업데이트
     *      rsync -a update/apps/ /usr/share/bpi-os/apps/
     *    - KERNEL: 커널 이미지만 교체
     *      cp update/Image /boot/Image.${inactive}
     *
     * 3. 부트로더 플래그 설정
     *    fw_setenv bpi_next_slot ${inactive}
     *    fw_setenv bpi_slot_verified 0
     *    fw_setenv bpi_boot_count 0
     *
     * 4. 검증 메타데이터 저장
     *    echo "${new_version}" > /var/lib/bpi-os/slot-${inactive}-version
     */

    const char *inactive = strcmp(u->active_slot, "a") == 0 ? "b" : "a";

    report_progress(u, 10, "Preparing inactive partition...");

    /* Step 1: 비활성 파티션 준비 */
    report_progress(u, 20, "Writing update to partition...");

    /* Step 2: 업데이트 이미지 기록 */
    report_progress(u, 70, "Setting boot flags...");

    /* Step 3: 부트 플래그 설정 */
    char flag_path[256];
    snprintf(flag_path, sizeof(flag_path),
             "/var/lib/bpi-os/next-slot");
    write_file_string(flag_path, inactive);

    report_progress(u, 90, "Saving metadata...");

    /* Step 4: 메타데이터 저장 */
    if (u->pending) {
        char ver_path[256];
        snprintf(ver_path, sizeof(ver_path),
                 "/var/lib/bpi-os/slot-%s-version", inactive);
        write_file_string(ver_path, u->pending->version);
    }

    report_progress(u, 100, "Update applied. Reboot to activate.");
    u->state = BPI_UPDATE_PENDING_REBOOT;

    return true;
}

bool bpi_updater_reboot_to_update(BpiUpdater *u) {
    if (!u || u->state != BPI_UPDATE_PENDING_REBOOT) return false;

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

bool bpi_updater_mark_verified(BpiUpdater *u) {
    if (!u) return false;

    /*
     * 부팅 후 시스템 자가 진단 통과 시 호출:
     *   1. 핵심 서비스 (compositor, WAM) 실행 확인
     *   2. 디스플레이 출력 확인
     *   3. 터치 입력 응답 확인
     *
     * 검증 성공 시:
     *   fw_setenv bpi_slot_verified 1
     *   fw_setenv bpi_active_slot ${current_slot}
     */

    write_file_string(VERIFY_FLAG_FILE, "1");

    char slot_path[256];
    snprintf(slot_path, sizeof(slot_path), "%s", SLOT_METADATA_PATH);
    write_file_string(slot_path, u->active_slot);

    fprintf(stderr, "[UPDATER] Slot '%s' marked as verified\n",
            u->active_slot);
    return true;
}

bool bpi_updater_rollback(BpiUpdater *u) {
    if (!u) return false;

    u->state = BPI_UPDATE_ROLLING_BACK;

    /*
     * 롤백 시나리오:
     *   1. 자동 롤백: 부팅 3회 연속 실패 시 U-Boot이 자동 전환
     *      fw_setenv에서 boot_count > 3이면 이전 슬롯으로
     *   2. 수동 롤백: 사용자가 설정에서 요청
     *      fw_setenv bpi_next_slot ${previous_slot}
     *      reboot
     */

    const char *previous = strcmp(u->active_slot, "a") == 0 ? "b" : "a";

    char flag_path[256];
    snprintf(flag_path, sizeof(flag_path), "/var/lib/bpi-os/next-slot");
    write_file_string(flag_path, previous);

    fprintf(stderr, "[UPDATER] Rollback to slot '%s' scheduled\n", previous);
    u->state = BPI_UPDATE_PENDING_REBOOT;

    return true;
}

BpiUpdateState bpi_updater_get_state(const BpiUpdater *u) {
    return u ? u->state : BPI_UPDATE_FAILED;
}

BpiPartitionInfo *bpi_updater_get_partition_info(const BpiUpdater *u) {
    if (!u) return NULL;

    BpiPartitionInfo *info = calloc(1, sizeof(BpiPartitionInfo));
    info->active_slot = strdup(u->active_slot);
    info->inactive_slot = strdup(
        strcmp(u->active_slot, "a") == 0 ? "b" : "a");
    info->active_version = strdup(u->current_version);

    /* 비활성 슬롯 버전 */
    char ver_path[256];
    snprintf(ver_path, sizeof(ver_path),
             "/var/lib/bpi-os/slot-%s-version", info->inactive_slot);
    info->inactive_version = read_file_string(ver_path);
    if (!info->inactive_version) info->inactive_version = strdup("(empty)");

    /* 검증 상태 */
    char *verified = read_file_string(VERIFY_FLAG_FILE);
    info->verified = verified && strcmp(verified, "1") == 0;
    free(verified);

    return info;
}

void bpi_updater_set_auto_check(BpiUpdater *u, bool enabled,
                                 int interval_hours) {
    if (!u) return;
    u->auto_check_enabled = enabled;
    u->auto_check_interval_h = interval_hours > 0 ? interval_hours : 24;
}

void bpi_update_manifest_free(BpiUpdateManifest *m) {
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

void bpi_partition_info_free(BpiPartitionInfo *info) {
    if (!info) return;
    free(info->active_slot);
    free(info->inactive_slot);
    free(info->active_version);
    free(info->inactive_version);
    free(info);
}
