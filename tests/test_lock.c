/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 화면 잠금 자격 증명(runtime/services/lock) 단위 테스트
 * 수행범위: PIN 은 해시로만 저장, 형식 검사, 실패 누적 대기(재시작 후 유지,
 *           대기 중 시도는 판정도 카운트도 안 함, 시도를 디스크에 못 세면 시도도
 *           없음), 성공 시 초기화, 변경·제거에
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

/* A specific 6-digit PIN: the stored line ends in 96 random hex characters, so
 * this exact sequence turns up inside them by chance about once in 180,000 runs
 * — a 4-digit PIN did so once in 725 and went red on CI with nothing wrong. */
#define PIN6 "482109"

static void test_pin_is_stored_only_as_a_salted_hash(void) {
    assert(*lock_set_pin(&s, "", PIN6, T) == '\0');
    char *file = NULL;
    g_assert_true(g_file_get_contents(ZYL_LOCK_FILE, &file, NULL, NULL));
    assert(!strstr(file, PIN6) && strstr(file, "pbkdf2-sha256$"));
    g_free(file);
    LockState other = {0};
    assert(hash_pin(PIN6, other.hash, sizeof(other.hash)));
    assert(strcmp(other.hash, s.hash) != 0);              /* fresh salt each time */
}

static void test_format_is_exactly_six_digits(void) {
    assert(strcmp(lock_set_pin(&s, "", "12345", T), "format") == 0);     /* too short */
    assert(strcmp(lock_set_pin(&s, "", "1234567", T), "format") == 0);   /* too long */
    assert(strcmp(lock_set_pin(&s, "", "4821", T), "format") == 0);      /* the old 4-digit */
    assert(strcmp(lock_set_pin(&s, "", "12a456", T), "format") == 0);    /* not all digits */
    assert(*lock_set_pin(&s, "", "123456", T) == '\0');
}

static void test_right_pin_unlocks_wrong_pin_does_not(void) {
    lock_set_pin(&s, "", "482109", T);
    assert(lock_check(&s, "000000", T) == CHECK_WRONG);
    assert(lock_check(&s, "482109", T) == CHECK_OK);
    assert(s.failures == 0);
}

static void test_failures_make_the_user_wait_and_survive_restart(void) {
    lock_set_pin(&s, "", "482109", T);
    for (int i = 0; i < 4; i++) assert(lock_check(&s, "000000", T) == CHECK_WRONG);
    assert(retry_after_ms(&s, T) == 0);                    /* four are free */
    assert(lock_check(&s, "000000", T) == CHECK_WRONG);
    assert(retry_after_ms(&s, T) == 30000);
    lock_load(&s);                                         /* reboot */
    assert(retry_after_ms(&s, T + 10) == 20000);
    /* While waiting even the right PIN is not tried — no oracle, no count */
    guint before = s.failures;
    assert(lock_check(&s, "482109", T + 10) == CHECK_WAIT);
    assert(s.failures == before);
    assert(lock_check(&s, "482109", T + 31) == CHECK_OK);
}

static void test_wait_doubles_and_is_capped(void) {
    assert(wait_for(4) == 0 && wait_for(5) == 30 && wait_for(9) == 30);
    assert(wait_for(10) == 60 && wait_for(15) == 120);
    assert(wait_for(200) == MAX_WAIT_S);
    s.wait_until = T + 10 * MAX_WAIT_S;                    /* clock was set back */
    assert(retry_after_ms(&s, T) == (guint64)MAX_WAIT_S * 1000);
}

static void test_change_and_remove_need_the_current_pin(void) {
    lock_set_pin(&s, "", "482109", T);
    assert(strcmp(lock_set_pin(&s, "111111", "555555", T), "wrong_pin") == 0);
    assert(*lock_set_pin(&s, "482109", "555555", T) == '\0');
    assert(lock_check(&s, "555555", T) == CHECK_OK);
    assert(strcmp(lock_set_pin(&s, "482109", "", T), "wrong_pin") == 0);
    assert(*lock_set_pin(&s, "555555", "", T) == '\0');      /* removed */
    assert(s.hash[0] == '\0');
}

/* A try is on disk before the PIN is looked at: with nowhere to write it
   (a full /data) there is no try — not even the right PIN — and nothing to
   reset by restarting. As root the test cannot make the file unwritable, so
   the state path is a directory where the file should be. */
static void test_no_try_without_counting_it_first(void) {
    assert(*lock_set_pin(&s, "", PIN6, T) == '\0');
    assert(lock_check(&s, "000000", T) == CHECK_WRONG);
    LockState kept = s;
    g_assert_true(system("rm -f " ZYL_LOCK_FILE " && mkdir " ZYL_LOCK_FILE) == 0);   /* cannot be written */
    assert(lock_check(&s, "000000", T) == CHECK_STORAGE);
    assert(lock_check(&s, PIN6, T) == CHECK_STORAGE);
    assert(s.failures == kept.failures);
    assert(strcmp(lock_set_pin(&s, PIN6, "", T), "storage") == 0);   /* nor removed */
    assert(s.hash[0]);
    g_assert_true(system("rmdir " ZYL_LOCK_FILE) == 0);
    assert(lock_check(&s, PIN6, T) == CHECK_OK);
}

/* A guess is counted even if the service dies before answering: the count is
   written before the PIN is compared */
static void test_a_guess_is_on_disk_before_it_is_judged(void) {
    assert(*lock_set_pin(&s, "", PIN6, T) == '\0');
    for (int i = 0; i < 3; i++) assert(lock_check(&s, "000000", T) == CHECK_WRONG);
    LockState disk;
    lock_load(&disk);
    assert(disk.failures == 3);
}

int main(void) {
    printf("=== Zyl OS Lock Credential Tests ===\n");
    RUN_TEST(test_pin_is_stored_only_as_a_salted_hash);
    RUN_TEST(test_format_is_exactly_six_digits);
    RUN_TEST(test_right_pin_unlocks_wrong_pin_does_not);
    RUN_TEST(test_failures_make_the_user_wait_and_survive_restart);
    RUN_TEST(test_wait_doubles_and_is_capped);
    RUN_TEST(test_change_and_remove_need_the_current_pin);
    RUN_TEST(test_no_try_without_counting_it_first);
    RUN_TEST(test_a_guess_is_on_disk_before_it_is_judged);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    (void)lock_vtable; (void)lock_introspection_xml;
    return tests_pass == tests_run ? 0 : 1;
}
