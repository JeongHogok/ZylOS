/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 키체인(runtime/services/credential) 단위 테스트
 * 수행범위: AES-256-GCM 왕복(디스크에는 암호문만, 0600, 매번 새 salt·IV),
 *           솔트·IV·암호문·태그 변조와 잘린 파일 거부, 틀린 암호 거부(재시작 후
 *           같은 암호는 열림), 마스터 키 전에는 저장·조회 거부, 이름 검사
 *           (빈 값·..·/·\·길이 초과)와 저장소 밖 탈출 차단, 경로가 잘려 두 계정이
 *           한 파일을 쓰지 않음, 덮어쓰기·삭제
 * 의존방향: runtime/services/credential/credential.c (ZYL_TEST), gio, OpenSSL
 * SOLID: SRP — 자격 증명 저장소 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-credential-test"
#include <assert.h>
#include <unistd.h>
#include "../runtime/services/credential/credential.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static ZylCredentialStore *s;

static void reopen(void) {                                 /* daemon restart */
    zyl_credential_destroy(s);
    s = zyl_credential_create(DIR_);
    assert(s);
}

static void reset(void) {
    assert(system("rm -rf " DIR_) == 0);
    reopen();
}

static void unlock(const char *pass) {
    assert(zyl_credential_set_master_key(s, pass, strlen(pass)) == 0);
}

static int put(const char *svc, const char *acct, const char *secret) {
    return zyl_credential_store(s, svc, acct, ZYL_CRED_PASSWORD, secret, strlen(secret), NULL);
}

/* Lookup as a NUL-terminated string (g_free), NULL when refused */
static char *get(const char *svc, const char *acct) {
    void *v = NULL;
    size_t n = 0;
    if (zyl_credential_lookup(s, svc, acct, &v, &n) != 0) return NULL;
    char *str = g_strndup(v, n);
    free(v);
    return str;
}

static bool same(char *got, const char *want) {
    bool ok = got && strcmp(got, want) == 0;
    g_free(got);
    return ok;
}

#define SECRET "correct-horse-battery-staple"
#define FILE_ DIR_ "/mail/ana.enc"

static void test_secret_round_trips_and_disk_holds_only_ciphertext(void) {
    unlock("pass");
    assert(put("mail", "ana", SECRET) == 0);
    assert(same(get("mail", "ana"), SECRET));
    gchar *a = NULL;
    gsize alen = 0;
    g_assert_true(g_file_get_contents(FILE_, &a, &alen, NULL));
    assert(alen == SALT_LEN + IV_LEN + strlen(SECRET) + TAG_LEN);
    assert(!g_strstr_len(a, (gssize)alen, "horse"));
    struct stat st;
    assert(stat(FILE_, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(put("mail", "ana", SECRET) == 0);               /* same secret again */
    gchar *b = NULL;
    g_assert_true(g_file_get_contents(FILE_, &b, NULL, NULL));
    assert(memcmp(a, b, alen) != 0);                       /* fresh salt and IV */
    g_free(a); g_free(b);
}

static void test_any_tampered_byte_or_truncation_is_rejected(void) {
    unlock("pass");
    assert(put("mail", "ana", SECRET) == 0);
    gchar *orig = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents(FILE_, &orig, &len, NULL));
    /* salt, IV, ciphertext, tag */
    size_t at[] = { 0, SALT_LEN, SALT_LEN + IV_LEN, len - 1 };
    for (size_t i = 0; i < G_N_ELEMENTS(at); i++) {
        orig[at[i]] ^= 0x01;
        g_assert_true(g_file_set_contents(FILE_, orig, (gssize)len, NULL));
        assert(get("mail", "ana") == NULL);
        orig[at[i]] ^= 0x01;
    }
    g_assert_true(g_file_set_contents(FILE_, orig, (gssize)len - 1, NULL));
    assert(get("mail", "ana") == NULL);                    /* tag cut short */
    g_assert_true(g_file_set_contents(FILE_, orig, SALT_LEN + IV_LEN, NULL));
    assert(get("mail", "ana") == NULL);                    /* no room for a tag */
    g_assert_true(g_file_set_contents(FILE_, orig, (gssize)len, NULL));
    assert(same(get("mail", "ana"), SECRET));              /* untouched file opens */
    g_free(orig);
}

static void test_wrong_passphrase_cannot_read_right_one_can_after_restart(void) {
    unlock("right");
    assert(put("mail", "ana", SECRET) == 0);
    struct stat st;
    assert(stat(DIR_ "/.master_salt", &st) == 0);
    assert(st.st_size == SALT_LEN && (st.st_mode & 0777) == 0600);
    reopen();
    unlock("wrong");
    assert(get("mail", "ana") == NULL);
    reopen();
    unlock("right");                                       /* salt kept on disk */
    assert(same(get("mail", "ana"), SECRET));
}

static void test_nothing_is_stored_or_read_before_the_master_key(void) {
    assert(put("mail", "ana", SECRET) == -1);
    assert(access(FILE_, F_OK) != 0);
    unlock("pass");
    assert(put("mail", "ana", SECRET) == 0);
    reopen();                                              /* key is not on disk */
    assert(get("mail", "ana") == NULL);
    assert(zyl_credential_store(s, "mail", "ana", ZYL_CRED_PASSWORD, "", 0, NULL) == -1);
}

/* The daemon opens with this device's own key: made once (0600), the same
   after a restart, and never replaced when it is there but broken */
static void test_the_device_key_opens_the_store_and_survives_restart(void) {
    assert(zyl_credential_open_with_device_key(s) == 0);
    struct stat st;
    assert(stat(DIR_ "/.device_key", &st) == 0);
    assert(st.st_size == 32 && (st.st_mode & 0777) == 0600);
    assert(put("wifi", "home", SECRET) == 0);
    reopen();
    assert(get("wifi", "home") == NULL);                   /* not open until the key is read */
    assert(zyl_credential_open_with_device_key(s) == 0);
    assert(same(get("wifi", "home"), SECRET));
    char *before = NULL;
    gsize n = 0;
    assert(g_file_get_contents(DIR_ "/.device_key", &before, &n, NULL));
    assert(truncate(DIR_ "/.device_key", 7) == 0);
    reopen();
    assert(zyl_credential_open_with_device_key(s) == -1);   /* broken: left locked */
    assert(put("wifi", "home", "x") == -1);
    assert(stat(DIR_ "/.device_key", &st) == 0 && st.st_size == 7);   /* not replaced */
    g_free(before);
}

static void test_names_that_could_leave_their_file_are_rejected(void) {
    char max[MAX_NAME_LEN + 2];
    memset(max, 'a', MAX_NAME_LEN);
    max[MAX_NAME_LEN] = '\0';
    assert(is_safe_name(max));
    assert(is_safe_name("com.zylos.mail") && is_safe_name("ana@example.com"));
    assert(is_safe_name("a-b_c.d"));
    max[MAX_NAME_LEN] = 'a';
    max[MAX_NAME_LEN + 1] = '\0';
    assert(!is_safe_name(max));
    const char *bad[] = { NULL, "", "..", "../x", "x/..", "a/b", "a\\b", "/etc" };
    char out[512];
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        assert(!is_safe_name(bad[i]));
        assert(!build_path(out, sizeof(out), DIR_, bad[i], "ok") && out[0] == '\0');
        assert(!build_path(out, sizeof(out), DIR_, "ok", bad[i]) && out[0] == '\0');
    }
    unlock("pass");
    assert(put("../zyl-credential-escape", "x", SECRET) == -1);
    assert(put("mail", "../../zyl-credential-escape", SECRET) == -1);
    assert(access("/tmp/zyl-credential-escape", F_OK) != 0);
    assert(access("/tmp/zyl-credential-escape.enc", F_OK) != 0);
    assert(zyl_credential_delete(s, "..", "x") == -1);
}

