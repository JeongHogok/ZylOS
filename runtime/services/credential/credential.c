#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 인증 정보 암호화 저장소 — AES-256-GCM + PBKDF2 기반 키체인
 * 수행범위: 비밀번호/토큰 암호화 저장, 조회, 삭제, 마스터 키 파생 및 관리
 * 의존방향: credential.h, gio/gio.h, openssl/evp.h
 * SOLID: SRP — 인증 정보 암호화 저장만 담당
 *
 * 저장소 구조: {store_path}/{service}/{account}.enc
 * 파일 포맷:   [salt(16)][iv(12)][ciphertext(N)][tag(16)]
 * 암호화:      AES-256-GCM via OpenSSL EVP API
 * 키 파생:     PBKDF2-HMAC-SHA256, 100,000 iterations
 * ────────────────────────────────────────────────────────── */

#include "credential.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <gio/gio.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#define MASTER_KEY_LEN   32   /* AES-256 = 32 bytes */
#define SALT_LEN         16
#define IV_LEN           12   /* GCM recommended IV length */
#define TAG_LEN          16   /* GCM authentication tag */
#define PBKDF2_ITER      100000

struct ZylCredentialStore {
    char *store_path;
    uint8_t master_key[MASTER_KEY_LEN];
    bool key_set;
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

/* ─── 유틸리티 ─── */

static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static void build_path(char *out, size_t out_len,
                        const char *store, const char *service,
                        const char *account) {
    snprintf(out, out_len, "%s/%s", store, service);
    ensure_dir(out);
    size_t len = strlen(out);
    snprintf(out + len, out_len - len, "/%s.enc", account);
}

/* ─── AES-256-GCM 암호화 ─── */

/**
 * Encrypt plaintext with AES-256-GCM.
 * Output format: [salt(16)][iv(12)][ciphertext(N)][tag(16)]
 * Returns total output length, or -1 on failure.
 */
static int aes_gcm_encrypt(const uint8_t *key, size_t key_len,
                            const uint8_t *plaintext, size_t pt_len,
                            const uint8_t *salt, size_t salt_len,
                            uint8_t **out, size_t *out_len) {
    uint8_t iv[IV_LEN];
    uint8_t tag[TAG_LEN];

    if (RAND_bytes(iv, IV_LEN) != 1) {
        fprintf(stderr, "[Credential] RAND_bytes failed for IV\n");
        return -1;
    }

    /* Derive per-file key from master key + salt using PBKDF2 */
    uint8_t derived_key[MASTER_KEY_LEN];
    if (PKCS5_PBKDF2_HMAC((const char *)key, (int)key_len,
                           salt, (int)salt_len,
                           PBKDF2_ITER, EVP_sha256(),
                           MASTER_KEY_LEN, derived_key) != 1) {
        fprintf(stderr, "[Credential] PBKDF2 key derivation failed\n");
        return -1;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        memset(derived_key, 0, sizeof(derived_key));
        return -1;
    }

    int ret = -1;
    int len = 0;
    int ct_len = 0;

    /* Allocate output: salt + iv + ciphertext + tag */
    size_t total = SALT_LEN + IV_LEN + pt_len + TAG_LEN;
    uint8_t *output = malloc(total);
    if (!output) goto cleanup;

    /* Write salt and IV to output header */
    memcpy(output, salt, SALT_LEN);
    memcpy(output + SALT_LEN, iv, IV_LEN);

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto cleanup;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) != 1)
        goto cleanup;

    if (EVP_EncryptInit_ex(ctx, NULL, NULL, derived_key, iv) != 1)
        goto cleanup;

    if (EVP_EncryptUpdate(ctx, output + SALT_LEN + IV_LEN, &len,
                          plaintext, (int)pt_len) != 1)
        goto cleanup;
    ct_len = len;

    if (EVP_EncryptFinal_ex(ctx, output + SALT_LEN + IV_LEN + ct_len,
                            &len) != 1)
        goto cleanup;
    ct_len += len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN,
                            output + SALT_LEN + IV_LEN + ct_len) != 1)
        goto cleanup;

    *out = output;
    *out_len = SALT_LEN + IV_LEN + (size_t)ct_len + TAG_LEN;
    output = NULL; /* Prevent free below */
    ret = 0;

