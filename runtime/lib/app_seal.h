/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 봉인 — 설치된 앱의 코드가 서명된 그대로인지
 * 수행범위: .zyl-seal/seal.json(파일마다 SHA-256) 의 서명 확인 — 공식 앱은
 *           스토어 키, 개발 앱은 스토어가 서명한 개발자 인증서(만료·폐기)와
 *           프로비저닝 프로필(앱 id 패턴·이 기기의 ID·만료)이 허락한 개발자 키.
 *           봉인된 파일 집합과 실제 파일이 정확히 같은지, 내보내는 파일 하나하나의
 *           해시가 봉인과 같은지. 쓰는 곳: appstore(설치), WAM(등록·파일 제공)
 * 의존방향: glib, json-glib, ota_verify.h (RSA-SHA256 over SHA-256)
 * SOLID: SRP — 봉인 판정만
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_APP_SEAL_H
#define ZYL_APP_SEAL_H

#include <stdbool.h>
#include <glib.h>

#define ZYL_SEAL_DIR ".zyl-seal"

typedef enum {
    ZYL_SEAL_OK,
    ZYL_SEAL_MISSING,          /* no seal: never installed or registered */
    ZYL_SEAL_BAD_SIGNATURE,    /* not signed by the store or an allowed developer */
    ZYL_SEAL_WRONG_APP,        /* the seal is another app's or version's */
    ZYL_SEAL_FILES_CHANGED,    /* a file added, removed or changed after sealing */
    ZYL_SEAL_NOT_PROVISIONED,  /* development build: this app or this device is not in the profile */
    ZYL_SEAL_EXPIRED,          /* development certificate or profile out of date */
    ZYL_SEAL_REVOKED,          /* the developer's certificate was revoked */
} ZylSealVerdict;

typedef struct ZylSeal ZylSeal;

/*
 * Open the seal of the app unpacked in app_dir.
 * app_id/version: what app.json says (version NULL: not checked).
 * store_pem: the store's public key. device_id: this device ("": none — no
 * development build can run). revoked: revoked certificate sha256s, may be NULL.
 * now: seconds since the epoch. full: also hash every file and require exactly
 * the sealed set (install, registration).
 */
ZylSealVerdict zyl_seal_open(const char *app_dir, const char *app_id, const char *version,
                             const char *store_pem, const char *device_id,
                             const char *const *revoked, gint64 now, bool full, ZylSeal **out);

/* rel's bytes are what was sealed (the WAM checks each file as it serves it) */
bool zyl_seal_bytes_ok(const ZylSeal *s, const char *rel, const void *data, gsize len);

/* The developer of a development build; NULL for the store's own */
const char *zyl_seal_developer(const ZylSeal *s);

/* Who signed the app unpacked in app_dir: a development build's developer key
   (its certificate's publicKey), NULL for the store's own. Read, not verified —
   for an installed app (verified when it was installed) or one just verified */
char *zyl_seal_signer_key(const char *app_dir);

void zyl_seal_free(ZylSeal *s);

const char *zyl_seal_verdict_message(ZylSealVerdict v);

/* The store's public key from its configuration (store.json "publicKey"); NULL when none */
char *zyl_seal_store_key(const char *config_path);

/* Revoked developer certificates the store last listed (one sha256 per line); NULL-terminated */
char **zyl_seal_load_revoked(const char *path);

#endif /* ZYL_APP_SEAL_H */
