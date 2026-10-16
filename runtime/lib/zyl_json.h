/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Library
 *
 * 역할: JSON 객체에서 문자열 멤버를 읽는 단 하나의 방법
 * 수행범위: zyl_json_str — 멤버가 없거나, null 이거나, 문자열이 아니면 기본값.
 *           json-glib 의 json_object_get_string_member_with_default 는 다른 타입의
 *           멤버("id": 1)에 기본값이 아니라 NULL 을 돌려줘서, 앱·패키지·서버가 쓴
 *           JSON 한 줄이 그 값을 쓰는 서비스를 죽였다(푸시 스트림이 재접속마다).
 *           runtime/ 은 문자열 멤버를 이것으로만 읽는다(tests/verify-all.sh 가 검사)
 * 의존방향: json-glib (헤더만)
 * SOLID: SRP — 문자열 멤버 읽기만
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_JSON_H
#define ZYL_JSON_H

#include <json-glib/json-glib.h>

/* The string member key of o, or def when o is NULL or the member is absent,
   null or not a string. Borrowed from o. */
static inline const char *zyl_json_str(JsonObject *o, const char *key, const char *def) {
    JsonNode *n = o ? json_object_get_member(o, key) : NULL;
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
        ? json_node_get_string(n) : def;
}

#endif /* ZYL_JSON_H */
