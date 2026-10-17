/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: OTA 설치(runtime/services/updater) 단위 테스트
 * 수행범위: 서명이 맞는 패키지만 설치, 비활성 슬롯의 rootfs·verity 트리·FIT 만
 *           기록(실행 중인 슬롯은 그대로), 일부가 빠진 패키지는 부트로더를
 *           새 슬롯으로 돌리지 않음, 시험 부팅 중엔 되돌아갈 슬롯을 덮지 않음,
 *           전체 이미지만 허용, 지원 여부(키·U-Boot 환경), 시험 부팅 판정,
 *           U-Boot 변수 이름이 부트 정책(board/boot-policy.txt)과 일치, 폴백 슬롯은
 *           확정 때 이전 슬롯·쓰기 전에 지움, 확정된 슬롯이 서면 부팅 횟수를 지움
 * 의존방향: runtime/services/updater/{updater,ota_verify,ota_install}.c,
 *           runtime/hal/hal_boot.c, 가짜 fw_printenv/fw_setenv,
 *           파일로 만든 가짜 파티션, tar, OpenSSL (테스트 키로 서명)
 * SOLID: SRP — 설치 경로 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#define DIR_ "/tmp/zyl-updater-test"
#define ZYL_STATE_DIR DIR_ "/state"
#define ZYL_SLOT_A_ROOTFS DIR_ "/dev/p2"   /* as runtime/hal/meson.build passes a layout */
#define ZYL_SLOT_A_VERITY DIR_ "/dev/p3"
#define ZYL_SLOT_B_ROOTFS DIR_ "/dev/p4"
#define ZYL_SLOT_B_VERITY DIR_ "/dev/p5"
#define ZYL_BOOT_DIR  DIR_ "/boot"
#define ZYL_OTA_KEY   DIR_ "/ota.pem"
#define ZYL_CMDLINE   DIR_ "/cmdline"
#define ZYL_SYSTEMCTL DIR_ "/bin/systemctl"
#define ZYL_CURL      DIR_ "/bin/curl"
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
    /* zyl_next_slot from DIR_/next, any other variable from DIR_/env-<name> */
    tool("fw_printenv", "f=" DIR_ "/env-$2; [ \"$2\" = zyl_next_slot ] && f=" DIR_ "/next\n"
                        "[ -s $f ] && exec cat $f\nexit 1\n");
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
    /* Pending install cancelled and the fallback forgotten (b held it) first,
       then U-Boot told to try b */
    assert(is(DIR_ "/env.log", "zyl_next_slot\nzyl_fallback_slot\nzyl_boot_count 0\nzyl_next_slot b\n"));
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
    assert(is(DIR_ "/env.log", "zyl_next_slot\nzyl_fallback_slot\n"));
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

/* The slot that booted comes from the signed kernel arguments, read whole: the
   verity values ahead of zyl.slot= make the line longer than a fixed buffer */
static void test_the_booted_slot_is_read_from_a_long_signed_command_line(void) {
    GString *line = g_string_new("root=/dev/mapper/zyl-root ro zyl.verity=/dev/vda4,/dev/vda5,786432,4096,4096,1,");
    for (int i = 0; i < 128; i++) g_string_append_c(line, "0123456789abcdef"[i % 16]);
    g_string_append(line, ",");
    for (int i = 0; i < 64; i++) g_string_append_c(line, 'f');
    g_string_append(line, " panic=10 console=ttyS0,115200 zyl.slot=b quiet\n");
    assert(line->len > 300);
    put(ZYL_CMDLINE, line->str);
    g_string_free(line, TRUE);
    ZylUpdater *v = zyl_updater_create(NULL, DIR_ "/cache");
    assert(v);
    put(DIR_ "/next", "b");                                 /* U-Boot is trying b */
    assert(zyl_updater_on_trial(v));
    zyl_updater_destroy(v);
}

/* Confirming a try makes the slot it replaced the one to fall back to — last,
   so a write that does not land leaves no fallback rather than a wrong one */
static void test_a_confirmed_update_keeps_the_slot_it_replaced(void) {
    put(DIR_ "/next", "a");                                 /* a was being tried */
    assert(zyl_updater_mark_verified(u));
    assert(is(DIR_ "/env.log", "zyl_active_slot a\nzyl_next_slot\nzyl_boot_count 0\nzyl_fallback_slot b\n"));
}

/* A confirmed slot that came up clears what U-Boot counted; a try does not */
static void test_a_confirmed_slot_that_came_up_clears_the_count(void) {
    put(DIR_ "/env-zyl_boot_count", "2");
    assert(zyl_updater_mark_booted(u));
    assert(is(DIR_ "/env.log", "zyl_boot_count 0\n"));
    put(DIR_ "/env-zyl_boot_count", "0");
    unlink(DIR_ "/env.log");
    assert(zyl_updater_mark_booted(u));                    /* nothing to clear, nothing written */
    assert(!g_file_test(DIR_ "/env.log", G_FILE_TEST_EXISTS));
    put(DIR_ "/env-zyl_boot_count", "2");
    put(DIR_ "/next", "a");                                 /* on trial: the health check's */
    assert(!zyl_updater_mark_booted(u));
    assert(!g_file_test(DIR_ "/env.log", G_FILE_TEST_EXISTS));
}