cleanup:
    EVP_CIPHER_CTX_free(ctx);
    memset(derived_key, 0, sizeof(derived_key));
    free(output);
    return ret;
}

/**
 * Decrypt AES-256-GCM ciphertext.
 * Input format: [salt(16)][iv(12)][ciphertext(N)][tag(16)]
 * Returns 0 on success, -1 on failure (including authentication failure).
 */
static int aes_gcm_decrypt(const uint8_t *key, size_t key_len,
                            const uint8_t *input, size_t input_len,
                            uint8_t **out, size_t *out_len) {
    if (input_len < SALT_LEN + IV_LEN + TAG_LEN) {
        fprintf(stderr, "[Credential] Input too short for decryption\n");
        return -1;
    }

    const uint8_t *salt = input;
    const uint8_t *iv = input + SALT_LEN;
    size_t ct_len = input_len - SALT_LEN - IV_LEN - TAG_LEN;
    const uint8_t *ciphertext = input + SALT_LEN + IV_LEN;
    const uint8_t *tag = input + SALT_LEN + IV_LEN + ct_len;

    /* Derive per-file key from master key + salt using PBKDF2 */
    uint8_t derived_key[MASTER_KEY_LEN];
    if (PKCS5_PBKDF2_HMAC((const char *)key, (int)key_len,
                           salt, SALT_LEN,
                           PBKDF2_ITER, EVP_sha256(),
                           MASTER_KEY_LEN, derived_key) != 1) {
        fprintf(stderr, "[Credential] PBKDF2 key derivation failed\n");
        return -1;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        memset(derived_key, 0, sizeof(derived_key));
        return -1;
    }

    int ret = -1;
    int len = 0;
    int pt_len = 0;
    uint8_t *plaintext = malloc(ct_len > 0 ? ct_len : 1);
    if (!plaintext) goto cleanup;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto cleanup;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) != 1)
        goto cleanup;

    if (EVP_DecryptInit_ex(ctx, NULL, NULL, derived_key, iv) != 1)
        goto cleanup;

    if (ct_len > 0) {
        if (EVP_DecryptUpdate(ctx, plaintext, &len,
                              ciphertext, (int)ct_len) != 1)
            goto cleanup;
        pt_len = len;
    }

    /* Set expected GCM tag before finalizing */
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN,
                            (void *)tag) != 1)
        goto cleanup;

    /* Finalize — this verifies the authentication tag */
    if (EVP_DecryptFinal_ex(ctx, plaintext + pt_len, &len) != 1) {
        fprintf(stderr, "[Credential] GCM authentication failed — "
                "data may be tampered\n");
        goto cleanup;
    }
    pt_len += len;

    *out = plaintext;
    *out_len = (size_t)pt_len;
    plaintext = NULL; /* Prevent free below */
    ret = 0;

cleanup:
    EVP_CIPHER_CTX_free(ctx);
    memset(derived_key, 0, sizeof(derived_key));
    if (plaintext) {
        memset(plaintext, 0, ct_len > 0 ? ct_len : 1);
        free(plaintext);
    }
    return ret;
}

/* ─── D-Bus 인트로스펙션 ─── */

