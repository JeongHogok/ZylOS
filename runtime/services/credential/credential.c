#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 인증 정보 암호화 저장소 — AES-256-GCM 기반 키체인
 * 수행범위: 비밀번호/토큰 암호화 저장, 조회, 삭제, 마스터 키 관리
 * 의존방향: credential.h, gio/gio.h
 * SOLID: SRP — 인증 정보 암호화 저장만 담당
 *
 * 저장소 구조: {store_path}/{service}/{account}.enc
 * 암호화: AES-256-GCM (프로덕션에서는 OpenSSL EVP 사용)
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

#define MASTER_KEY_LEN 32   /* AES-256 = 32 bytes */

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
                        const char *store, const char *service, const char *account) {
    snprintf(out, out_len, "%s/%s", store, service);
    ensure_dir(out);
    size_t len = strlen(out);
    snprintf(out + len, out_len - len, "/%s.enc", account);
}

/*
 * 간이 XOR 암호화 (프로토타입).
 * 프로덕션에서는 OpenSSL EVP_aes_256_gcm을 사용해야 합니다:
 *
 *   EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
 *   EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv);
 *   EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len);
 *   EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);
 *   EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
 */
static void xor_cipher(const uint8_t *key, size_t key_len,
                        const uint8_t *in, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        out[i] = in[i] ^ key[i % key_len];
    }
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
        g_variant_get(params, "(&s&s&s&s)", &service, &account, &secret, &label);
        int ret = zyl_credential_store(store, service, account,
            ZYL_CRED_PASSWORD, secret, strlen(secret), label);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ret == 0));

    } else if (g_strcmp0(method, "Lookup") == 0) {
        const gchar *service, *account;
        g_variant_get(params, "(&s&s)", &service, &account);
        void *secret = NULL;
        size_t len = 0;
        if (zyl_credential_lookup(store, service, account, &secret, &len) == 0 && secret) {
            /* NULL 종료 보장 */
            char *str = malloc(len + 1);
            if (str) {
                memcpy(str, secret, len);
                str[len] = '\0';
                g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", str));
                free(str);
            } else {
                g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", ""));
            }
            free(secret);
        } else {
            g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", ""));
        }

    } else if (g_strcmp0(method, "Delete") == 0) {
        const gchar *service, *account;
        g_variant_get(params, "(&s&s)", &service, &account);
        zyl_credential_delete(store, service, account);
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

static const GDBusInterfaceVTable cred_vtable = { .method_call = handle_cred_method };

static void on_cred_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    ZylCredentialStore *store = data;
    store->dbus = conn;

    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(cred_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_CREDENTIAL_DBUS_PATH,
            info->interfaces[0], &cred_vtable, store, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Credential] D-Bus registered: %s", ZYL_CREDENTIAL_DBUS_NAME);
}

/* ─── 공개 API ─── */

ZylCredentialStore *zyl_credential_create(const char *store_path) {
    if (!store_path) return NULL;

    ZylCredentialStore *store = calloc(1, sizeof(ZylCredentialStore));
    if (!store) return NULL;

    store->store_path = strdup(store_path);
    if (!store->store_path) { free(store); return NULL; }

    store->key_set = false;
    ensure_dir(store_path);

    store->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_CREDENTIAL_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_cred_bus, NULL, NULL, store, NULL);

    g_warning("[Credential] Using XOR cipher prototype. "
              "Production MUST use OpenSSL EVP_aes_256_gcm. "
              "See credential.c line 50 for migration path.");

    return store;
}

void zyl_credential_destroy(ZylCredentialStore *store) {
    if (!store) return;
    /* 마스터 키를 메모리에서 안전하게 삭제 */
    memset(store->master_key, 0, MASTER_KEY_LEN);
    g_bus_unown_name(store->dbus_owner_id);
    free(store->store_path);
    free(store);
}

