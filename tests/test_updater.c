/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: OTA 설치(runtime/services/updater) 단위 테스트
 * 수행범위: 서명이 맞는 패키지만 설치, 비활성 슬롯의 rootfs·verity 트리·FIT 만
 *           기록(실행 중인 슬롯은 그대로), 일부가 빠진 패키지는 부트로더를
 *           새 슬롯으로 돌리지 않음, 시험 부팅 중엔 되돌아갈 슬롯을 덮지 않음,
 *           전체 이미지만 허용, 지원 여부(키·U-Boot 환경), 시험 부팅 판정,
 *           U-Boot 변수 이름이 u-boot-env.txt 와 일치
 * 의존방향: runtime/services/updater/{updater,ota_verify,ota_install}.c,
 *           runtime/hal/hal_boot.c, 가짜 fw_printenv/fw_setenv,
 *           파일로 만든 가짜 파티션, tar, OpenSSL (테스트 키로 서명)
 * SOLID: SRP — 설치 경로 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#define DIR_ "/tmp/zyl-updater-test"
#define ZYL_STATE_DIR DIR_ "/state"
#define ZYL_BLOCK_DEV DIR_ "/dev/p"
#define ZYL_BOOT_DIR  DIR_ "/boot"
#define ZYL_OTA_KEY   DIR_ "/ota.pem"
/* One unit: the board's Boot HAL pointed at scratch partitions, and the updater */
#include "../runtime/hal/hal_boot.c"
#include "../runtime/lib/ota_verify.c"
#include "../runtime/services/updater/ota_install.c"
#include "../runtime/services/updater/updater.c"
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <assert.h>

#define PKG DIR_ "/cache/update.pkg"
static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-62s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static EVP_PKEY *key;
static ZylUpdater *u;

static void sh(const char *cmd) { assert(system(cmd) == 0); }
static void put(const char *path, const char *text) { assert(g_file_set_contents(path, text, -1, NULL)); }

/* Partitions are not truncated by a write, so only the start is compared */
static bool starts(const char *path, const char *want) {
    char *t = NULL;
    bool r = g_file_get_contents(path, &t, NULL, NULL) && g_str_has_prefix(t, want);
    g_free(t);
    return r;
}

static bool is(const char *path, const char *want) {
    char *t = NULL;
    bool r = g_file_get_contents(path, &t, NULL, NULL) && strcmp(t, want) == 0;
    g_free(t);
    return r;
}

static void tool(const char *name, const char *body) {
    char *path = g_strconcat(DIR_ "/bin/", name, NULL);
    char *text = g_strconcat("#!/bin/sh\n", body, NULL);
    put(path, text);
    assert(chmod(path, 0755) == 0);
    g_free(text);
    g_free(path);
}

/* What tools/build-ota.sh does: RSA-SHA256 over the package's SHA-256, base64 */
static char *sign(const char *path) {
    char *data = NULL;
    gsize len = 0;
    assert(g_file_get_contents(path, &data, &len, NULL));
    unsigned char h[SHA256_DIGEST_LENGTH], sig[512];
    SHA256((unsigned char *)data, len, h);
    g_free(data);
    size_t sig_len = sizeof(sig);
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    assert(EVP_DigestSignInit(c, NULL, EVP_sha256(), NULL, key) == 1);
    assert(EVP_DigestSign(c, sig, &sig_len, h, sizeof(h)) == 1);
    EVP_MD_CTX_free(c);
    char *b64 = malloc(4 * ((sig_len + 2) / 3) + 1);
    EVP_EncodeBlock((unsigned char *)b64, sig, (int)sig_len);
    return b64;
}

static void pack(const char *members) {
    char *cmd = g_strconcat("cd " DIR_ "/pkg && tar -czf " PKG " ", members, NULL);
    sh(cmd);
    g_free(cmd);
}

static void offer(const char *type, bool signed_) {
    ZylUpdateManifest *m = calloc(1, sizeof(*m));
    m->version = strdup("0.2.0");
    m->type = parse_update_type(type);
    if (signed_) m->signature = sign(PKG);
    if (u->pending) zyl_update_manifest_free(u->pending);
    u->pending = m;
    u->state = ZYL_UPDATE_AVAILABLE;
}

