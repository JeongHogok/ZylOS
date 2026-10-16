/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 로거 서비스(runtime/services/logger) 단위 테스트
 * 수행범위: 한 줄에 JSON 하나(제어 문자·따옴표·줄바꿈·거대한 메시지·잘리는 UTF-8
 *           에도 유효한 JSON·UTF-8), 시각 이후 로그 읽기, 메시지로 크래시 보고를
 *           위조할 수 없음, 치명 시그널은 시각이 찍힌 크래시 줄을 남기고(시각 이후
 *           로그에도 나옴) 그대로 죽음, 크기
 *           한도에서 로테이션(옛 파일 5개, 가장 오래된 것 삭제, 재시작 후 꽉 찬
 *           파일도), 필터 레벨(낮은 레벨 버림, 재시작 후 유지, 깨진 파일은 전부 기록)
 * 의존방향: runtime/services/logger/logger.c (ZYL_TEST), gio, json-glib
 * SOLID: SRP — 로그 저장 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define DIR_ "/tmp/zyl-logger-test"
#define ZYL_LOG_DIR DIR_
#include <assert.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <json-glib/json-glib.h>
#include "../runtime/services/logger/logger.c"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fflush(stdout); reset(); fn(); tests_pass++; printf("PASS\n"); } while (0)
static ZylLoggerService *svc;

static void reset(void) {
    if (svc) zyl_logger_service_destroy(svc);
    g_assert_true(system("rm -rf " DIR_) == 0);
    svc = zyl_logger_service_create();
    assert(svc);
}

static void put(const char *path, const char *text) {
    assert(g_file_set_contents(path, text, -1, NULL));
}

static off_t size_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_size : -1;
}

static int mode_of(const char *path) {
    struct stat st;
    assert(stat(path, &st) == 0);
    return st.st_mode & 0777;
}

/* What a JSON reader takes: valid UTF-8, no raw control characters */
static JsonNode *parse(const char *text) {
    assert(g_utf8_validate(text, -1, NULL));
    for (const char *p = text; *p; p++) assert((unsigned char)*p >= 0x20);
    JsonParser *jp = json_parser_new();
    assert(json_parser_load_from_data(jp, text, -1, NULL));
    JsonNode *n = json_node_copy(json_parser_get_root(jp));
    g_object_unref(jp);
    return n;
}

static gchar **log_lines(void) {
    gchar *text = NULL;
    assert(g_file_get_contents(ZYL_LOG_FILE, &text, NULL, NULL));
    assert(g_str_has_suffix(text, "\n"));
    text[strlen(text) - 1] = '\0';
    gchar **lines = g_strsplit(text, "\n", -1);
    g_free(text);
    return lines;
}

static const char *member(JsonNode *n, guint i, const char *key) {
    JsonNode *e = JSON_NODE_HOLDS_ARRAY(n) ? json_array_get_element(json_node_get_array(n), i) : n;
    return json_object_get_string_member(json_node_get_object(e), key);
}

static guint count(JsonNode *array) {
    return json_array_get_length(json_node_get_array(array));
}

static void test_entries_are_private_json_lines_read_back_from_a_time(void) {
    int64_t start = now_unix_ms();
    zyl_logger_write(svc, ZYL_LOG_INFO, "wifi", "connected");
    assert(mode_of(DIR_) == 0750 && mode_of(ZYL_LOG_FILE) == 0600);
    FILE *f = fopen(ZYL_LOG_FILE, "a");                    /* entries from long ago */
    fputs("{\"ts\":1000,\"level\":\"INFO\",\"source\":\"a\",\"msg\":\"old\"}\n"
          "{\"ts\":2000,\"level\":\"INFO\",\"source\":\"b\",\"msg\":\"older\"}\n", f);
    fclose(f);

    char *all = read_logs_since(0), *recent = read_logs_since(1500), *none = read_logs_since(start + 3600000);
    JsonNode *a = parse(all), *r = parse(recent);
    assert(count(a) == 3 && count(r) == 2);
    assert(strcmp(member(a, 0, "level"), "INFO") == 0 && strcmp(member(a, 0, "source"), "wifi") == 0);
    assert(strcmp(member(a, 0, "msg"), "connected") == 0);
    assert(json_object_get_int_member(json_node_get_object(json_array_get_element(json_node_get_array(a), 0)), "ts") >= start);
    assert(strcmp(member(r, 1, "source"), "b") == 0);
    assert(strcmp(none, "[]") == 0);
    json_node_unref(a); json_node_unref(r);
    g_free(all); g_free(recent); g_free(none);
}

