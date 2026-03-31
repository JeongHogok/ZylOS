/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 알람/스케줄러 서비스 인터페이스 — 앱이 미래 시각에 트리거를 등록
 * 수행범위: ZylAlarm 타입 정의, Set/Cancel/List API, D-Bus 상수
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 알람 관련 인터페이스만 노출
 *
 * D-Bus: org.zylos.AlarmService / /org/zylos/AlarmService
 *   메서드: Set(s tag, t trigger_at, t interval_ms, s app_id, b repeating) → b ok
 *           Cancel(s tag) → b ok
 *           List() → a(sttsb) alarms
 *   시그널: AlarmTriggered(s tag, s app_id)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_ALARM_H
#define ZYL_ALARM_H

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>

/* ─── D-Bus 상수 ─── */
#define ZYL_ALARM_DBUS_NAME   "org.zylos.AlarmService"
#define ZYL_ALARM_DBUS_PATH   "/org/zylos/AlarmService"

/* ─── 알람 레코드 ─── */
typedef struct {
    char    *tag;           /* 앱이 지정한 유니크 식별자 */
    uint64_t trigger_at;    /* 트리거 시각 (epoch ms) */
    uint64_t interval_ms;   /* 반복 간격 ms (0 = 1회성) */
    char    *app_id;        /* 알람을 등록한 앱 ID */
    bool     repeating;     /* 반복 여부 */
} ZylAlarm;

/* ─── 서비스 불투명 타입 ─── */
typedef struct ZylAlarmService ZylAlarmService;

/* ─── 서비스 생성/해제 ─── */
ZylAlarmService *zyl_alarm_service_create(void);
void             zyl_alarm_service_destroy(ZylAlarmService *svc);

/* ─── 공개 API ─── */

/**
 * 알람을 등록하거나 기존 tag의 알람을 갱신한다.
 * 반환: 0 성공, -1 실패
 */
int zyl_alarm_set(ZylAlarmService *svc, const ZylAlarm *alarm);

/**
 * tag에 해당하는 알람을 취소한다.
 * 반환: 0 성공, -1 알람 없음
 */
int zyl_alarm_cancel(ZylAlarmService *svc, const char *tag);

/**
 * 현재 등록된 모든 알람 목록을 반환한다.
 * *out 는 호출자가 zyl_alarm_list_free()로 해제해야 한다.
 * 반환: 0 성공, -1 실패
 */
int zyl_alarm_list(ZylAlarmService *svc, ZylAlarm **out, int *count);

/** zyl_alarm_list가 할당한 배열을 해제한다. */
void zyl_alarm_list_free(ZylAlarm *alarms, int count);

#endif /* ZYL_ALARM_H */
