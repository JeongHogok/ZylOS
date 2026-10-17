/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 화면 잠금 자격 증명 — PIN 설정·검증 (org.zylos.Lock), 그리고 사용자 데이터를
 *       잠그는 키(fscrypt)의 주인
 * 수행범위: PIN 을 PBKDF2-HMAC-SHA256 솔트 해시로만 저장, 상수 시간 비교,
 *           실패 누적에 따른 대기(재부팅·앱 재시작으로 초기화되지 않음 — 시도는
 *           PIN 을 보기 전에 디스크에 센다, 못 세면 시도도 없다),
 *           PIN 변경·제거는 현재 PIN 확인 후.
 *           사용자 데이터(CE_DIRS)의 키: 첫 부팅에 만들고 PIN 에서 나온 키로 감싸
 *           PIN 해시와 같은 파일에 둔다(둘이 어긋날 수 없게 한 번에 쓴다).
 *           --prepare(zyl-lock-prepare.service, 서비스들보다 먼저): 디렉터리에 정책을
 *           걸고, PIN 이 없으면 키를 올린다. PIN 이 있으면 부팅 뒤 첫 맞는 PIN 이
 *           키를 올리고 StorageUnlocked 를 알린다(그 뒤로는 열린 채 — 화면 잠금이
 *           다시 잠그지 않는다)
 * 의존방향: gio/gio.h (D-Bus), OpenSSL (PBKDF2, AES-GCM, RAND, CRYPTO_memcmp),
 *           runtime/lib/fscrypt (커널)
 * SOLID: SRP — 잠금 자격 증명만. 화면은 lockscreen, 설정 UI 는 settings 앱
 *
 * PIN 은 설정 저장소·앱 메시지·에뮬레이터 어디에도 평문으로 가지 않는다.
 * 6자리 PIN 은 해시만으로 오프라인 대입을 막을 수 없으므로 실제 방어선은
 * 실패 대기와 /data/lock 접근 제한(root, 0600)이다.
 * shortcut: 하드웨어 키(TEE) 결합 없음 — /data 이미지를 떠 간 사람은 PIN 10^6 개를
 *           오프라인으로 대 볼 수 있다. 보드에 TEE·eFuse 키가 생기면 PIN 검증과 키
 *           감싸기를 그쪽에 묶을 것
 * ────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <gio/gio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "fscrypt.h"

#ifndef ZYL_LOCK_DIR
#define ZYL_LOCK_DIR "/data/lock"
#endif
#define ZYL_LOCK_FILE ZYL_LOCK_DIR "/state"

/* The user's data — locked by the PIN until the first unlock after boot.
   The rest of /data (settings, alarms, call log, network, push …) stays
   readable before it, so the phone rings, alarms go off and Wi-Fi connects.
   Each is made by tmpfiles (zyl-data.conf — verify-all checks) and empty on
   the first boot, when it gets its policy */
#ifndef ZYL_CE_DIRS
#define ZYL_CE_DIRS "/data/appdata", "/data/users", "/data/contacts", "/data/messages", "/data/credentials"
#endif
static const char *const ce_dirs[] = { ZYL_CE_DIRS, NULL };
#define PBKDF2_ITERATIONS 50000
#define SALT_LEN 16
#define HASH_LEN 32
#define FREE_ATTEMPTS 5          /* before the first wait */
#define BASE_WAIT_S 30
#define MAX_WAIT_S 3600
/* The sealed storage key: hex of salt | iv | AES-256-GCM(key) | tag, under
   PBKDF2(PIN or "", salt, CE_ITERATIONS). Part of its format: changing the
   count leaves every sealed key unopenable */
#define CE_ITERATIONS 50000
#define CE_IV_LEN 12
#define CE_TAG_LEN 16
#define CE_SEALED_LEN (SALT_LEN + CE_IV_LEN + ZYL_FSCRYPT_KEY_LEN + CE_TAG_LEN)

typedef struct {
    char    hash[256];           /* "" = no PIN; else pbkdf2-sha256$iter$salt$hash */
    guint   failures;
    gint64  wait_until;          /* epoch seconds */
    char    ce[CE_SEALED_LEN * 2 + 1];          /* "" = no storage key (made before encryption) */
    char    ce_id[ZYL_FSCRYPT_ID_LEN * 2 + 1];  /* what the directories' policies name */
} LockState;

