/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 푸시 서비스(runtime/services/push) 단위 테스트
 * 수행범위: 토픽(추측할 수 없는 128비트, 앱마다 다름, 다시 물으면 같은 끝점), 끝점·
 *           스트림 주소(모든 토픽 하나로, 놓친 것은 since), 스트림 한 줄 해석(메시지만),
 *           토픽 → 앱, 서버 설정(https 만), 등록·since 의 저장과 복원, 서버가 없으면 꺼짐
 * 의존방향: push.c (ZYL_TEST), glib, json-glib
 * SOLID: SRP — 푸시의 순수 규칙만 검증
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-push-test"
#define PUSH_DATA_DIR ROOT_
#include "../runtime/services/push/push.c"
#include <assert.h>
#include <stdio.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); assert(system("rm -rf " ROOT_) == 0); fn(); tests_pass++; printf("PASS\n"); } while (0)

static Push *fresh(const char *url) {
    Push *ps = g_new0(Push, 1);
    ps->url = g_strdup(url);
    ps->regs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, reg_free);
    return ps;
}

static void drop(Push *ps) {
    g_free(ps->url);
    g_free(ps->since);
    g_hash_table_destroy(ps->regs);
    g_free(ps);
}

static void test_topics_are_random_and_an_apps_own(void) {
    Push *ps = fresh("https://push.example");
    char *a = push_register(ps, "com.example.chat", "Chat", "New message");
    char *again = push_register(ps, "com.example.chat", "Chat", "New message");
    char *b = push_register(ps, "com.example.mail", "Mail", "New mail");
    assert(a && b && strcmp(a, again) == 0 && strcmp(a, b) != 0);   /* the same app: the same endpoint */
    PushReg *r = g_hash_table_lookup(ps->regs, "com.example.chat");
    assert(strlen(r->topic) == 3 + 32 && g_str_has_prefix(r->topic, "zyl"));
    char *want = g_strdup_printf("https://push.example/%s?up=1", r->topic);
    assert(strcmp(a, want) == 0);
    assert(strcmp(push_owner(ps->regs, r->topic), "com.example.chat") == 0);
    assert(!push_owner(ps->regs, "zylnobody"));
    assert(!push_register(ps, "", "x", "y"));
    g_free(want); g_free(a); g_free(again); g_free(b);
    drop(ps);
}

static void test_no_server_is_no_push(void) {
    Push *ps = fresh("");
    assert(!push_register(ps, "com.example.chat", "Chat", "New"));
    assert(!push_stream_url(ps->url, ps->regs, NULL));
    drop(ps);
    g_mkdir_with_parents(ROOT_, 0700);
    assert(g_file_set_contents(ROOT_ "/a.json", "{\"url\":\"http://plain.example\"}", -1, NULL));
    char *u = config_url(ROOT_ "/a.json");
    assert(strcmp(u, "") == 0);                                       /* never plain http */
    g_free(u);
    assert(g_file_set_contents(ROOT_ "/b.json", "{\"url\":\"https://push.example//\"}", -1, NULL));
    u = config_url(ROOT_ "/b.json");
    assert(strcmp(u, "https://push.example") == 0);
    g_free(u);
    u = config_url(ROOT_ "/none.json");
    assert(strcmp(u, "") == 0);
    g_free(u);
}

static void test_one_stream_for_every_topic(void) {
    Push *ps = fresh("https://push.example");
    assert(!push_stream_url(ps->url, ps->regs, NULL));                /* nobody registered: no connection */
    PushReg *a = g_new0(PushReg, 1), *b = g_new0(PushReg, 1);
    a->topic = g_strdup("zylb"); b->topic = g_strdup("zyla");
    g_hash_table_replace(ps->regs, g_strdup("x"), a);
    g_hash_table_replace(ps->regs, g_strdup("y"), b);
    char *u = push_stream_url(ps->url, ps->regs, NULL);
    assert(strcmp(u, "https://push.example/zyla,zylb/json") == 0);
    g_free(u);
    u = push_stream_url(ps->url, ps->regs, "AbC123");
    assert(strcmp(u, "https://push.example/zyla,zylb/json?since=AbC123") == 0);
    g_free(u);
    drop(ps);
}

static void test_only_messages_count(void) {
    char *id = NULL, *topic = NULL, *msg = NULL;
    assert(push_parse_line("{\"id\":\"k1\",\"event\":\"message\",\"topic\":\"zyla\",\"message\":\"version=3\"}",
                           &id, &topic, &msg));
    assert(strcmp(id, "k1") == 0 && strcmp(topic, "zyla") == 0 && strcmp(msg, "version=3") == 0);
    g_free(id); g_free(topic); g_free(msg);
    assert(!push_parse_line("{\"id\":\"k2\",\"event\":\"keepalive\",\"topic\":\"zyla\"}", &id, &topic, &msg));
    assert(!push_parse_line("{\"id\":\"k3\",\"event\":\"open\",\"topic\":\"zyla\"}", &id, &topic, &msg));
    assert(!push_parse_line("not json", &id, &topic, &msg));
    assert(!push_parse_line("{\"event\":\"message\",\"topic\":\"zyla\"}", &id, &topic, &msg));   /* no id */
    assert(!push_parse_line(NULL, &id, &topic, &msg));
}

static void test_registrations_and_since_survive_a_restart(void) {
    Push *ps = fresh("https://push.example");
    char *e = push_register(ps, "com.example.chat", "Chat", "New message");
    ps->since = g_strdup("k9");
    state_save(ps);
    struct stat st;
    assert(stat(PUSH_STATE, &st) == 0 && (st.st_mode & 0777) == 0600);
    Push *back = fresh("https://push.example");
    state_load(back);
    PushReg *r = g_hash_table_lookup(back->regs, "com.example.chat");
    assert(r && strcmp(r->title, "Chat") == 0 && strcmp(r->body, "New message") == 0);
    assert(strcmp(back->since, "k9") == 0);
    char *again = push_register(back, "com.example.chat", "Chat", "New message");
    assert(strcmp(e, again) == 0);                                    /* the app's server keeps its endpoint */
    g_free(e); g_free(again);
    drop(ps); drop(back);
}

int main(void) {
    printf("=== push tests ===\n");
    RUN_TEST(test_topics_are_random_and_an_apps_own);
    RUN_TEST(test_no_server_is_no_push);
    RUN_TEST(test_one_stream_for_every_topic);
    RUN_TEST(test_only_messages_count);
    RUN_TEST(test_registrations_and_since_survive_a_restart);
    assert(system("rm -rf " ROOT_) == 0);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    return 0;
}
