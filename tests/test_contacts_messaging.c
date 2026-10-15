/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 연락처(runtime/services/contacts)·메시지(runtime/services/messaging)
 *       저장소 단위 테스트
 * 수행범위: 연락처 — 만들기, 준 필드만 고치기(별표가 이름을 지우지 않음),
 *           이름순, 대소문자 무시 검색, 삭제, 없는 연락처 고치기 거부, 0600.
 *           메시지 — 번호 → 대화 id, 앱이 준 id 로 경로 탈출 불가, 받은 것만
 *           안 읽음, 최근 대화 먼저, 보낸 결과 상태, 읽음, 삭제
 * 의존방향: contacts.c, messaging.c (ZYL_TEST), glib
 * SOLID: SRP — 두 저장소의 규칙 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-cm-test"
#define ZYL_CONTACTS_FILE ROOT_ "/contacts/contacts"
#define ZYL_MESSAGES_DIR ROOT_ "/messages"
#include "../runtime/services/contacts/contacts.c"
#include "../runtime/services/messaging/messaging.c"
#include <assert.h>
#include <sys/stat.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); assert(system("rm -rf " ROOT_) == 0); fn(); tests_pass++; printf("PASS\n"); } while (0)

static GVariant *fields(const char *json) {
    GVariant *v = g_variant_parse(G_VARIANT_TYPE("a{sv}"), json, NULL, NULL, NULL);
    assert(v);
    return v;
}

static char *str(GVariant *dict, const char *key) {
    const gchar *v = NULL;
    assert(g_variant_lookup(dict, key, "&s", &v));
    return g_strdup(v);
}

static void test_contact_update_keeps_other_fields(void) {
    GVariant *c = create(fields("{'name': <'Mina'>, 'phone': <'010-1234-5678'>}"));
    char *id = str(c, "id");
    g_variant_unref(c);
    char *upd = g_strdup_printf("{'id': <'%s'>, 'starred': <true>}", id);
    c = update(fields(upd));
    char *name = str(c, "name");
    gboolean starred = FALSE;
    assert(g_variant_lookup(c, "starred", "b", &starred) && starred);
    assert(strcmp(name, "Mina") == 0);
    g_variant_unref(c);
    struct stat st;
    assert(stat(ZYL_CONTACTS_FILE, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(!update(fields("{'id': <'c_nobody'>, 'name': <'x'>}")));
    g_free(upd); g_free(name); g_free(id);
}

static void test_contacts_sorted_searched_deleted(void) {
    g_variant_unref(create(fields("{'name': <'Yuna'>, 'phone': <'0102222'>}")));
    GVariant *c = create(fields("{'name': <'alex'>, 'phone': <'0101111'>}"));
    char *id = str(c, "id");
    g_variant_unref(c);
    GKeyFile *kf = load();
    GVariant *all = list(kf, NULL);
    assert(g_variant_n_children(all) == 2);
    GVariant *first = g_variant_get_child_value(all, 0);
    char *name = str(first, "name");
    assert(strcmp(name, "alex") == 0);
    GVariant *hit = list(kf, "ALE");
    assert(g_variant_n_children(hit) == 1);
    GVariant *byphone = list(kf, "2222");
    assert(g_variant_n_children(byphone) == 1);
    g_variant_unref(first); g_variant_unref(all); g_variant_unref(hit); g_variant_unref(byphone);
    g_key_file_free(kf);
    assert(remove_contact(id) && !remove_contact(id));
    g_free(name); g_free(id);
}

static void test_thread_ids_are_safe_names(void) {
    char *a = thread_id_for("+82 10-1234-5678"), *b = thread_id_for("KT"), *c = thread_id_for("");
    assert(strcmp(a, "thread_821012345678") == 0 && strcmp(b, "thread_KT") == 0 &&
           strcmp(c, "thread_unknown") == 0);
    g_free(a); g_free(b); g_free(c);
    assert(valid_thread_id("thread_123"));
    assert(!valid_thread_id("thread_") && !valid_thread_id("../contacts/contacts") &&
           !valid_thread_id("thread_1/../../x") && !valid_thread_id("thread_..") && !valid_thread_id(NULL));
    assert(!load_thread("../contacts/contacts") && !delete_thread("thread_1/.."));
}

static void test_threads_messages_and_unread(void) {
    GVariant *m = NULL;
    char *t1 = record("010-1", "hi", false, "received", 1000, NULL);
    char *t2 = record("010-2", "yo", true, "failed", 2000, &m);
    char *t1b = record("0101", "again", false, "received", 3000, NULL);
    assert(t1 && t2 && t1b && strcmp(t1, t1b) == 0);
    char *status = str(m, "status");
    assert(strcmp(status, "failed") == 0);

    GVariant *threads = list_threads();
    assert(g_variant_n_children(threads) == 2);
    GVariant *newest = g_variant_get_child_value(threads, 0);
    char *nid = str(newest, "id"), *last = str(newest, "lastMessage");
    gint32 unread = -1;
    assert(g_variant_lookup(newest, "unread", "i", &unread));
    assert(strcmp(nid, t1) == 0 && strcmp(last, "again") == 0 && unread == 2);

    GVariant *msgs = list_messages(t1);
    assert(g_variant_n_children(msgs) == 2);
    GVariant *m0 = g_variant_get_child_value(msgs, 0);
    char *text = str(m0, "text");
    assert(strcmp(text, "hi") == 0);

    assert(mark_read(t1));
    GVariant *after = list_threads();
    GVariant *a0 = g_variant_get_child_value(after, 0);
    assert(g_variant_lookup(a0, "unread", "i", &unread) && unread == 0);

    GVariant *sent_thread = g_variant_get_child_value(after, 1);
    assert(g_variant_lookup(sent_thread, "unread", "i", &unread) && unread == 0);

    assert(delete_thread(t2) && !delete_thread(t2));
    GVariant *left = list_threads();
    assert(g_variant_n_children(left) == 1);

    g_variant_unref(m); g_variant_unref(threads); g_variant_unref(newest); g_variant_unref(msgs);
    g_variant_unref(m0); g_variant_unref(after); g_variant_unref(a0); g_variant_unref(sent_thread);
    g_variant_unref(left);
    g_free(t1); g_free(t2); g_free(t1b); g_free(status); g_free(nid); g_free(last); g_free(text);
}

int main(void) {
    printf("=== contacts / messaging store tests ===\n");
    RUN_TEST(test_contact_update_keeps_other_fields);
    RUN_TEST(test_contacts_sorted_searched_deleted);
    RUN_TEST(test_thread_ids_are_safe_names);
    RUN_TEST(test_threads_messages_and_unread);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    assert(system("rm -rf " ROOT_) == 0);
    return tests_pass == tests_run ? 0 : 1;
}