static LockState g_lock;

/* ─── Persistence ─── */
static void lock_load(LockState *s) {
    memset(s, 0, sizeof(*s));
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, ZYL_LOCK_FILE, G_KEY_FILE_NONE, NULL)) {
        char *h = g_key_file_get_string(kf, "lock", "hash", NULL);
        if (h) g_strlcpy(s->hash, h, sizeof(s->hash));
        g_free(h);
        s->failures = (guint)g_key_file_get_integer(kf, "lock", "failures", NULL);
        s->wait_until = g_key_file_get_int64(kf, "lock", "wait_until", NULL);
        char *ce = g_key_file_get_string(kf, "lock", "ce", NULL), *id = g_key_file_get_string(kf, "lock", "ce_id", NULL);
        if (ce) g_strlcpy(s->ce, ce, sizeof(s->ce));
        if (id) g_strlcpy(s->ce_id, id, sizeof(s->ce_id));
        g_free(ce);
        g_free(id);
    }
    g_key_file_free(kf);
}

static bool lock_save(const LockState *s) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, "lock", "hash", s->hash);
    g_key_file_set_integer(kf, "lock", "failures", (gint)s->failures);
    g_key_file_set_int64(kf, "lock", "wait_until", s->wait_until);
    g_key_file_set_string(kf, "lock", "ce", s->ce);
    g_key_file_set_string(kf, "lock", "ce_id", s->ce_id);
    gsize len = 0;
    char *text = g_key_file_to_data(kf, &len, NULL);
    g_mkdir_with_parents(ZYL_LOCK_DIR, 0700);
    bool ok = g_file_set_contents_full(ZYL_LOCK_FILE, text, (gssize)len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, NULL);
    g_free(text);
    g_key_file_free(kf);
    return ok;
}

/* ─── PIN hashing ─── */
static bool pin_format_ok(const char *pin) {
    size_t n = strlen(pin);
    if (n != 6) return false;   /* 화면 키패드와 같은 길이 — UI 와 서비스가 어긋나지 않게 */
    for (size_t i = 0; i < n; i++) if (pin[i] < '0' || pin[i] > '9') return false;
    return true;
}

static bool derive(const char *pin, const unsigned char *salt, int iter, unsigned char *out) {
    return PKCS5_PBKDF2_HMAC(pin, (int)strlen(pin), salt, SALT_LEN, iter,
                             EVP_sha256(), HASH_LEN, out) == 1;
}

static void to_hex(const unsigned char *b, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
}

static bool hash_pin(const char *pin, char *out, size_t out_len) {
    unsigned char salt[SALT_LEN], hash[HASH_LEN];
    if (RAND_bytes(salt, SALT_LEN) != 1 || !derive(pin, salt, PBKDF2_ITERATIONS, hash)) return false;
    char salt_hex[SALT_LEN * 2 + 1], hash_hex[HASH_LEN * 2 + 1];
    to_hex(salt, SALT_LEN, salt_hex);
    to_hex(hash, HASH_LEN, hash_hex);
    snprintf(out, out_len, "pbkdf2-sha256$%d$%s$%s", PBKDF2_ITERATIONS, salt_hex, hash_hex);
    return true;
}

static bool unhex(const char *hex, unsigned char *out, size_t n) {
    if (strlen(hex) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned int b;
        if (sscanf(hex + i * 2, "%2x", &b) != 1) return false;
        out[i] = (unsigned char)b;
    }
    return true;
}

static bool pin_matches(const char *stored, const char *pin) {
    char **f = g_strsplit(stored, "$", -1);
    unsigned char salt[SALT_LEN], want[HASH_LEN], got[HASH_LEN];
    bool ok = g_strv_length(f) == 4 && strcmp(f[0], "pbkdf2-sha256") == 0 &&
              unhex(f[2], salt, SALT_LEN) && unhex(f[3], want, HASH_LEN) &&
              derive(pin, salt, atoi(f[1]), got) &&
              CRYPTO_memcmp(got, want, HASH_LEN) == 0;
    g_strfreev(f);
    return ok;
}