static void reset(void) {
    sh("rm -rf " DIR_ " && mkdir -p " DIR_ "/bin " DIR_ "/dev " DIR_ "/boot " DIR_ "/pkg " DIR_ "/cache");
    FILE *f = fopen(ZYL_OTA_KEY, "w");
    assert(f && PEM_write_PUBKEY(f, key) == 1);
    fclose(f);
    /* Running slot a, and slot b as the last install left it */
    put(DIR_ "/dev/p2", "old rootfs a");
    put(DIR_ "/dev/p3", "old tree a");
    put(DIR_ "/dev/p4", "old rootfs b");
    put(DIR_ "/dev/p5", "old tree b");
    put(DIR_ "/boot/fit-image-a.itb", "old fit a");
    put(DIR_ "/boot/fit-image-b.itb", "old fit b");
    put(DIR_ "/pkg/rootfs.img", "new rootfs");
    put(DIR_ "/pkg/verity-hashtree-a.img", "new tree a");
    put(DIR_ "/pkg/verity-hashtree-b.img", "new tree b");
    put(DIR_ "/pkg/fit-image-a.itb", "new fit a");
    put(DIR_ "/pkg/fit-image-b.itb", "new fit b");
    tool("fw_setenv", "echo \"$*\" >> " DIR_ "/env.log\n");
    tool("fw_printenv", "[ \"$2\" = zyl_next_slot ] && [ -s " DIR_ "/next ] && exec cat " DIR_ "/next\nexit 1\n");
    if (u) zyl_updater_destroy(u);
    u = zyl_updater_create(NULL, DIR_ "/cache");
    free(u->active_slot);
    u->active_slot = strdup("a");
}

#define ALL "rootfs.img verity-hashtree-a.img verity-hashtree-b.img fit-image-a.itb fit-image-b.itb"

static void running_slot_untouched(void) {
    assert(starts(DIR_ "/dev/p2", "old rootfs a"));
    assert(starts(DIR_ "/dev/p3", "old tree a"));
    assert(is(DIR_ "/boot/fit-image-a.itb", "old fit a"));
}

static void test_signed_image_installs_into_the_inactive_slot_only(void) {
    pack(ALL);
    offer("full", true);
    assert(verify_package(u, PKG));
    assert(zyl_updater_apply(u, NULL, NULL));
    assert(starts(DIR_ "/dev/p4", "new rootfs"));
    assert(starts(DIR_ "/dev/p5", "new tree b"));
    assert(is(DIR_ "/boot/fit-image-b.itb", "new fit b"));
    running_slot_untouched();
    /* Pending install cancelled first, then U-Boot told to try b */
    assert(is(DIR_ "/env.log", "zyl_next_slot\nzyl_boot_count 0\nzyl_next_slot b\n"));
    assert(u->state == ZYL_UPDATE_PENDING_REBOOT);
    assert(!g_file_test(DIR_ "/boot/.fit-image-b.itb.new", G_FILE_TEST_EXISTS));
    assert(!g_file_test(PKG, G_FILE_TEST_EXISTS));
    assert(is(DIR_ "/state/slot-b-version", "0.2.0"));
}

static void test_only_a_package_whose_signature_verifies_is_installed(void) {
    pack(ALL);
    offer("full", true);
    sh("printf x >> " PKG);                                 /* changed after signing */
    assert(!verify_package(u, PKG));
    assert(!zyl_updater_apply(u, NULL, NULL));
    reset();
    pack(ALL);
    offer("full", false);
    assert(!verify_package(u, PKG));
    assert(!zyl_updater_apply(u, NULL, NULL));              /* never verified */
    assert(starts(DIR_ "/dev/p4", "old rootfs b"));
    assert(!g_file_test(DIR_ "/env.log", G_FILE_TEST_EXISTS));
}

