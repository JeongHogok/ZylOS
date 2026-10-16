/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 알람 서비스(runtime/services/alarm) — 앱별 알람과 지워진 앱의 정리 테스트
 * 수행범위: 알람은 (앱, tag) — 같은 tag 라도 다른 앱의 것을 덮거나 취소하지 못하고 목록에
 *           보이지 않음, forget_app 이 그 앱의 알람만 모두 지우고 저장 파일에도 남기지 않음,
 *           때가 된 알람만 울리고(1회성은 사라지고 반복은 지금 뒤의 다음 시각으로) 벽시계
 *           타이머는 가장 이른 알람에 맞춰지고 알람이 없으면 꺼짐
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
    printf("  test_an_alarm_is_its_apps_tag                              PASS\n");
    zyl_alarm_service_destroy(svc);

    /* A repeat wakes the device at most each minute, at least yearly; no app
       holds every slot */
    assert(system("rm -f " ROOT_ "/alarms.json") == 0);
    svc = zyl_alarm_service_create();
    ZylAlarm r = { .tag = (char *)"r", .trigger_at = now_ms() + 3600000, .app_id = (char *)"com.example.a",
                   .repeating = true };
    const uint64_t bad[] = { 0, 1, ZYL_ALARM_MIN_INTERVAL_MS - 1, ZYL_ALARM_MAX_INTERVAL_MS + 1, UINT64_MAX };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        r.interval_ms = bad[i];
        assert(zyl_alarm_set(svc, &r) == -1);
    }
    r.interval_ms = ZYL_ALARM_MIN_INTERVAL_MS;
    assert(zyl_alarm_set(svc, &r) == 0);
    r.interval_ms = 0;                                            /* one-shot: no interval needed */
    r.repeating = false;
    assert(zyl_alarm_set(svc, &r) == 0);
    char tag[16];
    for (int i = 1; i < ZYL_ALARM_MAX_PER_APP; i++) {             /* "r" is one of them */
        g_snprintf(tag, sizeof(tag), "t%d", i);
        add(svc, tag, "com.example.a");
    }
    ZylAlarm more = { .tag = (char *)"one-more", .trigger_at = now_ms() + 3600000, .app_id = (char *)"com.example.a" };
    assert(zyl_alarm_set(svc, &more) == -1);                      /* its share is full … */
    assert(zyl_alarm_set(svc, &r) == 0);                          /* … replacing its own is not adding */
    add(svc, "wake", "com.zylos.clock");                          /* … and other apps still set theirs */
    zyl_alarm_service_destroy(svc);
    printf("  test_limits_keep_any_app_from_flooding_the_device         PASS\n");

    /* Due ones ring; the timer follows the earliest; none is no timer */
    assert(system("rm -f " ROOT_ "/alarms.json") == 0);           /* not the alarms above */
    svc = zyl_alarm_service_create();
    uint64_t now = now_ms();
    ZylAlarm past = { .tag = "once", .trigger_at = now - 5000, .app_id = "com.zylos.clock" };
    ZylAlarm rep = { .tag = "daily", .trigger_at = now - 1000, .interval_ms = 86400000,
                     .app_id = "com.zylos.clock", .repeating = true };
    ZylAlarm later = { .tag = "later", .trigger_at = now + 3600000, .app_id = "com.zylos.clock" };
    struct itimerspec its;
    assert(zyl_alarm_set(svc, &later) == 0);
    assert(timerfd_gettime(svc->tfd, &its) == 0);
    assert(its.it_value.tv_sec >= 3599 && its.it_value.tv_sec <= 3600);   /* armed at it */
    assert(zyl_alarm_set(svc, &past) == 0 && zyl_alarm_set(svc, &rep) == 0);
    assert(fire_due(svc, now) == 2);
    assert(zyl_alarm_list(svc, NULL, &list, &n) == 0 && n == 2);   /* the one-shot is gone */
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].tag, "daily") == 0) assert(list[i].trigger_at == now - 1000 + 86400000);
        else assert(strcmp(list[i].tag, "later") == 0 && list[i].trigger_at == now + 3600000);
    }
    zyl_alarm_list_free(list, n);
    assert(fire_due(svc, now) == 0);                               /* nothing due twice */
    rearm(svc);
    assert(timerfd_gettime(svc->tfd, &its) == 0);
    assert(its.it_value.tv_sec >= 3599 && its.it_value.tv_sec <= 3600);   /* at "later", the earliest */
    assert(zyl_alarm_cancel(svc, "com.zylos.clock", "later") == 0 &&
           zyl_alarm_cancel(svc, "com.zylos.clock", "daily") == 0);
    assert(timerfd_gettime(svc->tfd, &its) == 0 && !its.it_value.tv_sec && !its.it_value.tv_nsec);
    zyl_alarm_service_destroy(svc);
    printf("  test_due_alarms_ring_and_the_timer_follows_the_earliest    PASS\n=== 4/4 passed ===\n");
    assert(system("rm -rf " ROOT_) == 0);
    return 0;
}