static void test_hostile_messages_stay_one_valid_json_line_each(void) {
    GString *quotes = g_string_new(NULL), *hangul = g_string_new(NULL), *huge = g_string_new(NULL);
    for (int i = 0; i < 5000; i++) g_string_append(quotes, "\"\\");   /* every byte doubles */
    for (int i = 0; i < 1000; i++) g_string_append(hangul, "가");     /* cut lands inside a character */
    for (int i = 0; i < 1024 * 1024; i++) g_string_append_c(huge, 'x');
    const char *ctl = "bell\x07 esc\x1b[31m nul-ish\x01 del\x7f";
    const char *breaks = "one\ntwo\r\tthree \"q\" \\ end";
    const char *msgs[] = { ctl, breaks, quotes->str, hangul->str, huge->str };
    for (size_t i = 0; i < G_N_ELEMENTS(msgs); i++)
        zyl_logger_write(svc, ZYL_LOG_WARNING, "app\n\x02", msgs[i]);

    gchar **lines = log_lines();
    assert(g_strv_length(lines) == G_N_ELEMENTS(msgs));          /* a newline in a message is not a new entry */
    for (int i = 0; lines[i]; i++) {
        assert(strlen(lines[i]) < 4096);                         /* GetLogs reads lines of up to 4 KiB */
        JsonNode *n = parse(lines[i]);
        assert(strcmp(member(n, 0, "source"), "app\n\x02") == 0);
        const char *msg = member(n, 0, "msg");
        assert(*msg && g_str_has_prefix(msgs[i], msg));          /* the message, or the start of it */
        if (i < 2) assert(strcmp(msg, msgs[i]) == 0);
        json_node_unref(n);
    }
    char *all = read_logs_since(0);                              /* goes out as a D-Bus string */
    JsonNode *a = parse(all);
    assert(count(a) == G_N_ELEMENTS(msgs));
    json_node_unref(a);
    g_free(all);
    g_strfreev(lines);
    g_string_free(quotes, TRUE); g_string_free(hangul, TRUE); g_string_free(huge, TRUE);
}

static void test_a_message_cannot_fake_a_crash_report(void) {
    zyl_logger_write(svc, ZYL_LOG_INFO, "x\",\"level\":\"CRASH", "\"CRASH\" \\\"CRASH\"");
    char *r = read_crash_reports();
    assert(strcmp(r, "[]") == 0);
    g_free(r);
    zyl_logger_write(svc, ZYL_LOG_CRASH, "compositor", "abort");
    r = read_crash_reports();
    JsonNode *n = parse(r);
    assert(count(n) == 1 && strcmp(member(n, 0, "source"), "compositor") == 0);
    json_node_unref(n);
    g_free(r);
}

static void test_a_fatal_signal_leaves_a_crash_line_and_still_kills(void) {
    int64_t start = now_unix_ms();
    zyl_logger_write(svc, ZYL_LOG_INFO, "t", "before");          /* log open before the fork */
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        struct rlimit no_core = { 0, 0 };
        setrlimit(RLIMIT_CORE, &no_core);
        dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);       /* the handler also tells stderr */
        zyl_logger_install_crash_handler(svc);
        raise(SIGSEGV);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);  /* re-raised: a core dump still comes */
    char *r = read_crash_reports();
    JsonNode *n = parse(r);
    assert(count(n) == 1 && strcmp(member(n, 0, "source"), "signal") == 0);
    json_node_unref(n);
    g_free(r);
    /* Stamped like any entry, so the log read from a time has it too */
    char *logs = read_logs_since(start);
    JsonNode *l = parse(logs);
    assert(count(l) == 2 && strcmp(member(l, 1, "level"), "CRASH") == 0);
    gint64 ts = json_object_get_int_member(json_node_get_object(json_array_get_element(json_node_get_array(l), 1)), "ts");
    assert(ts >= start && ts <= now_unix_ms());
    json_node_unref(l);
    g_free(logs);
}