static const char *cred_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_CREDENTIAL_DBUS_NAME "'>"
    "    <method name='Store'>"
    "      <arg type='s' name='service' direction='in'/>"
    "      <arg type='s' name='account' direction='in'/>"
    "      <arg type='s' name='secret' direction='in'/>"
    "      <arg type='s' name='label' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Lookup'>"
    "      <arg type='s' name='service' direction='in'/>"
    "      <arg type='s' name='account' direction='in'/>"
    "      <arg type='s' name='secret' direction='out'/>"
    "    </method>"
    "    <method name='Delete'>"
    "      <arg type='s' name='service' direction='in'/>"
    "      <arg type='s' name='account' direction='in'/>"
    "    </method>"
    "    <method name='SetMasterKey'>"
    "      <arg type='s' name='passphrase' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void handle_cred_method(GDBusConnection *conn, const gchar *sender,
                                const gchar *path, const gchar *iface,
                                const gchar *method, GVariant *params,
                                GDBusMethodInvocation *inv, gpointer data) {
    ZylCredentialStore *store = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "Store") == 0) {
        const gchar *service, *account, *secret, *label;
        g_variant_get(params, "(&s&s&s&s)", &service, &account,
                      &secret, &label);
        int ret = zyl_credential_store(store, service, account,
            ZYL_CRED_PASSWORD, secret, strlen(secret), label);
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(b)", ret == 0));

    } else if (g_strcmp0(method, "Lookup") == 0) {
        const gchar *service, *account;
        g_variant_get(params, "(&s&s)", &service, &account);
        void *secret = NULL;
        size_t len = 0;
        if (zyl_credential_lookup(store, service, account,
                                  &secret, &len) == 0 && secret) {
            char *str = malloc(len + 1);
            if (str) {
                memcpy(str, secret, len);
                str[len] = '\0';
                g_dbus_method_invocation_return_value(
                    inv, g_variant_new("(s)", str));
                memset(str, 0, len + 1);
                free(str);
            } else {
                g_dbus_method_invocation_return_value(
                    inv, g_variant_new("(s)", ""));
            }
            memset(secret, 0, len);
            free(secret);
        } else {
            g_dbus_method_invocation_return_value(
                inv, g_variant_new("(s)", ""));
        }

    } else if (g_strcmp0(method, "Delete") == 0) {
        const gchar *service, *account;
        g_variant_get(params, "(&s&s)", &service, &account);
        zyl_credential_delete(store, service, account);
        g_dbus_method_invocation_return_value(inv, NULL);

    } else if (g_strcmp0(method, "SetMasterKey") == 0) {
        const gchar *passphrase;
        g_variant_get(params, "(&s)", &passphrase);
        int ret = zyl_credential_set_master_key(
            store, passphrase, strlen(passphrase));
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(b)", ret == 0));
    }
}

static const GDBusInterfaceVTable cred_vtable = {
    .method_call = handle_cred_method
};

static void on_cred_bus(GDBusConnection *conn, const gchar *name,
                         gpointer data) {
    ZylCredentialStore *store = data;
    (void)name;
    store->dbus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(
        cred_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(
            conn, ZYL_CREDENTIAL_DBUS_PATH,
            info->interfaces[0], &cred_vtable, store, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Credential] D-Bus registered: %s", ZYL_CREDENTIAL_DBUS_NAME);
}

/* ─── 공개 API ─── */

ZylCredentialStore *zyl_credential_create(const char *store_path) {
    if (!store_path) return NULL;

    ZylCredentialStore *store = calloc(1, sizeof(ZylCredentialStore));
    if (!store) {
        g_critical("[Credential] Failed to allocate store");
        return NULL;
    }

    store->store_path = strdup(store_path);
    if (!store->store_path) {
        free(store);
        return NULL;
    }

    store->key_set = false;
    memset(store->master_key, 0, MASTER_KEY_LEN);
    ensure_dir(store_path);

    store->dbus_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        ZYL_CREDENTIAL_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_cred_bus, NULL, NULL, store, NULL);

    return store;
}

void zyl_credential_destroy(ZylCredentialStore *store) {
    if (!store) return;
    /* Securely wipe master key from memory */
    OPENSSL_cleanse(store->master_key, MASTER_KEY_LEN);
    g_bus_unown_name(store->dbus_owner_id);
    free(store->store_path);
    free(store);
}

