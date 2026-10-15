/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 컨테이너 — 앱 하나의 모든 것이 사는 /data/appdata/<앱 id>/
 * 수행범위: 앱 id 검증(디렉터리 이름이 되므로), 컨테이너·하위 디렉터리 경로와
 *           만들기 — 루트(WAM 소유)와 같은 소유로, WAM 이 앱의 웹 저장소를
 *           계속 둘 수 있게. 쓰는 곳: appdata(비공개 데이터), fs(제3자 앱 파일),
 *           WAM(webkit), appstore(삭제 때 통째로 제거)
 * 의존방향: glib
 * SOLID: SRP — 컨테이너 위치 규칙만
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_CONTAINER_H
#define ZYL_CONTAINER_H

#include <stdbool.h>

#define ZYL_CONTAINERS "/data/appdata"

/* Letters, digits, . _ - only, no "." or ".." — it names a directory */
bool zyl_container_valid_id(const char *id);

/* <root>/<id>[/<sub>]; made (0700) when make. NULL for a bad id or when it
   cannot be made. */
char *zyl_container_dir(const char *root, const char *id, const char *sub, bool make);

#endif /* ZYL_CONTAINER_H */
