/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 멀티유저 관리(runtime/services/user) 단위 테스트
 * 수행범위: 시작 시 소유자(uid 1000) 하나와 그 데이터 디렉터리, 소유자는 하나뿐,
 *           새 사용자마다 다음 uid 와 자기 디렉터리, 최대 8명, 긴 이름은 잘림,
 *           전환(활성 표시·없는 uid 거부), 삭제(소유자·현재 사용자 불가, 데이터
 *           디렉터리까지 지움), 재시작 후 새 사용자가 남의 데이터를 물려받지 않음
 * 의존방향: runtime/services/user/user.c (ZYL_TEST), gio, /bin/rm
 * SOLID: SRP — 사용자 계정 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-user-test"
#define ZYL_USER_DATA_ROOT DIR_
#include <assert.h>
#include <unistd.h>
#include "../runtime/services/user/user.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static ZylUserService *s;

static void reset(void) {
    zyl_user_destroy(s);
    assert(system("rm -rf " DIR_) == 0);
    s = zyl_user_create();
    assert(s);
}

static ZylUserProfile *find(uint32_t uid) {
    for (int i = 0; i < s->user_count; i++)
        if (s->users[i].uid == uid) return &s->users[i];
    return NULL;
}

static bool is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void test_owner_exists_from_the_start_with_its_data_dir(void) {
    assert(s->user_count == 1 && s->current_uid == 1000);
    ZylUserProfile *o = find(1000);
    assert(o && o->type == ZYL_USER_TYPE_OWNER && o->is_active);
    assert(strcmp(o->name, "Owner") == 0 && strcmp(o->data_dir, DIR_ "/1000") == 0);
    assert(is_dir(DIR_ "/1000/apps") && is_dir(DIR_ "/1000/downloads"));
    assert(is_dir(DIR_ "/1000/settings"));
}

static void test_there_is_only_one_owner(void) {
    uint32_t uid = 0;
    assert(zyl_user_add(s, "Second", ZYL_USER_TYPE_OWNER, &uid) == -1);
    assert(s->user_count == 1 && uid == 0);
    assert(zyl_user_add(s, NULL, ZYL_USER_TYPE_STANDARD, &uid) == -1);
}

static void test_each_user_gets_the_next_uid_and_own_dir(void) {
    uint32_t a = 0, b = 0;
    assert(zyl_user_add(s, "Ana", ZYL_USER_TYPE_STANDARD, &a) == 0 && a == 1001);
    assert(zyl_user_add(s, "Kid", ZYL_USER_TYPE_RESTRICTED, &b) == 0 && b == 1002);
    assert(strcmp(find(a)->data_dir, DIR_ "/1001") == 0 && !find(a)->is_active);
    assert(is_dir(DIR_ "/1001/apps") && is_dir(DIR_ "/1002/settings"));
    assert(s->current_uid == 1000);                        /* adding is not switching */
}

static void test_at_most_eight_users(void) {
    uint32_t uid;
    for (int i = 1; i < ZYL_USER_MAX; i++)
        assert(zyl_user_add(s, "U", ZYL_USER_TYPE_STANDARD, &uid) == 0);
    assert(s->user_count == ZYL_USER_MAX);
    assert(zyl_user_add(s, "One too many", ZYL_USER_TYPE_GUEST, &uid) == -1);
    assert(s->user_count == ZYL_USER_MAX);
}

static void test_long_name_is_cut_not_overflowed(void) {
    char name[200];
    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    uint32_t uid;
    assert(zyl_user_add(s, name, ZYL_USER_TYPE_STANDARD, &uid) == 0);
    assert(strlen(find(uid)->name) == ZYL_USER_NAME_MAX - 1);
}

static void test_switch_moves_the_active_user_and_refuses_strangers(void) {
    uint32_t a;
    zyl_user_add(s, "Ana", ZYL_USER_TYPE_STANDARD, &a);
    assert(zyl_user_switch(s, a) == 0);
    assert(s->current_uid == a && find(a)->is_active && find(a)->last_login_at > 0);
    assert(!find(1000)->is_active);
    assert(zyl_user_switch(s, 4242) == -1);
    assert(s->current_uid == a && find(a)->is_active);
}

static void test_remove_wipes_data_but_never_owner_or_current(void) {
    uint32_t a, b, c;
    zyl_user_add(s, "Ana", ZYL_USER_TYPE_STANDARD, &a);
    zyl_user_add(s, "Ben", ZYL_USER_TYPE_STANDARD, &b);
    zyl_user_add(s, "Cy", ZYL_USER_TYPE_GUEST, &c);
    assert(zyl_user_remove(s, 1000) == -1);                /* owner */
    zyl_user_switch(s, b);
    assert(zyl_user_remove(s, b) == -1);                   /* in use */
    assert(g_file_set_contents(DIR_ "/1001/apps/notes", "x", -1, NULL));
    assert(zyl_user_remove(s, a) == 0);
    assert(access(DIR_ "/1001", F_OK) != 0 && !find(a));
    assert(s->user_count == 3 && find(1000) && find(b) && find(c));
    assert(is_dir(DIR_ "/1002") && is_dir(DIR_ "/1003"));  /* only Ana's went */
    assert(zyl_user_remove(s, a) == -1);
}

/* Accounts are held in memory only, so after a restart only the owner is
   listed — but the other accounts' directories are still on disk. The next
   new account was handed uid 1001 again, and with it Ana's files. */
static void test_new_user_after_restart_does_not_inherit_old_data(void) {
    uint32_t a, b;
    zyl_user_add(s, "Ana", ZYL_USER_TYPE_STANDARD, &a);
    assert(g_file_set_contents(DIR_ "/1001/apps/diary", "private", -1, NULL));
    zyl_user_destroy(s);
    s = zyl_user_create();                                 /* restart */
    assert(access(DIR_ "/1000/apps", F_OK) == 0);          /* owner keeps its own */
    assert(zyl_user_add(s, "Ben", ZYL_USER_TYPE_STANDARD, &b) == 0);
    assert(b != a);
    char path[300];
    snprintf(path, sizeof(path), "%s/apps/diary", find(b)->data_dir);
    assert(access(path, F_OK) != 0);
    assert(access(DIR_ "/1001/apps/diary", F_OK) == 0);    /* nor is Ana's deleted */
}

int main(void) {
    printf("=== Zyl OS User Manager Tests ===\n");
    RUN_TEST(test_owner_exists_from_the_start_with_its_data_dir);
    RUN_TEST(test_there_is_only_one_owner);
    RUN_TEST(test_each_user_gets_the_next_uid_and_own_dir);
    RUN_TEST(test_at_most_eight_users);
    RUN_TEST(test_long_name_is_cut_not_overflowed);
    RUN_TEST(test_switch_moves_the_active_user_and_refuses_strangers);
    RUN_TEST(test_remove_wipes_data_but_never_owner_or_current);
    RUN_TEST(test_new_user_after_restart_does_not_inherit_old_data);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    zyl_user_destroy(s);
    return tests_pass == tests_run ? 0 : 1;
}
