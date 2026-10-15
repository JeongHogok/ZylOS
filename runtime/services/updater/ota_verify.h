/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: OTA 패키지 신뢰 판정 — 무결성(SHA-256)과 출처(RSA-SHA256 서명)
 * 수행범위: 패키지 SHA-256 계산, 매니페스트 해시와 비교(주어졌을 때), 이미지에
 *           든 공개키로 "패키지 SHA-256 에 대한 서명" 확인. 서명 없으면 거부
 *           (tools/build-ota.sh 가 만드는 형식)
 * 의존방향: OpenSSL (EVP)
 * SOLID: SRP — 판정만. 내려받기·상태는 updater.c, 기록은 ota_install.c
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_OTA_VERIFY_H
#define ZYL_OTA_VERIFY_H

typedef enum {
    ZYL_OTA_TRUSTED,
    ZYL_OTA_UNSIGNED,          /* no signature: never installed */
    ZYL_OTA_NO_KEY,            /* the image ships no OTA key */
    ZYL_OTA_BAD_KEY,
    ZYL_OTA_UNREADABLE,
    ZYL_OTA_HASH_MISMATCH,     /* corrupt or not the package the manifest names */
    ZYL_OTA_BAD_SIGNATURE,     /* not signed by the key: tampered */
} ZylOtaVerdict;

/* sha256_hex may be NULL or "" (the signature still decides) */
ZylOtaVerdict zyl_ota_verify(const char *pkg_path, const char *sha256_hex,
                             const char *signature_b64, const char *pubkey_path);
/* The same check with the public key given as PEM text (the app store's key) */
ZylOtaVerdict zyl_ota_verify_pem(const char *pkg_path, const char *sha256_hex,
                                 const char *signature_b64, const char *pubkey_pem);
const char   *zyl_ota_verdict_message(ZylOtaVerdict v);

#endif /* ZYL_OTA_VERIFY_H */
