/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Facts
 *
 * 역할: 기기 사용자와 그 데이터 위치 — 한 곳에만 적는 사실 (CLAUDE.md 8-1)
 * 수행범위: 사용자 루트, 소유자(첫 사용자)의 uid, 소유자의 파일 트리.
 *           user 서비스가 소유자를 이 uid 로 만들고, fs(사용자 파일)·appstore
 *           (InstallFile 의 패키지가 오는 곳)가 같은 트리를 쓴다. 에뮬레이터의
 *           apps/system/services/user.js 는 tests/test_services.js 가 대조한다
 * 의존방향: 없음 (헤더만)
 * SOLID: SRP — 위치 사실만
 * shortcut: fs·appstore 는 소유자의 파일만 안다 — 다른 사용자로 전환하면 그
 *           사용자의 트리를 따라가야 한다 (다중 사용자를 출시할 때)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_USERS_H
#define ZYL_USERS_H

#ifndef ZYL_USERS_ROOT
#define ZYL_USERS_ROOT   "/data/users"
#endif
#define ZYL_OWNER_UID    1000
#define ZYL_OWNER_FILES  ZYL_USERS_ROOT "/1000/files"   /* ZYL_OWNER_UID's */

#endif /* ZYL_USERS_H */