/* ─── The storage key ─── */
static bool gcm(bool enc, const unsigned char *kek, const unsigned char *iv,
                const unsigned char *in, unsigned char *out, unsigned char *tag) {
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int n = 0, fin = 0;
    bool ok = c && EVP_CipherInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL, enc) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, CE_IV_LEN, NULL) == 1 &&
              EVP_CipherInit_ex(c, NULL, NULL, kek, iv, enc) == 1 &&
              (enc || EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, CE_TAG_LEN, tag) == 1) &&
              EVP_CipherUpdate(c, out, &n, in, ZYL_FSCRYPT_KEY_LEN) == 1 &&
              EVP_CipherFinal_ex(c, out + n, &fin) == 1 &&
              (!enc || EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, CE_TAG_LEN, tag) == 1);
    EVP_CIPHER_CTX_free(c);
    return ok;
}

/* key under pin ("" = no PIN) → s->ce */
static bool ce_seal(LockState *s, const unsigned char key[ZYL_FSCRYPT_KEY_LEN], const char *pin) {
    unsigned char b[CE_SEALED_LEN], kek[HASH_LEN];
    unsigned char *salt = b, *iv = b + SALT_LEN, *ct = iv + CE_IV_LEN, *tag = ct + ZYL_FSCRYPT_KEY_LEN;
    bool ok = RAND_bytes(b, SALT_LEN + CE_IV_LEN) == 1 &&
              PKCS5_PBKDF2_HMAC(pin, (int)strlen(pin), salt, SALT_LEN, CE_ITERATIONS,
                                EVP_sha256(), HASH_LEN, kek) == 1 &&
              gcm(true, kek, iv, key, ct, tag);
    if (ok) to_hex(b, CE_SEALED_LEN, s->ce);
    OPENSSL_cleanse(kek, sizeof(kek));
    return ok;
}

/* s->ce under pin → key; a wrong PIN fails the tag */
static bool ce_open(const LockState *s, const char *pin, unsigned char key[ZYL_FSCRYPT_KEY_LEN]) {
    unsigned char b[CE_SEALED_LEN], kek[HASH_LEN];
    unsigned char *salt = b, *iv = b + SALT_LEN, *ct = iv + CE_IV_LEN, *tag = ct + ZYL_FSCRYPT_KEY_LEN;
    bool ok = unhex(s->ce, b, CE_SEALED_LEN) &&
              PKCS5_PBKDF2_HMAC(pin, (int)strlen(pin), salt, SALT_LEN, CE_ITERATIONS,
                                EVP_sha256(), HASH_LEN, kek) == 1 &&
              gcm(false, kek, iv, ct, key, tag);
    OPENSSL_cleanse(kek, sizeof(kek));
    return ok;
}

/* The storage key onto /data: 1 put on now, 0 already on (or none — made
   before encryption), -1 the PIN did not open it */
static int ce_unlock(const LockState *s, const char *pin) {
    unsigned char id[ZYL_FSCRYPT_ID_LEN], key[ZYL_FSCRYPT_KEY_LEN];
    if (!s->ce[0]) return 0;
    if (unhex(s->ce_id, id, sizeof(id)) && zyl_fscrypt_key_present(ZYL_LOCK_DIR, id) == 1) return 0;
    int r = ce_open(s, pin, key) && zyl_fscrypt_add_key(ZYL_LOCK_DIR, key, id) ? 1 : -1;
    OPENSSL_cleanse(key, sizeof(key));
    return r;
}

static bool dir_empty(const char *dir) {
    GDir *d = g_dir_open(dir, 0, NULL);
    bool empty = d && !g_dir_read_name(d);
    if (d) g_dir_close(d);
    return empty;
}

/* zyl-lock --prepare, before any service starts. First boot: a new key, saved
   before any directory names it (a policy naming a key that was not kept
   locks that directory for good). Every boot: each user data directory has the
   key's policy — an empty one gets it — and with no PIN the key goes on.
   0 when all of them are encrypted with it and, with no PIN, open */
