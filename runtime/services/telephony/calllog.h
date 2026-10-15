/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: 통화 기록 — 끝난 통화를 무엇이었는지 판정하고 보관
 * 수행범위: 발신/수신/부재중 판정(통화가 거친 상태로), 통화 시간, 고유 id,
 *           최근 500건 영속(원자적 저장), 삭제
 * 의존방향: glib (GKeyFile). 모뎀·D-Bus 비의존
 * SOLID: SRP — 판정·보관만. 통화 상태 추적은 telephony.c, 표시는 phone 앱 몫
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_CALLLOG_H
#define ZYL_CALLLOG_H

#include <stdbool.h>
#include <stdint.h>

#define ZYL_CALLLOG_MAX 500

typedef struct {
    uint64_t id;
    int64_t  time_ms;        /* when the call began */
    uint32_t duration;       /* seconds talking; 0 when never answered */
    char     type[12];       /* "outgoing", "incoming", "missed" */
    char     number[32];
} ZylCallLogEntry;

typedef struct ZylCallLog ZylCallLog;

/* path: state file, or NULL to keep the log in memory only */
ZylCallLog *zyl_calllog_open(const char *path);
void        zyl_calllog_free(ZylCallLog *log);

/* A call has ended. Outgoing unless it began ringing; a ringing call is
   incoming once answered (answered_ms != 0), missed otherwise */
const ZylCallLogEntry *zyl_calllog_record(ZylCallLog *log, const char *number,
                                          bool incoming, int64_t start_ms,
                                          int64_t answered_ms, int64_t end_ms);

unsigned               zyl_calllog_count(const ZylCallLog *log);
const ZylCallLogEntry *zyl_calllog_get(const ZylCallLog *log, unsigned i);   /* 0 = newest */
bool                   zyl_calllog_delete(ZylCallLog *log, uint64_t id);

#endif /* ZYL_CALLLOG_H */