int zyl_credential_store(ZylCredentialStore *store,
                          const char *service, const char *account,
                          ZylCredentialType type,
                          const void *secret, size_t secret_len,
                          const char *label) {
    if (!store || !service || !account || !secret || secret_len == 0)
        return -1;
    (void)type; (void)label;

    if (!store->key_set) {
        g_warning("[Credential] Master key not set — refusing to store. "
                  "Call SetMasterKey first.");
        return -1;
    }

    char path[512];
    build_path(path, sizeof(path), store->store_path, service, account);

    /* Generate random salt per file */
    uint8_t salt[SALT_LEN];
    if (RAND_bytes(salt, SALT_LEN) != 1) {
        g_warning("[Credential] RAND_bytes failed for salt");
        return -1;
    }

    /* Encrypt with AES-256-GCM */
    uint8_t *encrypted = NULL;
    size_t enc_len = 0;
    if (aes_gcm_encrypt(store->master_key, MASTER_KEY_LEN,
                        (const uint8_t *)secret, secret_len,
                        salt, SALT_LEN,
                        &encrypted, &enc_len) != 0) {
        g_warning("[Credential] Encryption failed for %s/%s",
                  service, account);
        return -1;
    }

    /* Write to file with restrictive permissions */
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(encrypted);
        return -1;
    }
    int fd = fileno(f);
    if (fd >= 0) fchmod(fd, 0600);
    size_t written = fwrite(encrypted, 1, enc_len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    free(encrypted);

    if (written != enc_len) {
        g_warning("[Credential] Incomplete write for %s/%s",
                  service, account);
        remove(path);
        return -1;
    }

    g_message("[Credential] Stored: %s/%s (%zu bytes encrypted)",
              service, account, enc_len);
    return 0;
}

int zyl_credential_lookup(ZylCredentialStore *store,
                           const char *service, const char *account,
                           void **out_secret, size_t *out_len) {
    if (!store || !service || !account || !out_secret || !out_len)
        return -1;

    if (!store->key_set) {
        g_warning("[Credential] Master key not set — cannot decrypt");
        return -1;
    }

    char path[512];
    build_path(path, sizeof(path), store->store_path, service, account);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    if (file_len <= 0 || (size_t)file_len < SALT_LEN + IV_LEN + TAG_LEN) {
        fclose(f);
        return -1;
    }
    fseek(f, 0, SEEK_SET);

    uint8_t *encrypted = malloc((size_t)file_len);
    if (!encrypted) { fclose(f); return -1; }
    size_t rd = fread(encrypted, 1, (size_t)file_len, f);
    fclose(f);

    if (rd != (size_t)file_len) {
        free(encrypted);
        return -1;
    }

    /* Decrypt with AES-256-GCM */
    uint8_t *decrypted = NULL;
    size_t dec_len = 0;
    int ret = aes_gcm_decrypt(store->master_key, MASTER_KEY_LEN,
                              encrypted, rd,
                              &decrypted, &dec_len);
    /* Securely wipe encrypted buffer */
    OPENSSL_cleanse(encrypted, rd);
    free(encrypted);

    if (ret != 0) {
        g_warning("[Credential] Decryption failed for %s/%s "
                  "(tampered or wrong key)", service, account);
        return -1;
    }

    *out_secret = decrypted;
    *out_len = dec_len;
    return 0;
}

int zyl_credential_delete(ZylCredentialStore *store,
                           const char *service, const char *account) {
    if (!store || !service || !account) return -1;

    char path[512];
    build_path(path, sizeof(path), store->store_path, service, account);

    /* Overwrite file with random data before unlinking (secure delete) */
    FILE *f = fopen(path, "r+b");
    if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        if (len > 0) {
            fseek(f, 0, SEEK_SET);
            uint8_t *noise = malloc((size_t)len);
            if (noise) {
                RAND_bytes(noise, (int)len);
                fwrite(noise, 1, (size_t)len, f);
                fflush(f);
                free(noise);
            }
        }
        fclose(f);
    }

    if (remove(path) == 0) {
        g_message("[Credential] Securely deleted: %s/%s", service, account);
        return 0;
    }
    return -1;
}