static void test_variables_are_the_ones_u_boot_reads(void) {
    char *env = NULL;
    assert(g_file_get_contents("board/boot-policy.txt", &env, NULL, NULL));
    assert(strstr(env, "\nzyl_next_slot=") && strstr(env, "\nzyl_boot_count=") &&
           strstr(env, "\nzyl_active_slot=") && strstr(env, "\nzyl_fallback_slot=") &&
           strstr(env, "fit-image-${zyl_slot}.itb"));
    g_free(env);
}

/* systemctl show for the four core units: state and run of each */
static void units(const char *wam_state, const char *wam_run) {
    char *t = g_strdup_printf(
        "ActiveState=active\nInvocationID=c1\n\nActiveState=%s\nInvocationID=%s\n\n"
        "ActiveState=active\nInvocationID=l1\n\nActiveState=active\nInvocationID=s1\n",
        wam_state, wam_run);
    put(DIR_ "/units", t);
    g_free(t);
}

/* One is-active at 60 s confirmed a WAM crashing every few seconds most of the
   time — a black screen U-Boot would never leave */
static void test_healthy_is_every_core_unit_up_in_the_same_run(void) {
    tool("systemctl", "cat " DIR_ "/units\n");
    units("active", "w1");
    char *a = zyl_updater_health_sample(), *b = zyl_updater_health_sample();
    assert(a && zyl_updater_health_stable(a, b));
    units("active", "w2");                                  /* restarted in between */
    char *c = zyl_updater_health_sample();
    assert(!zyl_updater_health_stable(b, c));
    units("activating", "w3");                              /* still coming up */
    char *d = zyl_updater_health_sample(), *e = zyl_updater_health_sample();
    assert(!zyl_updater_health_stable(d, e));
    units("active", "");                                    /* never ran */
    char *f = zyl_updater_health_sample();
    assert(!zyl_updater_health_stable(f, f));
    put(DIR_ "/units", "ActiveState=active\nInvocationID=c1\n");   /* one unit only */
    char *g = zyl_updater_health_sample();
    assert(!zyl_updater_health_stable(g, g));
    assert(!zyl_updater_health_stable(NULL, a));            /* the first look */
    tool("systemctl", "exit 1\n");
    assert(zyl_updater_health_sample() == NULL);
    g_free(a); g_free(b); g_free(c); g_free(d); g_free(e); g_free(f); g_free(g);
}

/* A server or mirror sending without end filled /data, and the part stayed */
static void test_a_download_is_bounded_and_a_failed_one_leaves_nothing(void) {
    /* curl writes what it was given as arguments, a part of a file, and fails
       as it does past --max-filesize */
    tool("curl", "echo \"$@\" > " DIR_ "/curl-args; out=''\n"
                 "while [ $# -gt 0 ]; do [ \"$1\" = -o ] && out=$2; shift; done\n"
                 "echo part > \"$out\"; exit 63\n");
    offer("full", false);
    u->pending->download_url = strdup("https://update.zyl-os.com/p.pkg");
    u->pending->download_size = 4096;
    assert(!zyl_updater_download(u, NULL, NULL));
    assert(access(PKG, F_OK) != 0);                         /* the part is gone */
    char *args = NULL;
    assert(g_file_get_contents(DIR_ "/curl-args", &args, NULL, NULL));
    assert(strstr(args, "--proto =https --proto-redir =https") && strstr(args, "--max-filesize 4096"));
    g_free(args);
    /* No room for it: not even started */
    unlink(DIR_ "/curl-args");
    offer("full", false);
    u->pending->download_url = strdup("https://update.zyl-os.com/p.pkg");
    u->pending->download_size = (size_t)1 << 62;
    assert(!zyl_updater_download(u, NULL, NULL));
    assert(access(DIR_ "/curl-args", F_OK) != 0);
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
    RUN_TEST(test_the_booted_slot_is_read_from_a_long_signed_command_line);
    RUN_TEST(test_a_confirmed_update_keeps_the_slot_it_replaced);
    RUN_TEST(test_a_confirmed_slot_that_came_up_clears_the_count);
    RUN_TEST(test_variables_are_the_ones_u_boot_reads);
    RUN_TEST(test_healthy_is_every_core_unit_up_in_the_same_run);
    RUN_TEST(test_a_download_is_bounded_and_a_failed_one_leaves_nothing);
    zyl_updater_destroy(u);
    EVP_PKEY_free(key);
    sh("rm -rf " DIR_);
    printf("Results: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
