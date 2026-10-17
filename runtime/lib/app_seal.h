/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 봉인 — 설치된 앱의 코드가 서명된 그대로인지
 * 수행범위: 2단 키 — 이미지에 든 루트 키(오프라인)가 서명 키 인증서(signing.json:
 *           일련번호·공개키·앞서 폐기한 일련번호)를 서명하고, 그 서명 키가 카탈로그·
 *           패키지·봉인·개발자 인증서·프로필을 서명한다. 봉인은 자기 서명 키 인증서를
 *           함께 담는다(.zyl-seal/signing.json) — 등록 때 스토어 없이 판정된다.
 *           .zyl-seal/seal.json(파일마다 SHA-256) 의 서명 확인 — 공식 앱은 서명 키,
 *           개발 앱은 서명 키가 서명한 개발자 인증서(만료·폐기)와 프로비저닝
 *           프로필(앱 id 패턴·이 기기의 ID·만료)이 허락한 개발자 키. 회수: 폐기된
 *           서명 키 일련번호·개발자 인증서·앱 id(스토어가 마지막에 말한 것 — 이 파일이
 *           그 저장 형식의 주인). 봉인된 파일 집합과 실제 파일이 정확히 같은지, 내보내는
 *           파일 하나하나의 해시가 봉인과 같은지. 쓰는 곳: appstore(설치·회수 저장),
 *           WAM(등록·파일 제공)
 * 의존방향: glib, json-glib, ota_verify.h (RSA-SHA256 over SHA-256)
 * SOLID: SRP — 봉인 판정만
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_APP_SEAL_H
#define ZYL_APP_SEAL_H

#include <stdbool.h>
#include <glib.h>

#define ZYL_SEAL_DIR ".zyl-seal"
/* Where the device keeps what the store revoked: in the user apps directory (/data/apps) */
#define ZYL_SEAL_REVOCATIONS ".revocations.json"

typedef enum {
    ZYL_SEAL_OK,
    ZYL_SEAL_MISSING,          /* no seal: never installed or registered */
    ZYL_SEAL_BAD_SIGNATURE,    /* not signed by the store or an allowed developer */
    ZYL_SEAL_WRONG_APP,        /* the seal is another app's or version's */
    ZYL_SEAL_FILES_CHANGED,    /* a file added, removed or changed after sealing */
    ZYL_SEAL_NOT_PROVISIONED,  /* development build: this app or this device is not in the profile */
    ZYL_SEAL_EXPIRED,          /* development certificate or profile out of date */
    ZYL_SEAL_REVOKED,          /* the app, its developer's certificate or the store key that signed it was revoked */
} ZylSealVerdict;

typedef struct ZylSeal ZylSeal;

/* What the store revoked, as the device keeps it (appstore writes it from each
   catalog newer than the last; everyone else reads it) */
typedef struct {
    gint64  issued;   /* the catalog's "issued": an older one is a replay */
    char  **certs;    /* developer certificates (cert.json sha256, lower hex) — the catalog's list */
    char  **apps;     /* app ids — the catalog's list */
    char  **keys;     /* signing key serials (decimal) — kept for good: a revoked key never comes back */
} ZylRevocations;

/*
 * Open the seal of the app unpacked in app_dir.
 * app_id/version: what app.json says (version NULL: not checked).
 * root_pem: the store's root key (store.json "rootKey"). device_id: this device
 * ("": none — no development build can run). rev: what is revoked, may be NULL.
 * now: seconds since the epoch. full: also hash every file and require exactly
 * the sealed set (install, registration).
 */
ZylSealVerdict zyl_seal_open(const char *app_dir, const char *app_id, const char *version,
                             const char *root_pem, const char *device_id,
                             const ZylRevocations *rev, gint64 now, bool full, ZylSeal **out);

/* The store key a signing certificate vouches for: dir/signing.json signed by
   the root (dir/signing.sig), with a serial rev does not revoke.
   NULL otherwise. revokes (may be NULL): the earlier serials it revokes, for the
   caller to keep (NULL-terminated) */
char *zyl_seal_signing_key(const char *dir, const char *root_pem, const ZylRevocations *rev,
                           char ***revokes);

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

/* The store's root key from its configuration (store.json "rootKey"); NULL when none */
char *zyl_seal_root_key(const char *config_path);

/* What is revoked, from path (never NULL: nothing when the file is not there) */
ZylRevocations *zyl_seal_load_revocations(const char *path);

/* Atomically, 0644 (the WAM reads it) */
bool zyl_seal_save_revocations(const char *path, const ZylRevocations *rev);

void zyl_seal_revocations_free(ZylRevocations *rev);

#endif /* ZYL_APP_SEAL_H */