int zyl_credential_list(ZylCredentialStore *store,
                         const char *service,
                         ZylCredentialInfo **out, int *count) {
    if (!store || !service || !out || !count) return -1;

    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%s",
             store->store_path, service);

    DIR *dir = opendir(dir_path);
    if (!dir) { *out = NULL; *count = 0; return 0; }

    int cap = 16;
    int n = 0;
    ZylCredentialInfo *list = calloc(cap, sizeof(ZylCredentialInfo));
    if (!list) { closedir(dir); return -1; }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        size_t name_len = strlen(entry->d_name);
        if (name_len < 5) continue; /* must end in .enc */

        if (n >= cap) {
            cap *= 2;
            ZylCredentialInfo *tmp = realloc(
                list, cap * sizeof(ZylCredentialInfo));
            if (!tmp) break;
            list = tmp;
        }

        ZylCredentialInfo *info = &list[n];
        info->service = strdup(service);
        if (!info->service) continue;
        /* account = filename minus .enc suffix */
        char *acc = strdup(entry->d_name);
        if (!acc) { free(info->service); info->service = NULL; continue; }
        size_t al = strlen(acc);
        if (al > 4 && strcmp(acc + al - 4, ".enc") == 0) acc[al - 4] = '\0';
        info->account = acc;
        info->label = NULL;
        info->type = ZYL_CRED_PASSWORD;
        n++;
    }
    closedir(dir);

    *out = list;
    *count = n;
    return 0;
}

void zyl_credential_info_free(ZylCredentialInfo *info, int count) {
    if (!info) return;
    for (int i = 0; i < count; i++) {
        free(info[i].label);
        free(info[i].service);
        free(info[i].account);
    }
    free(info);
}

int zyl_credential_set_master_key(ZylCredentialStore *store,
                                   const void *key, size_t key_len) {
    if (!store || !key || key_len == 0) return -1;

    /*
     * Derive a 256-bit master key from the user passphrase via PBKDF2.
     * We use a fixed service-level salt here (derived from store path)
     * to ensure the same passphrase always produces the same master key
     * for the same store instance. Per-credential salts are generated
     * independently during encryption.
     */
    uint8_t store_salt[SALT_LEN];
    /* Derive a deterministic salt from store path */
    memset(store_salt, 0, SALT_LEN);
    size_t path_len = strlen(store->store_path);
    for (size_t i = 0; i < path_len && i < SALT_LEN; i++) {
        store_salt[i] = (uint8_t)store->store_path[i];
    }

    if (PKCS5_PBKDF2_HMAC((const char *)key, (int)key_len,
                           store_salt, SALT_LEN,
                           PBKDF2_ITER, EVP_sha256(),
                           MASTER_KEY_LEN, store->master_key) != 1) {
        g_warning("[Credential] PBKDF2 master key derivation failed");
        memset(store->master_key, 0, MASTER_KEY_LEN);
        return -1;
    }

    store->key_set = true;
    g_message("[Credential] Master key derived from passphrase "
              "(%zu bytes input, PBKDF2 %d iterations)",
              key_len, PBKDF2_ITER);
    return 0;
}

/* ─── 데몬 진입점 ─── */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    ZylCredentialStore *store = zyl_credential_create(
        "/var/lib/zyl-os/credentials");
    if (!store) {
        g_critical("[Credential] Failed to create store");
        return 1;
    }

    g_message("[Credential] Zyl OS Credential Manager started "
              "(AES-256-GCM + PBKDF2)");
    g_message("[Credential] Waiting for SetMasterKey via D-Bus "
              "before accepting Store/Lookup requests");

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    zyl_credential_destroy(store);
    return 0;
}
