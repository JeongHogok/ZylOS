/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 화면 잠금 자격 증명(runtime/services/lock) 단위 테스트
 * 수행범위: PIN 은 해시로만 저장, 형식 검사, 실패 누적 대기(재시작 후 유지,
 *           대기 중 시도는 판정도 카운트도 안 함), 성공 시 초기화, 변경·제거에
 *           현재 PIN 필요, 시계가 뒤로 가도 대기 상한
 * 의존방향: runtime/services/lock/lock.c (ZYL_TEST), gio, OpenSSL
 * SOLID: SRP — 잠금 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-lock-test"
#define ZYL_LOCK_FILE DIR_ "/state"
#include <assert.h>
#include "../runtime/services/lock/lock.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static LockState s;
static const gint64 T = 1800000000;

static void reset(void) {
    g_assert_true(system("rm -rf " DIR_) == 0);
    lock_load(&s);
}

/* A long PIN on purpose: the stored line ends in 96 random hex characters, and
   a 4-digit PIN turns up inside them by chance about once in 725 runs — this
   test went red on CI for exactly that, with nothing wrong in the code. */
#define LONG_PIN "4821096372"

static void test_pin_is_stored_only_as_a_salted_hash(void) {
    assert(*lock_set_pin(&s, "", LONG_PIN, T) == '\0');
    char *file = NULL;
    g_assert_true(g_file_get_contents(ZYL_LOCK_FILE, &file, NULL, NULL));
    assert(!strstr(file, LONG_PIN) && strstr(file, "pbkdf2-sha256$"));
    g_free(file);
    LockState other = {0};
    assert(hash_pin(LONG_PIN, other.hash, sizeof(other.hash)));
    assert(strcmp(other.hash, s.hash) != 0);              /* fresh salt each time */
}

static void test_format_is_digits_four_to_sixteen(void) {
    assert(strcmp(lock_set_pin(&s, "", "123", T), "format") == 0);
    assert(strcmp(lock_set_pin(&s, "", "12a4", T), "format") == 0);
    assert(*lock_set_pin(&s, "", "1234567890123456", T) == '\0');
}

static void test_right_pin_unlocks_wrong_pin_does_not(void) {
    lock_set_pin(&s, "", "4821", T);
    assert(lock_check(&s, "0000", T) == CHECK_WRONG);
    assert(lock_check(&s, "4821", T) == CHECK_OK);
    assert(s.failures == 0);
}

static void test_failures_make_the_user_wait_and_survive_restart(void) {
    lock_set_pin(&s, "", "4821", T);
    for (int i = 0; i < 4; i++) assert(lock_check(&s, "0000", T) == CHECK_WRONG);
    assert(retry_after_ms(&s, T) == 0);                    /* four are free */
    assert(lock_check(&s, "0000", T) == CHECK_WRONG);
    assert(retry_after_ms(&s, T) == 30000);
    lock_load(&s);                                         /* reboot */
    assert(retry_after_ms(&s, T + 10) == 20000);
    /* While waiting even the right PIN is not tried — no oracle, no count */
    guint before = s.failures;
    assert(lock_check(&s, "4821", T + 10) == CHECK_WAIT);
    assert(s.failures == before);
    assert(lock_check(&s, "4821", T + 31) == CHECK_OK);
}

static void test_wait_doubles_and_is_capped(void) {
    assert(wait_for(4) == 0 && wait_for(5) == 30 && wait_for(9) == 30);
    assert(wait_for(10) == 60 && wait_for(15) == 120);
    assert(wait_for(200) == MAX_WAIT_S);
    s.wait_until = T + 10 * MAX_WAIT_S;                    /* clock was set back */
    assert(retry_after_ms(&s, T) == (guint64)MAX_WAIT_S * 1000);
}

static void test_change_and_remove_need_the_current_pin(void) {
    lock_set_pin(&s, "", "4821", T);
    assert(strcmp(lock_set_pin(&s, "1111", "5555", T), "wrong_pin") == 0);
    assert(*lock_set_pin(&s, "4821", "5555", T) == '\0');
    assert(lock_check(&s, "5555", T) == CHECK_OK);
    assert(strcmp(lock_set_pin(&s, "4821", "", T), "wrong_pin") == 0);
    assert(*lock_set_pin(&s, "5555", "", T) == '\0');      /* removed */
    assert(s.hash[0] == '\0');
}

int main(void) {
    printf("=== Zyl OS Lock Credential Tests ===\n");
    RUN_TEST(test_pin_is_stored_only_as_a_salted_hash);
    RUN_TEST(test_format_is_digits_four_to_sixteen);
    RUN_TEST(test_right_pin_unlocks_wrong_pin_does_not);
    RUN_TEST(test_failures_make_the_user_wait_and_survive_restart);
    RUN_TEST(test_wait_doubles_and_is_capped);
    RUN_TEST(test_change_and_remove_need_the_current_pin);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    (void)lock_vtable; (void)lock_introspection_xml;
    return tests_pass == tests_run ? 0 : 1;
}
