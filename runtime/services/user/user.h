/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 멀티유저 관리 인터페이스 — 사용자 계정, 프로필, 전환
 * 수행범위: 사용자 생성/삭제/전환, 프로필 격리, 앱 데이터 분리
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 사용자 관리 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_USER_H
#define ZYL_USER_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define ZYL_USER_MAX        8
#define ZYL_USER_NAME_MAX   64
#define ZYL_USER_DATA_ROOT  "/data/users"

typedef enum {
    ZYL_USER_TYPE_OWNER,       /* 기기 소유자 (1명만) */
    ZYL_USER_TYPE_STANDARD,    /* 일반 사용자 */
    ZYL_USER_TYPE_GUEST,       /* 게스트 (임시, 재부팅 시 삭제) */
    ZYL_USER_TYPE_RESTRICTED,  /* 제한된 사용자 (키즈 모드 등) */
} ZylUserType;

typedef struct {
    uint32_t uid;
    char name[ZYL_USER_NAME_MAX];
    ZylUserType type;
    char avatar_path[256];     /* 프로필 사진 경로 */
    uint64_t created_at;       /* Unix timestamp */
    uint64_t last_login_at;
    bool is_active;            /* 현재 로그인 상태 */
    char data_dir[256];        /* /data/users/{uid}/ */
} ZylUserProfile;

typedef struct ZylUserService ZylUserService;

ZylUserService *zyl_user_create(void);
void            zyl_user_destroy(ZylUserService *svc);

/* 사용자 관리 */
int  zyl_user_add(ZylUserService *svc, const char *name, ZylUserType type,
                   uint32_t *out_uid);
int  zyl_user_remove(ZylUserService *svc, uint32_t uid);
int  zyl_user_get_profile(ZylUserService *svc, uint32_t uid, ZylUserProfile *out);
int  zyl_user_list(ZylUserService *svc, ZylUserProfile **out, int *count);

/* 사용자 전환 */
int  zyl_user_switch(ZylUserService *svc, uint32_t uid);
uint32_t zyl_user_get_current(const ZylUserService *svc);

/* 데이터 격리: 사용자별 앱 데이터 경로 */
int  zyl_user_get_app_data_path(const ZylUserService *svc, uint32_t uid,
                                 const char *app_id, char *out, size_t out_len);

#define ZYL_USER_DBUS_NAME "org.zylos.UserManager"
#define ZYL_USER_DBUS_PATH "/org/zylos/UserManager"

#endif /* ZYL_USER_H */