/* 24 + 1 + 255 + 1 + 240 + 4 bytes do not fit the 512-byte path: cut short,
   two accounts sharing their first 230 characters wrote one file, and each
   read the other's secret */
static void test_long_names_never_share_a_file(void) {
    unlock("pass");
    char svc[MAX_NAME_LEN + 1], a[241], b[241];
    memset(svc, 's', MAX_NAME_LEN); svc[MAX_NAME_LEN] = '\0';
    memset(a, 'x', 240); a[240] = '\0';
    memcpy(b, a, sizeof(a));
    a[239] = 'A'; b[239] = 'B';
    assert(put(svc, a, "alpha") == -1 && put(svc, b, "bravo") == -1);
    assert(get(svc, a) == NULL && get(svc, b) == NULL);
    svc[200] = a[200] = b[200] = '\0';
    a[199] = 'A'; b[199] = 'B';                            /* these fit */
    assert(put(svc, a, "alpha") == 0 && put(svc, b, "bravo") == 0);
    assert(same(get(svc, a), "alpha") && same(get(svc, b), "bravo"));
}

static void test_store_overwrites_and_delete_removes(void) {
    unlock("pass");
    assert(put("mail", "ana", "one") == 0);
    assert(put("mail", "ana", "two") == 0);
    assert(put("mail", "ben", "three") == 0);
    assert(same(get("mail", "ana"), "two"));
    assert(zyl_credential_delete(s, "mail", "ana") == 0);
    assert(access(FILE_, F_OK) != 0 && get("mail", "ana") == NULL);
    assert(zyl_credential_delete(s, "mail", "ana") == -1);
    assert(same(get("mail", "ben"), "three"));             /* neighbour untouched */
}

int main(void) {
    printf("=== Zyl OS Credential Store Tests ===\n");
    RUN_TEST(test_secret_round_trips_and_disk_holds_only_ciphertext);
    RUN_TEST(test_any_tampered_byte_or_truncation_is_rejected);
    RUN_TEST(test_wrong_passphrase_cannot_read_right_one_can_after_restart);
    RUN_TEST(test_nothing_is_stored_or_read_before_the_master_key);
    RUN_TEST(test_the_device_key_opens_the_store_and_survives_restart);
    RUN_TEST(test_names_that_could_leave_their_file_are_rejected);
    RUN_TEST(test_long_names_never_share_a_file);
    RUN_TEST(test_store_overwrites_and_delete_removes);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    zyl_credential_destroy(s);
    return tests_pass == tests_run ? 0 : 1;
}