static int ce_prepare(LockState *s) {
    unsigned char key[ZYL_FSCRYPT_KEY_LEN], id[ZYL_FSCRYPT_ID_LEN];
    int bad = 0;
    if (!s->ce[0] && !s->hash[0]) {
        LockState next = *s;
        if (RAND_bytes(key, sizeof(key)) == 1 && zyl_fscrypt_add_key(ZYL_LOCK_DIR, key, id) &&
            ce_seal(&next, key, "")) {
            to_hex(id, sizeof(id), next.ce_id);
            if (lock_save(&next)) *s = next;
        }
        OPENSSL_cleanse(key, sizeof(key));
    } else if (!s->hash[0] && ce_unlock(s, "") < 0) {
        g_critical("[Lock] the storage key did not go on — user data stays locked");
        bad++;
    }
    if (!s->ce[0] || !unhex(s->ce_id, id, sizeof(id))) {
        g_critical("[Lock] no storage key — user data is not encrypted");
        return 1;
    }
    for (const char *const *d = ce_dirs; *d; d++) {
        unsigned char has[ZYL_FSCRYPT_ID_LEN];
        int p = zyl_fscrypt_get_policy(*d, has);
        if (p == 0 && dir_empty(*d) && zyl_fscrypt_set_policy(*d, id)) {
            memcpy(has, id, sizeof(id));
            p = 1;
        }
        if (p != 1 || memcmp(has, id, sizeof(id)) != 0) {
            g_critical("[Lock] %s is not encrypted with the storage key", *d);
            bad++;
        }
    }
    return bad ? 1 : 0;
}

/* ─── Policy ─── */
static gint64 now_s(void) { return (gint64)time(NULL); }

static guint64 retry_after_ms(const LockState *s, gint64 now) {
    gint64 left = s->wait_until - now;
    if (left <= 0) return 0;
    if (left > MAX_WAIT_S) left = MAX_WAIT_S;   /* a clock set back must not lock for years */
    return (guint64)left * 1000;
}

/* 30 s after the 5th failure, doubling every 5 more, at most an hour */
static gint64 wait_for(guint failures) {
    if (failures < FREE_ATTEMPTS) return 0;
    guint step = (failures - FREE_ATTEMPTS) / FREE_ATTEMPTS;
    gint64 w = BASE_WAIT_S;
    while (step-- > 0 && w < MAX_WAIT_S) w *= 2;
    return w > MAX_WAIT_S ? MAX_WAIT_S : w;
}

typedef enum { CHECK_OK, CHECK_WRONG, CHECK_WAIT, CHECK_STORAGE } CheckResult;

/* One attempt against the stored PIN: counts failures, enforces the wait.
   The attempt is on disk as a failure before the PIN is looked at, and with no
   room to write it there is no attempt: a count kept only in memory (a full
   /data — the save failing — or this service killed right after a guess) gave
   five free guesses again on every restart */
static CheckResult lock_check(LockState *s, const char *pin, gint64 now) {
    if (retry_after_ms(s, now)) return CHECK_WAIT;   /* not even tried: no oracle while waiting */
    LockState tried = *s;
    tried.failures++;
    gint64 w = wait_for(tried.failures);
    tried.wait_until = w ? now + w : 0;
    if (!lock_save(&tried)) return CHECK_STORAGE;
    if (pin_matches(s->hash, pin)) {
        s->failures = 0;
        s->wait_until = 0;
        lock_save(s);      /* if this fails the count stays one higher: fewer free tries, not more */
        return CHECK_OK;
    }
    *s = tried;
    return CHECK_WRONG;
}

/* reason: "" ok, "format", "wrong_pin", "locked_out", "storage" */
static const char *lock_set_pin(LockState *s, const char *current, const char *pin, gint64 now) {
    if (*pin && !pin_format_ok(pin)) return "format";
    if (s->hash[0]) {
        CheckResult r = lock_check(s, current, now);
        if (r == CHECK_WAIT) return "locked_out";
        if (r == CHECK_WRONG) return "wrong_pin";
        if (r == CHECK_STORAGE) return "storage";
    }
    LockState next = *s;
    next.hash[0] = '\0';
    if (*pin && !hash_pin(pin, next.hash, sizeof(next.hash))) return "storage";
    /* The storage key moves to the new PIN in the same write as the hash */
    if (s->ce[0]) {
        unsigned char key[ZYL_FSCRYPT_KEY_LEN];
        bool ok = ce_open(s, s->hash[0] ? current : "", key) && ce_seal(&next, key, pin);
        OPENSSL_cleanse(key, sizeof(key));
        if (!ok) return "storage";
    }
    next.failures = 0;
    next.wait_until = 0;
    if (!lock_save(&next)) return "storage";
    *s = next;
    return "";
}