static void test_log_rotates_once_it_passes_the_size_limit(void) {
    char msg[2001];
    memset(msg, 'x', sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    int writes = 0;
    while (size_of(ZYL_LOG_FILE ".1") < 0 && writes < 10000) {
        zyl_logger_write(svc, ZYL_LOG_INFO, "t", msg);
        writes++;
    }
    off_t rotated = size_of(ZYL_LOG_FILE ".1");
    assert(rotated >= ZYL_LOG_MAX_SIZE && rotated < ZYL_LOG_MAX_SIZE + 4096);   /* at most one line over */
    gchar **lines = log_lines();
    assert(g_strv_length(lines) == 1);                           /* the write that rotated starts the new file */
    g_strfreev(lines);
    assert(mode_of(ZYL_LOG_FILE) == 0600);
}

static void test_rotation_keeps_five_old_logs_and_drops_the_oldest(void) {
    assert(mkdir(DIR_, 0750) == 0);
    for (int i = 1; i <= ZYL_LOG_MAX_ROTATED; i++) {
        char *path = g_strdup_printf(ZYL_LOG_FILE ".%d", i), *text = g_strdup_printf("old%d", i);
        put(path, text);
        g_free(path); g_free(text);
    }
    put(ZYL_LOG_FILE, "");
    assert(truncate(ZYL_LOG_FILE, ZYL_LOG_MAX_SIZE) == 0);       /* full when the service last stopped */

    zyl_logger_write(svc, ZYL_LOG_INFO, "t", "fresh");
    zyl_logger_write(svc, ZYL_LOG_INFO, "t", "again");
    assert(size_of(ZYL_LOG_FILE ".1") == ZYL_LOG_MAX_SIZE);
    for (int i = 2; i <= ZYL_LOG_MAX_ROTATED; i++) {
        char *path = g_strdup_printf(ZYL_LOG_FILE ".%d", i), *text = NULL, *want = g_strdup_printf("old%d", i - 1);
        assert(g_file_get_contents(path, &text, NULL, NULL) && strcmp(text, want) == 0);
        g_free(path); g_free(text); g_free(want);
    }
    assert(size_of(ZYL_LOG_FILE ".6") < 0);                      /* old5 is gone, nothing past .5 */
    gchar **lines = log_lines();
    assert(g_strv_length(lines) == 2 && strstr(lines[0], "fresh") && strstr(lines[1], "again"));
    g_strfreev(lines);
}

static void test_filter_level_drops_lower_levels_and_survives_restart(void) {
    assert(!valid_level(-1) && !valid_level(ZYL_LOG_CRASH + 1));
    assert(valid_level(ZYL_LOG_DEBUG) && valid_level(ZYL_LOG_CRASH));
    svc->min_level = ZYL_LOG_WARNING;                            /* what SetFilterLevel(2) does */
    save_filter_level(svc);
    assert(mode_of(ZYL_LOG_FILTER_FILE) == 0600);
    zyl_logger_write(svc, ZYL_LOG_DEBUG, "t", "a");
    zyl_logger_write(svc, ZYL_LOG_INFO, "t", "b");
    zyl_logger_write(svc, ZYL_LOG_WARNING, "t", "c");
    zyl_logger_write(svc, ZYL_LOG_ERROR, "t", "d");
    gchar **lines = log_lines();
    assert(g_strv_length(lines) == 2 && strstr(lines[0], "\"c\"") && strstr(lines[1], "\"d\""));
    g_strfreev(lines);

    zyl_logger_service_destroy(svc);                             /* restart */
    svc = zyl_logger_service_create();
    assert(svc->min_level == ZYL_LOG_WARNING);
}

static void test_a_broken_filter_file_means_log_everything(void) {
    assert(mkdir(DIR_, 0750) == 0);
    const char *bad[] = { "", "abc", "9", "-3" };
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        put(ZYL_LOG_FILTER_FILE, bad[i]);
        load_filter_level(svc);
        assert(svc->min_level == ZYL_LOG_DEBUG);
    }
    put(ZYL_LOG_FILTER_FILE, "3\n");
    load_filter_level(svc);
    assert(svc->min_level == ZYL_LOG_ERROR);
}

int main(void) {
    printf("=== Zyl OS Logger Tests ===\n");
    /* No system bus for the service to take a name on */
    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=" DIR_ "-no-bus", TRUE);
    RUN_TEST(test_entries_are_private_json_lines_read_back_from_a_time);
    RUN_TEST(test_hostile_messages_stay_one_valid_json_line_each);
    RUN_TEST(test_a_message_cannot_fake_a_crash_report);
    RUN_TEST(test_a_fatal_signal_leaves_a_crash_line_and_still_kills);
    RUN_TEST(test_log_rotates_once_it_passes_the_size_limit);
    RUN_TEST(test_rotation_keeps_five_old_logs_and_drops_the_oldest);
    RUN_TEST(test_filter_level_drops_lower_levels_and_survives_restart);
    RUN_TEST(test_a_broken_filter_file_means_log_everything);
    zyl_logger_service_destroy(svc);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
