/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 알람 서비스(runtime/services/alarm) — 앱별 알람과 지워진 앱의 정리 테스트
 * 수행범위: 알람은 (앱, tag) — 같은 tag 라도 다른 앱의 것을 덮거나 취소하지 못하고 목록에
 *           보이지 않음, forget_app 이 그 앱의 알람만 모두 지우고 저장 파일에도 남기지 않음
 * 의존방향: alarm.c (ZYL_TEST), glib, json-glib
 * SOLID: SRP — 앱 삭제 시 알람 정리 규칙만 검증
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-alarm-test"
#define ALARM_PERSIST_DIR ROOT_
#include "../runtime/services/alarm/alarm.c"
#include <assert.h>

static void add(ZylAlarmService *svc, const char *tag, const char *app) {
    ZylAlarm a = { .tag = (char *)tag, .trigger_at = now_ms() + 3600000, .interval_ms = 0,
                   .app_id = (char *)app, .repeating = false };
    assert(zyl_alarm_set(svc, &a) == 0);
}

int main(void) {
    printf("=== alarm tests ===\n");
    assert(system("rm -rf " ROOT_) == 0);
    g_mkdir_with_parents(ROOT_, 0700);
    ZylAlarmService *svc = zyl_alarm_service_create();
    add(svc, "wake", "com.zylos.clock");
    add(svc, "a1", "com.example.gone");
    add(svc, "a2", "com.example.gone");
    assert(zyl_alarm_forget_app(svc, "com.example.gone") == 2);
    ZylAlarm *list = NULL;
    int n = 0;
    assert(zyl_alarm_list(svc, NULL, &list, &n) == 0 && n == 1 && strcmp(list[0].tag, "wake") == 0);
    zyl_alarm_list_free(list, n);
    gchar *saved = NULL;
    assert(g_file_get_contents(ROOT_ "/alarms.json", &saved, NULL, NULL));
    assert(!strstr(saved, "com.example.gone"));                 /* not on disk either */
    g_free(saved);
    assert(zyl_alarm_forget_app(svc, "com.example.none") == 0 && zyl_alarm_forget_app(NULL, "x") == 0);
    printf("  test_forget_app_removes_only_that_app                      PASS\n");

    /* Another app's "wake" is another alarm: not replaced, not cancelled, not listed */
    add(svc, "wake", "com.example.evil");
    assert(zyl_alarm_list(svc, NULL, &list, &n) == 0 && n == 2);
    zyl_alarm_list_free(list, n);
    assert(zyl_alarm_cancel(svc, "com.example.evil", "wake") == 0);
    assert(zyl_alarm_cancel(svc, "com.example.evil", "wake") == -1);      /* its own is gone … */
    assert(zyl_alarm_list(svc, "com.zylos.clock", &list, &n) == 0 && n == 1);   /* … the clock's is not */
    zyl_alarm_list_free(list, n);
    add(svc, "x", "com.example.other");
    assert(zyl_alarm_list(svc, "com.example.other", &list, &n) == 0 && n == 1 &&
           strcmp(list[0].app_id, "com.example.other") == 0);
    zyl_alarm_list_free(list, n);
    printf("  test_an_alarm_is_its_apps_tag                              PASS\n=== 2/2 passed ===\n");
    zyl_alarm_service_destroy(svc);
    assert(system("rm -rf " ROOT_) == 0);
    return 0;
}
