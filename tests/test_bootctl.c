/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 부트로더 잠금 제어(bootctl) 단위 테스트
 * 수행범위: U-Boot 환경 없음 → 미지원, 허용 없이 잠금 해제 요청 거부,
 *           허용 후 요청은 "대기"로 보이고 부팅 후 반영, 재잠금은 허용 불필요,
 *           부팅 슬롯 판별, 부트 환경 파일과 변수 이름 일치
 * 의존방향: runtime/services/bootctl/bootctl.c (ZYL_TEST), 가짜 fw_printenv/fw_setenv
 * SOLID: SRP — 잠금 요청 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ZYL_CMDLINE "/tmp/zyl-bootctl-test/cmdline"
#include <assert.h>
#include <glib/gstdio.h>
#include "../runtime/services/bootctl/bootctl.c"

#define DIR_ "/tmp/zyl-bootctl-test"
static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void tool(const char *name, const char *body) {
    char *path = g_build_filename(DIR_, name, NULL);
    g_assert_true(g_file_set_contents(path, body, -1, NULL));
    g_chmod(path, 0755);
    g_free(path);
}

static void env_file(const char *text) {
    if (text) g_assert_true(g_file_set_contents(DIR_ "/env", text, -1, NULL));
    else g_unlink(DIR_ "/env");
}

static gboolean get_b(GVariant *st, const char *key) {
    GVariant *d = g_variant_get_child_value(st, 0);
    gboolean v = FALSE;
    g_variant_lookup(d, key, "b", &v);
    g_variant_unref(d);
    return v;
}

static GVariant *state(void) { return g_variant_ref_sink(state_variant()); }

static void test_no_uboot_env_means_unsupported(void) {
    env_file(NULL);
    GVariant *st = state();
    assert(!get_b(st, "supported") && !get_b(st, "unlocked"));
    g_variant_unref(st);
    assert(!request_lock_state(true));
}

static void test_unlock_needs_the_settings_permission(void) {
    env_file("zyl_unlocked=0\nzyl_lock_seen=0\nzyl_oem_unlock_allowed=0\n");
    assert(!request_lock_state(true));
    assert(env_is("zyl_unlocked", "0"));
}

static void test_request_is_pending_until_the_bootloader_confirms(void) {
    env_file("zyl_unlocked=0\nzyl_lock_seen=0\nzyl_oem_unlock_allowed=0\n");
    assert(env_set("zyl_oem_unlock_allowed", "1"));
    assert(request_lock_state(true));
    GVariant *st = state();
    assert(get_b(st, "supported") && get_b(st, "pending") && get_b(st, "unlockAllowed"));
    assert(!get_b(st, "unlocked"));             /* still running locked */
    g_variant_unref(st);
    /* U-Boot confirmed at the device and wiped: */
    env_file("zyl_unlocked=1\nzyl_lock_seen=1\nzyl_oem_unlock_allowed=0\n");
    st = state();
    assert(get_b(st, "unlocked") && !get_b(st, "pending"));
    g_variant_unref(st);
}

static void test_relock_never_needs_permission(void) {
    env_file("zyl_unlocked=1\nzyl_lock_seen=1\nzyl_oem_unlock_allowed=0\n");
    assert(request_lock_state(false));
    assert(env_is("zyl_unlocked", "0"));
}

static void test_booted_slot_comes_from_the_signed_cmdline(void) {
    g_file_set_contents(ZYL_CMDLINE, "root=/dev/dm-0 ro zyl.slot=b quiet\n", -1, NULL);
    char *slot = booted_slot();
    assert(strcmp(slot, "b") == 0);
    g_free(slot);
}

static void test_names_match_the_boot_environment(void) {
    char *env = NULL;
    g_assert_true(g_file_get_contents("board/bpi-f3/u-boot-env.txt", &env, NULL, NULL) ||
                  g_file_get_contents("../board/bpi-f3/u-boot-env.txt", &env, NULL, NULL) ||
                  g_file_get_contents("../../board/bpi-f3/u-boot-env.txt", &env, NULL, NULL));
    assert(strstr(env, "\nzyl_unlocked=0\n") && strstr(env, "\nzyl_lock_seen=0\n") &&
           strstr(env, "\nzyl_oem_unlock_allowed=0\n"));
    assert(strstr(env, "${zyl_oem_unlock_allowed}") && strstr(env, "${zyl_lock_seen}"));
    g_free(env);
}

int main(void) {
    printf("=== Zyl OS Boot Control Tests ===\n");
    g_assert_true(system("rm -rf " DIR_ " && mkdir -p " DIR_) == 0);
    tool("fw_printenv",
         "#!/bin/sh\n[ \"$1\" = -n ] || exit 2\n[ -f " DIR_ "/env ] || exit 1\n"
         "grep \"^$2=\" " DIR_ "/env | head -1 | cut -d= -f2- | grep . || exit 1\n");
    tool("fw_setenv",
         "#!/bin/sh\n[ -f " DIR_ "/env ] || exit 1\n"
         "grep -v \"^$1=\" " DIR_ "/env > " DIR_ "/env.new; [ $# -gt 1 ] && echo \"$1=$2\" >> " DIR_ "/env.new\n"
         "mv " DIR_ "/env.new " DIR_ "/env\n");
    char *path = g_strconcat(DIR_ ":", g_getenv("PATH"), NULL);
    g_setenv("PATH", path, TRUE);
    g_free(path);

    RUN_TEST(test_no_uboot_env_means_unsupported);
    RUN_TEST(test_unlock_needs_the_settings_permission);
    RUN_TEST(test_request_is_pending_until_the_bootloader_confirms);
    RUN_TEST(test_relock_never_needs_permission);
    RUN_TEST(test_booted_slot_comes_from_the_signed_cmdline);
    RUN_TEST(test_names_match_the_boot_environment);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    (void)bootctl_vtable; (void)bootctl_introspection_xml;
    return tests_pass == tests_run ? 0 : 1;
}
