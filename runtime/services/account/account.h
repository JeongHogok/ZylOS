/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 계정 서비스 인터페이스 — OAuth 2.0 + 로컬 계정 관리
 * 수행범위: 계정 등록/로그인/토큰 갱신, 프로필 동기화
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 계정 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_ACCOUNT_H
#define ZYL_ACCOUNT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ZYL_ACCOUNT_LOCAL  = 0,   /* 로컬 전용 (동기화 없음) */
    ZYL_ACCOUNT_CLOUD  = 1,   /* 클라우드 계정 (동기화 가능) */
} ZylAccountType;

typedef struct {
    char *account_id;          /* 고유 ID */
    char *display_name;        /* 표시 이름 */
    char *email;               /* 이메일 (클라우드 계정) */
    ZylAccountType type;
    bool is_active;            /* 현재 활성 계정 */
    uint64_t created_at;
    uint64_t last_sync;        /* 마지막 동기화 시각 (0 = 미동기화) */
} ZylAccountInfo;

typedef struct ZylAccountService ZylAccountService;

ZylAccountService *zyl_account_create(void);
void               zyl_account_destroy(ZylAccountService *svc);

/* 로컬 계정 */
int  zyl_account_register_local(ZylAccountService *svc,
                                 const char *name, const char *pin);
int  zyl_account_login_local(ZylAccountService *svc, const char *pin);

/* 클라우드 계정 (OAuth 2.0) */
int  zyl_account_login_oauth(ZylAccountService *svc,
                              const char *provider, /* "google", "github" */
                              const char *auth_code);
int  zyl_account_refresh_token(ZylAccountService *svc);
int  zyl_account_logout(ZylAccountService *svc);

/* 계정 정보 */
ZylAccountInfo *zyl_account_get_current(ZylAccountService *svc);
int  zyl_account_list(ZylAccountService *svc, ZylAccountInfo **out,
                       int *count);
void zyl_account_info_free(ZylAccountInfo *info, int count);

/* 동기화 */
int  zyl_account_sync_now(ZylAccountService *svc);
int  zyl_account_set_auto_sync(ZylAccountService *svc, bool enabled,
                                 int interval_min);

/* 백업 */
int  zyl_account_backup(ZylAccountService *svc, const char *output_path);
int  zyl_account_restore(ZylAccountService *svc, const char *backup_path);

#define ZYL_ACCOUNT_DBUS_NAME "org.zylos.AccountService"
#define ZYL_ACCOUNT_DBUS_PATH "/org/zylos/AccountService"

#endif /* ZYL_ACCOUNT_H */
