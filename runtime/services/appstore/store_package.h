/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 패키지(zip) 풀기와 확인 — 서명이 검증된 패키지만 여기 온다
 * 수행범위: 일반 파일·폴더만(절대 경로·..·링크·장치 거부), 개수·크기 상한,
 *           app.json 이 카탈로그와 같은 id·버전이고 진입 파일이 있는지
 * 의존방향: glib, libzip, json-glib
 * SOLID: SRP — 패키지 내용 처리만
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_STORE_PACKAGE_H
#define ZYL_STORE_PACKAGE_H

#include <stdbool.h>

#define ZYL_STORE_MAX_FILES     4096
#define ZYL_STORE_MAX_UNPACKED  (200 * 1024 * 1024)

/* why: set to a short reason on failure only (static string) */
bool zyl_store_unpack(const char *zip_path, const char *dir, const char **why);

bool zyl_store_check_manifest(const char *dir, const char *id, const char *version, const char **why);

#endif /* ZYL_STORE_PACKAGE_H */
