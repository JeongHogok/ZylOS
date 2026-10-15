/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: OOM Killer 인터페이스 — 메모리 압박 감지 및 앱 종료 정책
 * 수행범위: 주기적 pressure 모니터링,
 *           포그라운드 보호, LRU 기반 백그라운드 앱 종료
 * 의존방향: lifecycle.h
 * SOLID: ISP — OOM 정책 인터페이스만 노출, SRP — 메모리 관리만 담당
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_OOM_H
#define ZYL_WAM_OOM_H

#include <glib.h>
#include <stdbool.h>
#include "../lifecycle/lifecycle.h"

/* ─── Memory pressure 임계값 (MemAvailable / MemTotal %) ─── */
#define ZYL_OOM_CRITICAL_PCT   10   /* 즉시 종료 */
#define ZYL_OOM_MODERATE_PCT   15   /* 상태 저장 알림 후 종료 */
#define ZYL_OOM_WARNING_PCT    25   /* 경고 로그만 */

/* ─── 주기적 모니터링 간격 (밀리초) ─── */
#define ZYL_OOM_POLL_INTERVAL_MS   5000  /* 5초 */

/* ─── 상태 저장 대기 시간 (밀리초) ─── */
#define ZYL_OOM_SAVE_GRACE_MS      500   /* 0.5초 */

/* ─── OOM Killer 컨텍스트 ─── */
typedef struct {
    ZylAppInterface *iface;          /* 앱 인터페이스 (lookup/close) */
    GQueue          *lru_order;      /* LRU 큐: head=가장 오래된 bg앱 */
    char            *foreground_id;  /* 현재 포그라운드 앱 ID (보호 대상) */
    guint            poll_source_id; /* GLib 타이머 소스 ID */
} ZylOomKiller;

/**
 * OOM Killer 초기화 — 주기적 모니터링 타이머 시작.
 */
ZylOomKiller *zyl_oom_init(ZylAppInterface *iface);

/**
 * OOM Killer 해제. 타이머 해제, LRU 큐 정리.
 */
void zyl_oom_destroy(ZylOomKiller *oom);

/* 앱 실행 시 호출 — LRU 큐에 추가. 앱별 메모리 상한은 없다: 앱 코드는 WebKit 이
 * 띄우는 웹 프로세스에서 돌고 그 PID 를 WAM 이 알 길이 없어 cgroup 으로 가둘 수
 * 없다. 압박 때 어느 앱을 닫을지가 WAM 이 쥔 유일한 지렛대다. */
void zyl_oom_on_app_launched(ZylOomKiller *oom, const char *app_id);

/**
 * 앱 종료 시 호출 — LRU 큐에서 제거.
 */
void zyl_oom_on_app_closed(ZylOomKiller *oom, const char *app_id);

/**
 * 앱이 포그라운드로 전환 시 호출 — 포그라운드 보호 + LRU 업데이트.
 */
void zyl_oom_on_app_foreground(ZylOomKiller *oom, const char *app_id);

/**
 * 즉시 메모리 압박 체크 (앱 launch 직후 등에서 호출).
 */
void zyl_oom_check_pressure(ZylOomKiller *oom);

#endif /* ZYL_WAM_OOM_H */
