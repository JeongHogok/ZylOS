/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 화면 잠금 자격 증명(runtime/services/lock) 단위 테스트
 * 수행범위: PIN 은 해시로만 저장, 형식 검사, 실패 누적 대기(재시작 후 유지,
 *           대기 중 시도는 판정도 카운트도 안 함, 시도를 디스크에 못 세면 시도도
 *           없음), 성공 시 초기화, 변경·제거에
 *           현재 PIN 필요, 시계가 뒤로 가도 대기 상한.
 *           사용자 데이터 키: 첫 부팅에 만들어 이름 붙이기 전에 저장, 빈 디렉터리만
 *           정책, PIN 이 있으면 맞는 PIN 전에는 올라가지 않음, PIN 을 바꾸거나 지워도
 *           같은 키, 키는 평문으로 저장되지 않음
 * 의존방향: runtime/services/lock/lock.c (ZYL_TEST), gio, OpenSSL. 커널 fscrypt 는
 *           이 파일의 가짜(키 표·정책 표)가 대신한다 — 실제 ioctl 은 QEMU 레인의
 *           zyl-lock-prepare 가 본다
 * SOLID: SRP — 잠금 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-lock-test"
#define ZYL_LOCK_DIR DIR_
#define ZYL_CE_DIRS DIR_ "-ce/users", DIR_ "-ce/messages"
#include <assert.h>
#include "../runtime/services/lock/lock.c"

static LockState s;

/* ─── The kernel, faked: keys on the filesystem, policies on directories ─── */
static unsigned char k_key[ZYL_FSCRYPT_KEY_LEN], k_id[ZYL_FSCRYPT_ID_LEN];
static bool k_on;                       /* the one key is on /data */
static GHashTable *k_policy;            /* dir → id (16 bytes) */

bool zyl_fscrypt_add_key(const char *path, const unsigned char key[ZYL_FSCRYPT_KEY_LEN],
                         unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    (void)path;
    memcpy(k_key, key, sizeof(k_key));
    for (int i = 0; i < ZYL_FSCRYPT_ID_LEN; i++) k_id[i] = key[i] ^ 0x5a;   /* derived from the key */
    memcpy(id, k_id, sizeof(k_id));
    k_on = true;
    return true;
}
bool zyl_fscrypt_set_policy(const char *dir, const unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    if (g_hash_table_contains(k_policy, dir) || !dir_empty(dir)) return false;
    g_hash_table_insert(k_policy, g_strdup(dir), g_memdup2(id, ZYL_FSCRYPT_ID_LEN));
    return true;
}
int zyl_fscrypt_get_policy(const char *dir, unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    const unsigned char *p = g_hash_table_lookup(k_policy, dir);
    if (p) memcpy(id, p, ZYL_FSCRYPT_ID_LEN);
    return p ? 1 : 0;
}
int zyl_fscrypt_key_present(const char *path, const unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    (void)path;
    return k_on && memcmp(id, k_id, sizeof(k_id)) == 0;
}
static void reboot_(void) { k_on = false; lock_load(&s); }

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static const gint64 T = 1800000000;

