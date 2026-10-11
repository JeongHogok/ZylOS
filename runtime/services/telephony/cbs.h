/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: 재난문자(Cell Broadcast) 정책 — 채널 분류, 수신 채널 목록, 경보 기록
 * 수행범위: 3GPP TS 23.041 메시지 식별자(ETWS/CMAS)를 경보 등급으로 분류,
 *           모뎀에 설정할 채널 범위, 재전송(채널·메시지 코드·업데이트 번호
 *           동일) 24시간 내 무시, 기록 50건 영속과 사용자 확인 표시
 * 의존방향: glib (GKeyFile). 모뎀·D-Bus 비의존
 * SOLID: SRP — 분류·기록만. 수신은 telephony.c, 표시는 alerts 앱 몫
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_CBS_H
#define ZYL_CBS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ZYL_CBS_NONE = -1,        /* not an alert we show (geo-fencing trigger, other) */
    ZYL_CBS_PRESIDENTIAL,     /* national alert — cannot be turned off */
    ZYL_CBS_EXTREME,
    ZYL_CBS_SEVERE,
    ZYL_CBS_AMBER,            /* child abduction / missing person */
    ZYL_CBS_PUBLIC_SAFETY,
    ZYL_CBS_EARTHQUAKE,       /* ETWS earthquake / tsunami */
    ZYL_CBS_TEST              /* monthly test, exercise, operator, state/local test */
} ZylCbsCategory;

typedef struct { uint16_t from, to; } ZylCbsRange;

/* Ranges the modem must listen to, terminated by {0, 0} */
extern const ZylCbsRange ZYL_CBS_CHANNELS[];

ZylCbsCategory zyl_cbs_classify(unsigned channel);

/* Stable name the OS UI translates: "presidential", "extreme", ... */
const char *zyl_cbs_category_name(ZylCbsCategory c);
ZylCbsCategory zyl_cbs_category_from_name(const char *name);
#define ZYL_CBS_CATEGORIES (ZYL_CBS_TEST + 1)

/* ─── Alert history ─── */
#define ZYL_CBS_HISTORY_MAX 50

typedef struct {
    uint64_t       id;
    int64_t        at;        /* seconds since the epoch */
    unsigned       channel, code, update;
    ZylCbsCategory category;
    char          *text;
    bool           acked;     /* the user dismissed the full-screen alert */
} ZylCbsAlert;

typedef struct ZylCbsStore ZylCbsStore;

/* path: state file, or NULL to keep the history in memory only */
ZylCbsStore *zyl_cbs_store_open(const char *path);
void         zyl_cbs_store_free(ZylCbsStore *s);

/* A received message. Returns the new alert, or NULL when it is not an alert,
   the user turned its category off, or the network is repeating one already
   shown. */
const ZylCbsAlert *zyl_cbs_store_receive(ZylCbsStore *s, unsigned channel,
                                         unsigned code, unsigned update,
                                         const char *text, int64_t now);

bool zyl_cbs_store_ack(ZylCbsStore *s, uint64_t id);

/* Which categories the user receives. Presidential alerts cannot be turned
   off (set returns false); test alerts are off until asked for. */
bool zyl_cbs_store_enabled(const ZylCbsStore *s, ZylCbsCategory c);
bool zyl_cbs_store_set_enabled(ZylCbsStore *s, ZylCbsCategory c, bool on);

/* Newest first; index < count */
int                zyl_cbs_store_count(const ZylCbsStore *s);
const ZylCbsAlert *zyl_cbs_store_get(const ZylCbsStore *s, int index);

#endif /* ZYL_CBS_H */
