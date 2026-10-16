/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Library
 *
 * 역할: 서비스가 설정 한 범주를 따라가기 (org.zylos.Settings)
 * 수행범위: 범주를 읽어 콜백에 넘기고, CategoryChanged 때마다 다시 읽는다.
 *           설정 서비스가 늦게 뜨거나 다시 떠도(이름이 나타날 때) 다시 읽고, 없으면
 *           NULL 로 알린다(기본값으로 동작하라는 뜻) — 사용자가 빠른 설정에서 바꾼
 *           스위치(위치, 비행기 모드)를 그 일을 하는 서비스가 지키게
 * 의존방향: gio (D-Bus)
 * SOLID: SRP — 설정 범주 구독만
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_SETTINGS_FOLLOW_H
#define ZYL_SETTINGS_FOLLOW_H

#include <stdbool.h>
#include <gio/gio.h>

/* values: the category as a{sv} (borrowed), or NULL when no settings service
   has ever answered — then the service runs on its defaults. Once one has, the
   settings service restarting keeps the last values (airplane mode does not
   switch the radio on for the moment it is gone) */
typedef void (*ZylSettingsFn)(GVariant *values, gpointer data);

typedef struct ZylSettingsFollow ZylSettingsFollow;

/* Calls fn now (once the settings service answers, or with NULL when it is
   not on the bus and never was) and after every change of category, until unfollowed */
ZylSettingsFollow *zyl_settings_follow(GDBusConnection *conn, const char *category,
                                       ZylSettingsFn fn, gpointer data);

/* No more calls to fn, not even for an answer already on its way. NULL is fine */
void zyl_settings_unfollow(ZylSettingsFollow *f);

/* A boolean of values, or def when values is NULL or the key is absent or not a boolean */
bool zyl_settings_bool(GVariant *values, const char *key, bool def);

#endif /* ZYL_SETTINGS_FOLLOW_H */