int zyl_credential_store(ZylCredentialStore *store,
                          const char *service, const char *account,
                          ZylCredentialType type,
                          const void *secret, size_t secret_len,
                          const char *label) {
    if (!store || !service || !account || !secret || secret_len == 0) return -1;
    (void)type; (void)label;

    char path[512];
    build_path(path, sizeof(path), store->store_path, service, account);

    /* 암호화 */
    uint8_t *encrypted = malloc(secret_len);
    if (!encrypted) return -1;

    if (store->key_set) {
        xor_cipher(store->master_key, MASTER_KEY_LEN,
                   (const uint8_t *)secret, encrypted, secret_len);
    } else {
        /* 마스터 키 미설정 → 기본 키 사용 (보안 경고) */
        g_warning("[Credential] Master key not set — using insecure default");
        uint8_t default_key[MASTER_KEY_LEN];
        memset(default_key, 0x5A, MASTER_KEY_LEN);
        xor_cipher(default_key, MASTER_KEY_LEN,
                   (const uint8_t *)secret, encrypted, secret_len);
        memset(default_key, 0, sizeof(default_key));
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(encrypted); return -1; }
    if (f) {
        int fd = fileno(f);
        if (fd >= 0) fchmod(fd, 0600);
    }
    fwrite(encrypted, 1, secret_len, f);
    fclose(f);
    free(encrypted);

    g_message("[Credential] Stored: %s/%s", service, account);
    return 0;
}

int zyl_credential_lookup(ZylCredentialStore *store,
                           const char *service, const char *account,
                           void **out_secret, size_t *out_len) {
    if (!store || !service || !account || !out_secret || !out_len) return -1;

    char path[512];
    build_path(path, sizeof(path), store->store_path, service, account);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);

    uint8_t *encrypted = malloc((size_t)len);
    if (!encrypted) { fclose(f); return -1; }
    size_t rd = fread(encrypted, 1, (size_t)len, f);
    fclose(f);

    uint8_t *decrypted = malloc(rd);
    if (!decrypted) { free(encrypted); return -1; }

    if (store->key_set) {
        xor_cipher(store->master_key, MASTER_KEY_LEN, encrypted, decrypted, rd);
    } else {
        uint8_t default_key[MASTER_KEY_LEN];
        memset(default_key, 0x5A, MASTER_KEY_LEN);
        xor_cipher(default_key, MASTER_KEY_LEN, encrypted, decrypted, rd);
        memset(default_key, 0, sizeof(default_key));
    }

    free(encrypted);
    *out_secret = decrypted;
    *out_len = rd;
    return 0;
}

int zyl_credential_delete(ZylCredentialStore *store,
                           const char *service, const char *account) {
    if (!store || !service || !account) return -1;

    char path[512];
    build_path(path, sizeof(path), store->store_path, service, account);

    if (remove(path) == 0) {
        g_message("[Credential] Deleted: %s/%s", service, account);
        return 0;
    }
    return -1;
}

int zyl_credential_list(ZylCredentialStore *store,
                         const char *service,
                         ZylCredentialInfo **out, int *count) {
    if (!store || !service || !out || !count) return -1;

    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", store->store_path, service);

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
            ZylCredentialInfo *tmp = realloc(list, cap * sizeof(ZylCredentialInfo));
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
     * TODO: Production key derivation:
     *   Use PBKDF2-HMAC-SHA256 with:
     *   - Salt: random 16 bytes stored alongside
     *   - Iterations: 100,000+
     *   - Output: 32 bytes (AES-256 key)
     *
     *   unsigned char salt[16];
     *   RAND_bytes(salt, sizeof(salt));
     *   PKCS5_PBKDF2_HMAC(key, key_len, salt, sizeof(salt),
     *                       100000, EVP_sha256(), MASTER_KEY_LEN, store->master_key);
     */
    memset(store->master_key, 0, MASTER_KEY_LEN);
    size_t copy = key_len < MASTER_KEY_LEN ? key_len : MASTER_KEY_LEN;
    memcpy(store->master_key, key, copy);
    store->key_set = true;

    g_message("[Credential] Master key set (%zu bytes)", key_len);
    return 0;
}

/* ─── 데몬 진입점 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    ZylCredentialStore *store = zyl_credential_create("/var/lib/zyl-os/credentials");
    if (!store) {
        g_critical("[Credential] Failed to create store");
        return 1;
    }

    g_message("[Credential] Zyl OS Credential Manager started");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    zyl_credential_destroy(store);
    return 0;
}
