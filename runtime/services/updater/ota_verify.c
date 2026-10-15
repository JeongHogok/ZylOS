/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Policy
 *
 * 역할: OTA 패키지 신뢰 판정 구현
 * 수행범위: ota_verify.h 참고. 패키지는 한 번만 읽어 해시를 구한다
 * 의존방향: ota_verify.h, OpenSSL (EVP, PEM)
 * SOLID: SRP — 판정만
 * ────────────────────────────────────────────────────────── */

#include "ota_verify.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

static bool sha256_file(const char *path, unsigned char out[EVP_MAX_MD_SIZE]) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1;
    unsigned char buf[8192];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), f)) > 0) ok = EVP_DigestUpdate(ctx, buf, n) == 1;
    unsigned int len = 0;
    ok = ok && !ferror(f) && EVP_DigestFinal_ex(ctx, out, &len) == 1 && len == 32;
    EVP_MD_CTX_free(ctx);
    fclose(f);
    return ok;
}

/* key: taken (freed here) */
static ZylOtaVerdict verify_with(const char *pkg_path, const char *sha256_hex,
                                 const char *signature_b64, EVP_PKEY *key) {
    if (!signature_b64 || !*signature_b64) { EVP_PKEY_free(key); return ZYL_OTA_UNSIGNED; }
    unsigned char digest[EVP_MAX_MD_SIZE];
    if (!sha256_file(pkg_path, digest)) { EVP_PKEY_free(key); return ZYL_OTA_UNREADABLE; }

    if (sha256_hex && *sha256_hex) {
        char hex[65];
        for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
        if (strncmp(hex, sha256_hex, 64) != 0 || strlen(sha256_hex) != 64) {
            EVP_PKEY_free(key);
            return ZYL_OTA_HASH_MISMATCH;
        }
    }

    size_t b64_len = strlen(signature_b64);
    unsigned char *sig = malloc(b64_len * 3 / 4 + 4);
    int len = 0, tail = 0;
    EVP_ENCODE_CTX *dec = EVP_ENCODE_CTX_new();
    EVP_DecodeInit(dec);
    bool decoded = sig && EVP_DecodeUpdate(dec, sig, &len, (const unsigned char *)signature_b64,
                                           (int)b64_len) >= 0 &&
                   EVP_DecodeFinal(dec, sig + len, &tail) == 1;
    EVP_ENCODE_CTX_free(dec);

    /* RSA-SHA256 over the package's SHA-256 (tools/build-ota.sh signs that) */
    EVP_MD_CTX *vfy = EVP_MD_CTX_new();
    bool good = decoded && vfy &&
                EVP_DigestVerifyInit(vfy, NULL, EVP_sha256(), NULL, key) == 1 &&
                EVP_DigestVerifyUpdate(vfy, digest, 32) == 1 &&
                EVP_DigestVerifyFinal(vfy, sig, (size_t)(len + tail)) == 1;
    EVP_MD_CTX_free(vfy);
    EVP_PKEY_free(key);
    free(sig);
    return good ? ZYL_OTA_TRUSTED : ZYL_OTA_BAD_SIGNATURE;
}

ZylOtaVerdict zyl_ota_verify(const char *pkg_path, const char *sha256_hex,
                             const char *signature_b64, const char *pubkey_path) {
    if (!signature_b64 || !*signature_b64) return ZYL_OTA_UNSIGNED;
    FILE *kf = fopen(pubkey_path, "r");
    if (!kf) return ZYL_OTA_NO_KEY;
    EVP_PKEY *key = PEM_read_PUBKEY(kf, NULL, NULL, NULL);
    fclose(kf);
    if (!key) return ZYL_OTA_BAD_KEY;
    return verify_with(pkg_path, sha256_hex, signature_b64, key);
}

ZylOtaVerdict zyl_ota_verify_pem(const char *pkg_path, const char *sha256_hex,
                                 const char *signature_b64, const char *pubkey_pem) {
    if (!signature_b64 || !*signature_b64) return ZYL_OTA_UNSIGNED;
    if (!pubkey_pem || !*pubkey_pem) return ZYL_OTA_NO_KEY;
    BIO *bio = BIO_new_mem_buf(pubkey_pem, -1);
    EVP_PKEY *key = bio ? PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL) : NULL;
    BIO_free(bio);
    if (!key) return ZYL_OTA_BAD_KEY;
    return verify_with(pkg_path, sha256_hex, signature_b64, key);
}

const char *zyl_ota_verdict_message(ZylOtaVerdict v) {
    switch (v) {
    case ZYL_OTA_TRUSTED:       return "Package verified";
    case ZYL_OTA_UNSIGNED:      return "Unsigned update rejected";
    case ZYL_OTA_NO_KEY:        return "OTA signing key missing";
    case ZYL_OTA_BAD_KEY:       return "Invalid OTA signing key";
    case ZYL_OTA_UNREADABLE:    return "Package cannot be read";
    case ZYL_OTA_HASH_MISMATCH: return "Package integrity check failed";
    default:                    return "Signature verification failed";
    }
}
