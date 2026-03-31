/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Interface
 *
 * 역할: Zygote 패턴 — 사전 fork된 WebKitGTK 프로세스 풀
 * 수행범위: pre-fork, 앱 launch 시 fork → setuid → sandbox → 앱 로드
 * 의존방향: lifecycle.h, sandbox.h
 * SOLID: SRP — 프로세스 풀 관리만 담당
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_ZYGOTE_H
#define ZYL_ZYGOTE_H

#include <stdbool.h>
#include <sys/types.h>

#define ZYL_ZYGOTE_POOL_SIZE 3  /* 사전 fork할 프로세스 수 */

typedef struct ZylZygote ZylZygote;

/**
 * Zygote 초기화 — WebKitGTK 초기화 후 POOL_SIZE개 프로세스 사전 fork.
 * 각 자식은 파이프로 대기하다가 launch 명령 수신 시 앱 로드.
 */
ZylZygote *zyl_zygote_create(void);
void       zyl_zygote_destroy(ZylZygote *zyg);

/**
 * 앱 launch — 풀에서 사전 fork된 프로세스를 꺼내 앱 로드.
 * 풀이 비면 새로 fork. 비동기적으로 풀 보충.
 *
 * @param app_id  앱 식별자 (UID 조회 + sandbox 적용)
 * @param app_url 앱 HTML 진입점 URL
 * @return 자식 PID, 또는 실패 시 -1
 */
pid_t zyl_zygote_launch(ZylZygote *zyg, const char *app_id,
                         const char *app_url);

/**
 * 풀 상태 조회.
 */
int zyl_zygote_pool_available(const ZylZygote *zyg);

#endif /* ZYL_ZYGOTE_H */
