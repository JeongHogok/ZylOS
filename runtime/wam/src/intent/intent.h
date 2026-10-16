/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 인텐트 해석 — 앱이 시작한 인텐트를 어느 앱이 받는지
 * 수행범위: 명시적(targetApp) 또는 암시적(action + mimeType 을 각 app.json
 *           "intents" 와 비교, 시작한 앱 자신은 제외 — PICK 은 시스템 앱만), 받는
 *           앱에 넘길 내용(건넬 파일 files 포함), 앱 페이지에 메시지를 넣는 스크립트.
 *           같은 규칙: apps/system/intent.js
 * 의존방향: glib, json-glib, manifest.h
 * SOLID: SRP — 해석만. 실행·선택 상태는 WAM(wam.c)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_WAM_INTENT_H
#define ZYL_WAM_INTENT_H

#include <glib.h>
#include <json-glib/json-glib.h>
#include "../manifest/manifest.h"

/* Asks the user for something and gives back what was chosen (intent.result).
   Only a system app handles it: what is picked is picked on an OS screen */
#define ZYL_INTENT_PICK "zyl.intent.action.PICK"

/* The files an intent hands over, at most this many (paths in the sender's root) */
#define ZYL_INTENT_MAX_FILES 10

/* Any ("*" slash "*"), exact, or a type with any subtype ("image" slash "*") */
gboolean zyl_intent_mime_matches(const char *filter, const char *mime);

/* Ids of the apps that handle it, sorted (strings borrowed from the manifests) */
GPtrArray *zyl_intent_resolve(GHashTable *manifests, const char *source, JsonObject *intent);

/* What the handler gets: action, data, mimeType, extras, files (strings only,
   at most ZYL_INTENT_MAX_FILES) and sourceApp — the OS's, never the request's.
   New object. */
JsonObject *zyl_intent_deliverable(JsonObject *intent, const char *source);

/* window.postMessage({ type, data }) for an app's page; data is taken */
char *zyl_intent_post_script(const char *type, JsonNode *data);

/* The files member of a deliverable or result: a new array of the strings in
   it, ZYL_INTENT_MAX_FILES at most — anything else is dropped */
JsonArray *zyl_intent_files(JsonObject *o);

/* A node holding obj, taking the caller's reference */
JsonNode *zyl_intent_object_node(JsonObject *obj);

#endif /* ZYL_WAM_INTENT_H */