static void reset(void) {
    g_assert_true(system("rm -rf " DIR_ " " DIR_ "-ce && mkdir -p " DIR_ "-ce/users " DIR_ "-ce/messages") == 0);
    if (k_policy) g_hash_table_destroy(k_policy);
    k_policy = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    k_on = false;
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

/* First boot: a key, kept before any directory names it, on /data, and
   every user data directory under it — never in the clear on disk */
static void test_first_boot_makes_the_key_and_locks_the_user_data(void) {
    assert(ce_prepare(&s) == 0);
    assert(s.ce[0] && s.ce_id[0] && k_on);
    LockState disk;
    lock_load(&disk);
    assert(strcmp(disk.ce, s.ce) == 0);
    unsigned char id[ZYL_FSCRYPT_ID_LEN];
    assert(zyl_fscrypt_get_policy(DIR_ "-ce/users", id) == 1 && memcmp(id, k_id, sizeof(id)) == 0);
    assert(zyl_fscrypt_get_policy(DIR_ "-ce/messages", id) == 1);
    char hex[ZYL_FSCRYPT_KEY_LEN * 2 + 1], *file = NULL;
    to_hex(k_key, 24, hex);                                   /* any stretch of the key */
    g_assert_true(g_file_get_contents(ZYL_LOCK_FILE, &file, NULL, NULL));
    assert(!strstr(file, hex));
    g_free(file);
    reboot_();                                                /* no PIN: opens at boot */
    assert(ce_prepare(&s) == 0 && k_on);
}

/* With nowhere to keep the key, no directory gets a policy naming it */
static void test_no_policy_for_a_key_that_was_not_kept(void) {
    g_assert_true(system("mkdir -p " ZYL_LOCK_FILE) == 0);   /* cannot be written */
    assert(ce_prepare(&s) == 1);
    assert(!s.ce[0] && g_hash_table_size(k_policy) == 0);
}

/* A directory that already holds plain files cannot be encrypted in place:
   it is reported, the others still are */
static void test_a_plain_directory_with_files_is_reported(void) {
    g_assert_true(system("touch " DIR_ "-ce/messages/thread_1") == 0);
    assert(ce_prepare(&s) == 1);
    unsigned char id[ZYL_FSCRYPT_ID_LEN];
    assert(zyl_fscrypt_get_policy(DIR_ "-ce/users", id) == 1);
    assert(zyl_fscrypt_get_policy(DIR_ "-ce/messages", id) == 0);
}

/* With a PIN the key is not on after boot until the right PIN, and stays the
   same key through changing and removing the PIN */
static void test_the_pin_opens_the_user_data(void) {
    assert(ce_prepare(&s) == 0);
    unsigned char first[ZYL_FSCRYPT_KEY_LEN];
    memcpy(first, k_key, sizeof(first));
    assert(*lock_set_pin(&s, "", PIN6, T) == '\0');
    reboot_();
    assert(ce_prepare(&s) == 0 && !k_on);                     /* encrypted, locked */
    for (int i = 0; i < FREE_ATTEMPTS; i++) assert(lock_verify(&s, "000000", T) == CHECK_WRONG && !k_on);
    assert(lock_verify(&s, PIN6, T) == CHECK_WAIT && !k_on);  /* waiting: not even the storage */
    assert(lock_verify(&s, PIN6, T + BASE_WAIT_S) == CHECK_OK && k_on);
    assert(memcmp(k_key, first, sizeof(first)) == 0);

    assert(*lock_set_pin(&s, PIN6, "555555", T) == '\0');
    reboot_();
    assert(lock_verify(&s, PIN6, T) == CHECK_WRONG && !k_on);  /* the old PIN opens nothing */
    assert(lock_verify(&s, "555555", T) == CHECK_OK && k_on);
    assert(memcmp(k_key, first, sizeof(first)) == 0);

    assert(*lock_set_pin(&s, "555555", "", T) == '\0');       /* removed: opens at boot */
    reboot_();
    assert(ce_prepare(&s) == 0 && k_on);
    assert(memcmp(k_key, first, sizeof(first)) == 0);
}

/* The key moves with the hash in one write: a change that cannot be saved
   leaves both as they were */
static void test_pin_and_key_change_together(void) {
    assert(ce_prepare(&s) == 0);
    assert(*lock_set_pin(&s, "", PIN6, T) == '\0');
    LockState kept = s;
    g_assert_true(system("rm -f " ZYL_LOCK_FILE " && mkdir " ZYL_LOCK_FILE) == 0);
    assert(strcmp(lock_set_pin(&s, PIN6, "555555", T), "storage") == 0);
    assert(strcmp(s.ce, kept.ce) == 0 && strcmp(s.hash, kept.hash) == 0);
    g_assert_true(system("rmdir " ZYL_LOCK_FILE) == 0);
    lock_save(&s);
    reboot_();
    assert(lock_verify(&s, PIN6, T) == CHECK_OK && k_on);
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
    RUN_TEST(test_first_boot_makes_the_key_and_locks_the_user_data);
    RUN_TEST(test_no_policy_for_a_key_that_was_not_kept);
    RUN_TEST(test_a_plain_directory_with_files_is_reported);
    RUN_TEST(test_the_pin_opens_the_user_data);
    RUN_TEST(test_pin_and_key_change_together);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    (void)lock_vtable; (void)lock_introspection_xml;
    return tests_pass == tests_run ? 0 : 1;
}
