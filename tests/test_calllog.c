/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 통화 기록 정책(runtime/services/telephony/calllog.c) 단위 테스트
 * 수행범위: 발신/수신/부재중 판정, 통화 시간, 같은 시각 통화의 고유 id,
 *           최근 500건 유지, 재시작 후 유지, 삭제
 * 의존방향: calllog.c, glib
 * SOLID: SRP — 통화 기록 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calllog.h"

#define FILE_ "/tmp/zyl-calllog-test/calllog"
static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void test_what_a_call_was_follows_from_its_states(void) {
    ZylCallLog *log = zyl_calllog_open(NULL);
    zyl_calllog_record(log, "010", false, 1000, 3000, 63000);   /* dialled, talked 60 s */
    zyl_calllog_record(log, "011", true, 100000, 101000, 131000);
    zyl_calllog_record(log, "012", true, 200000, 0, 230000);     /* rang out */
    assert(strcmp(zyl_calllog_get(log, 2)->type, "outgoing") == 0 && zyl_calllog_get(log, 2)->duration == 60);
    assert(strcmp(zyl_calllog_get(log, 1)->type, "incoming") == 0 && zyl_calllog_get(log, 1)->duration == 30);
    assert(strcmp(zyl_calllog_get(log, 0)->type, "missed") == 0 && zyl_calllog_get(log, 0)->duration == 0);
    assert(strcmp(zyl_calllog_get(log, 0)->number, "012") == 0);  /* newest first */
    zyl_calllog_free(log);
}

static void test_ids_are_unique_even_for_calls_at_the_same_time(void) {
    ZylCallLog *log = zyl_calllog_open(NULL);
    uint64_t a = zyl_calllog_record(log, "1", false, 5000, 0, 5000)->id;
    uint64_t b = zyl_calllog_record(log, "2", false, 5000, 0, 5000)->id;
    assert(b > a);
    zyl_calllog_free(log);
}

static void test_only_the_latest_entries_are_kept(void) {
    ZylCallLog *log = zyl_calllog_open(NULL);
    for (int i = 0; i < ZYL_CALLLOG_MAX + 5; i++) zyl_calllog_record(log, "1", false, i, 0, i);
    assert(zyl_calllog_count(log) == ZYL_CALLLOG_MAX);
    assert(zyl_calllog_get(log, ZYL_CALLLOG_MAX - 1)->time_ms == 5);   /* oldest five dropped */
    zyl_calllog_free(log);
}

static void test_the_log_survives_a_restart_and_entries_can_be_deleted(void) {
    assert(system("rm -rf /tmp/zyl-calllog-test") == 0);
    ZylCallLog *log = zyl_calllog_open(FILE_);
    uint64_t id = zyl_calllog_record(log, "+82 10", true, 1000, 0, 2000)->id;
    zyl_calllog_record(log, "02", false, 3000, 4000, 9000);
    zyl_calllog_free(log);
    log = zyl_calllog_open(FILE_);
    assert(zyl_calllog_count(log) == 2 && strcmp(zyl_calllog_get(log, 1)->number, "+82 10") == 0);
    assert(zyl_calllog_delete(log, id) && !zyl_calllog_delete(log, id));
    zyl_calllog_free(log);
    log = zyl_calllog_open(FILE_);
    assert(zyl_calllog_count(log) == 1);
    zyl_calllog_free(log);
    assert(system("rm -rf /tmp/zyl-calllog-test") == 0);
}

int main(void) {
    printf("=== Call log ===\n");
    RUN_TEST(test_what_a_call_was_follows_from_its_states);
    RUN_TEST(test_ids_are_unique_even_for_calls_at_the_same_time);
    RUN_TEST(test_only_the_latest_entries_are_kept);
    RUN_TEST(test_the_log_survives_a_restart_and_entries_can_be_deleted);
    printf("Results: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
