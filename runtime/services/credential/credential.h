/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 인증 정보 관리 인터페이스 — 암호화 저장소, 키체인
 * 수행범위: 비밀번호/토큰/키 저장, 조회, 삭제 (사용자별 격리)
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 인증 정보 관리 인터페이스만 노출
 *
 * 실기기: libsecret (GNOME Keyring) 또는 자체 AES-256-GCM 저장소
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_CREDENTIAL_H
#define ZYL_CREDENTIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ─── 자격 증명 유형 ─── */
typedef enum {
    ZYL_CRED_PASSWORD,     /* 비밀번호 (평문 → 암호화 저장) */
    ZYL_CRED_TOKEN,        /* OAuth/API 토큰 */
    ZYL_CRED_KEY,          /* 암호화 키 (바이너리) */
    ZYL_CRED_CERTIFICATE,  /* X.509 인증서 */
} ZylCredentialType;

/* ─── 저장된 자격 증명 ─── */
typedef struct {
    char *label;           /* 사용자 표시 이름 */
    char *service;         /* 서비스/앱 ID */
    char *account;         /* 계정/사용자명 */
    ZylCredentialType type;
    uint64_t created_at;
    uint64_t modified_at;
} ZylCredentialInfo;

typedef struct ZylCredentialStore ZylCredentialStore;

ZylCredentialStore *zyl_credential_create(const char *store_path);
void                zyl_credential_destroy(ZylCredentialStore *store);

/* 저장: 데이터는 AES-256-GCM으로 암호화되어 디스크에 기록 */
int zyl_credential_store(ZylCredentialStore *store,
                          const char *service, const char *account,
                          ZylCredentialType type,
                          const void *secret, size_t secret_len,
                          const char *label);

/* 조회: 복호화된 비밀을 out_secret에 반환. 호출자가 free() */
int zyl_credential_lookup(ZylCredentialStore *store,
                           const char *service, const char *account,
                           void **out_secret, size_t *out_len);

/* 삭제 */
int zyl_credential_delete(ZylCredentialStore *store,
                           const char *service, const char *account);

/* 서비스에 속한 모든 자격 증명 목록 */
int zyl_credential_list(ZylCredentialStore *store,
                         const char *service,
                         ZylCredentialInfo **out, int *count);

/* 자격 증명 정보 해제 */
void zyl_credential_info_free(ZylCredentialInfo *info, int count);

/* 마스터 키 설정 (기기 잠금 PIN에서 파생) */
int zyl_credential_set_master_key(ZylCredentialStore *store,
                                   const void *key, size_t key_len);

#define ZYL_CREDENTIAL_DBUS_NAME "org.zylos.CredentialManager"
#define ZYL_CREDENTIAL_DBUS_PATH "/org/zylos/CredentialManager"

#endif /* ZYL_CREDENTIAL_H */