static GDBusConnection *g_conn;

/* A right PIN also opens the user data, the first time after boot */
static CheckResult lock_verify(LockState *s, const char *pin, gint64 now) {
    if (!s->hash[0]) return CHECK_OK;
    CheckResult r = lock_check(s, pin, now);
    int opened = r == CHECK_OK ? ce_unlock(s, pin) : 0;
    if (opened < 0) g_critical("[Lock] the PIN is right but the user data did not open");
    if (opened > 0 && g_conn)
        g_dbus_connection_emit_signal(g_conn, NULL, "/org/zylos/Lock", "org.zylos.Lock",
                                      "StorageUnlocked", NULL, NULL);
    return r;
}

/* ─── D-Bus ─── */
static const char *lock_introspection_xml =
    "<node>"
    "  <interface name='org.zylos.Lock'>"
    "    <method name='GetState'>"
    "      <arg type='a{sv}' name='state' direction='out'/>"
    "    </method>"
    "    <method name='VerifyPin'>"
    "      <arg type='s' name='pin' direction='in'/>"
    "      <arg type='a{sv}' name='result' direction='out'/>"
    "    </method>"
    "    <method name='SetPin'>"
    "      <arg type='s' name='current' direction='in'/>"
    "      <arg type='s' name='pin' direction='in'/>"
    "      <arg type='a{sv}' name='result' direction='out'/>"
    "    </method>"
    "    <signal name='StorageUnlocked'/>"
    "  </interface>"
    "</node>";

static GVariant *result_variant(bool ok, const char *reason, const LockState *s, gint64 now) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "ok", g_variant_new_boolean(ok));
    g_variant_builder_add(&b, "{sv}", "reason", g_variant_new_string(reason));
    g_variant_builder_add(&b, "{sv}", "hasPin", g_variant_new_boolean(s->hash[0] != '\0'));
    g_variant_builder_add(&b, "{sv}", "retryAfterMs", g_variant_new_uint64(retry_after_ms(s, now)));
    return g_variant_new("(a{sv})", &b);
}

static void handle_lock_method(GDBusConnection *conn, const gchar *sender,
                               const gchar *path, const gchar *iface,
                               const gchar *method, GVariant *params,
                               GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;
    gint64 now = now_s();
    if (g_strcmp0(method, "GetState") == 0) {
        g_dbus_method_invocation_return_value(inv, result_variant(true, "", &g_lock, now));
    } else if (g_strcmp0(method, "VerifyPin") == 0) {
        const gchar *pin = NULL;
        g_variant_get(params, "(&s)", &pin);
        CheckResult r = lock_verify(&g_lock, pin, now);
        g_dbus_method_invocation_return_value(inv, result_variant(r == CHECK_OK,
            r == CHECK_OK ? "" : r == CHECK_WAIT ? "locked_out" :
            r == CHECK_STORAGE ? "storage" : "wrong_pin", &g_lock, now));
    } else if (g_strcmp0(method, "SetPin") == 0) {
        const gchar *current = NULL, *pin = NULL;
        g_variant_get(params, "(&s&s)", &current, &pin);
        const char *reason = lock_set_pin(&g_lock, current, pin, now);
        g_dbus_method_invocation_return_value(inv, result_variant(!*reason, reason, &g_lock, now));
    }
}

static const GDBusInterfaceVTable lock_vtable = { .method_call = handle_lock_method };

#ifndef ZYL_TEST
static void on_lock_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    g_conn = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(lock_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0])
        g_dbus_connection_register_object(conn, "/org/zylos/Lock",
            info->interfaces[0], &lock_vtable, NULL, NULL, NULL);
    if (info) g_dbus_node_info_unref(info);
}

int main(int argc, char *argv[]) {
    lock_load(&g_lock);
    if (argc > 1 && strcmp(argv[1], "--prepare") == 0) return ce_prepare(&g_lock);
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.Lock", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_lock_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