static void test_a_missing_part_never_points_the_bootloader_at_the_slot(void) {
    pack("rootfs.img fit-image-a.itb fit-image-b.itb");
    offer("full", true);
    assert(verify_package(u, PKG));
    assert(!zyl_updater_apply(u, NULL, NULL));
    assert(u->state == ZYL_UPDATE_FAILED);
    assert(is(DIR_ "/boot/fit-image-b.itb", "old fit b"));
    assert(is(DIR_ "/env.log", "zyl_next_slot\n"));
    running_slot_untouched();
}

static void test_the_fallback_slot_is_kept_while_this_boot_is_on_trial(void) {
    put(DIR_ "/next", "a");                                 /* running a as an update try */
    pack(ALL);
    offer("full", true);
    assert(verify_package(u, PKG));
    assert(!zyl_updater_apply(u, NULL, NULL));
    assert(starts(DIR_ "/dev/p4", "old rootfs b"));
    assert(!g_file_test(DIR_ "/env.log", G_FILE_TEST_EXISTS));
}

static void test_only_full_images_are_installed(void) {
    pack(ALL);
    const char *types[] = {"delta", "apps_only", "kernel"};
    for (int i = 0; i < 3; i++) {
        offer(types[i], true);
        assert(verify_package(u, PKG));
        assert(!zyl_updater_apply(u, NULL, NULL));
    }
    assert(starts(DIR_ "/dev/p4", "old rootfs b"));
}

static void test_rollback_is_tried_like_an_update(void) {
    assert(zyl_updater_rollback(u));
    assert(is(DIR_ "/env.log", "zyl_boot_count 0\nzyl_next_slot b\n"));
}

static void test_supported_needs_the_key_and_a_bootloader_environment(void) {
    assert(!zyl_updater_supported(u));                     /* fake fw_printenv: no zyl_active_slot */
    tool("fw_printenv", "[ \"$2\" = zyl_active_slot ] && exec echo a\nexit 1\n");
    assert(zyl_updater_supported(u));
    unlink(ZYL_OTA_KEY);
    assert(!zyl_updater_supported(u));
}

static void test_on_trial_only_while_u_boot_is_trying_this_slot(void) {
    assert(!zyl_updater_on_trial(u));
    put(DIR_ "/next", "b");                                 /* b installed, not booted yet */
    assert(!zyl_updater_on_trial(u));
    put(DIR_ "/next", "a");
    assert(zyl_updater_on_trial(u));
}

static void test_variables_are_the_ones_u_boot_reads(void) {
    char *env = NULL;
    assert(g_file_get_contents("board/bpi-f3/u-boot-env.txt", &env, NULL, NULL));
    assert(strstr(env, "\nzyl_next_slot=") && strstr(env, "\nzyl_boot_count=") &&
           strstr(env, "\nzyl_active_slot=") && strstr(env, "fit-image-${zyl_slot}.itb"));
    g_free(env);
}

int main(void) {
    key = EVP_RSA_gen(2048);
    assert(key);
    char *path = g_strconcat(DIR_ "/bin:", g_getenv("PATH"), NULL);
    g_setenv("PATH", path, TRUE);
    g_free(path);
    printf("=== OTA install ===\n");
    RUN_TEST(test_signed_image_installs_into_the_inactive_slot_only);
    RUN_TEST(test_only_a_package_whose_signature_verifies_is_installed);
    RUN_TEST(test_a_missing_part_never_points_the_bootloader_at_the_slot);
    RUN_TEST(test_the_fallback_slot_is_kept_while_this_boot_is_on_trial);
    RUN_TEST(test_only_full_images_are_installed);
    RUN_TEST(test_rollback_is_tried_like_an_update);
    RUN_TEST(test_supported_needs_the_key_and_a_bootloader_environment);
    RUN_TEST(test_on_trial_only_while_u_boot_is_trying_this_slot);
    RUN_TEST(test_variables_are_the_ones_u_boot_reads);
    zyl_updater_destroy(u);
    EVP_PKEY_free(key);
    sh("rm -rf " DIR_);
    printf("Results: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
