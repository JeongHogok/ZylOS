/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 화면 잠금 자격 증명 — PIN 설정·검증 (org.zylos.Lock)
 * 수행범위: PIN 을 PBKDF2-HMAC-SHA256 솔트 해시로만 저장, 상수 시간 비교,
 *           실패 누적에 따른 대기(재부팅·앱 재시작으로 초기화되지 않음),
 *           PIN 변경·제거는 현재 PIN 확인 후
 * 의존방향: gio/gio.h (D-Bus), OpenSSL (PBKDF2, RAND, CRYPTO_memcmp)
 * SOLID: SRP — 잠금 자격 증명만. 화면은 lockscreen, 설정 UI 는 settings 앱
 *
 * PIN 은 설정 저장소·앱 메시지·에뮬레이터 어디에도 평문으로 가지 않는다.
 * 4자리 PIN 은 해시만으로 오프라인 대입을 막을 수 없으므로 실제 방어선은
 * 실패 대기와 /data/lock 접근 제한(root, 0600)이다.
 * shortcut: 하드웨어 키(TEE) 결합 없음 — 보드에 TEE 가 생기면 PIN 검증을 옮길 것
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

#ifndef ZYL_LOCK_FILE
#define ZYL_LOCK_FILE "/data/lock/state"
#endif
#define PBKDF2_ITERATIONS 50000
#define SALT_LEN 16
#define HASH_LEN 32
#define FREE_ATTEMPTS 5          /* before the first wait */
#define BASE_WAIT_S 30
#define MAX_WAIT_S 3600

typedef struct {
    char    hash[256];           /* "" = no PIN; else pbkdf2-sha256$iter$salt$hash */
    guint   failures;
    gint64  wait_until;          /* epoch seconds */
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
    }
    g_key_file_free(kf);
}

static bool lock_save(const LockState *s) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, "lock", "hash", s->hash);
    g_key_file_set_integer(kf, "lock", "failures", (gint)s->failures);
    g_key_file_set_int64(kf, "lock", "wait_until", s->wait_until);
    gsize len = 0;
    char *text = g_key_file_to_data(kf, &len, NULL);
    char *dir = g_path_get_dirname(ZYL_LOCK_FILE);
    g_mkdir_with_parents(dir, 0700);
    bool ok = g_file_set_contents_full(ZYL_LOCK_FILE, text, (gssize)len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, NULL);
    g_free(dir);
    g_free(text);
    g_key_file_free(kf);
    return ok;
}

/* ─── PIN hashing ─── */
static bool pin_format_ok(const char *pin) {
    size_t n = strlen(pin);
    if (n < 4 || n > 16) return false;
    for (size_t i = 0; i < n; i++) if (pin[i] < '0' || pin[i] > '9') return false;
    return true;
}

static bool derive(const char *pin, const unsigned char *salt, int iter, unsigned char *out) {
    return PKCS5_PBKDF2_HMAC(pin, (int)strlen(pin), salt, SALT_LEN, iter,
                             EVP_sha256(), HASH_LEN, out) == 1;
}

static bool hash_pin(const char *pin, char *out, size_t out_len) {
    unsigned char salt[SALT_LEN], hash[HASH_LEN];
    if (RAND_bytes(salt, SALT_LEN) != 1 || !derive(pin, salt, PBKDF2_ITERATIONS, hash)) return false;
    char *salt_hex = g_malloc(SALT_LEN * 2 + 1), *hash_hex = g_malloc(HASH_LEN * 2 + 1);
    for (int i = 0; i < SALT_LEN; i++) sprintf(salt_hex + i * 2, "%02x", salt[i]);
    for (int i = 0; i < HASH_LEN; i++) sprintf(hash_hex + i * 2, "%02x", hash[i]);
    snprintf(out, out_len, "pbkdf2-sha256$%d$%s$%s", PBKDF2_ITERATIONS, salt_hex, hash_hex);
    g_free(salt_hex);
    g_free(hash_hex);
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

typedef enum { CHECK_OK, CHECK_WRONG, CHECK_WAIT } CheckResult;

/* One attempt against the stored PIN: counts failures, enforces the wait */
static CheckResult lock_check(LockState *s, const char *pin, gint64 now) {
    if (retry_after_ms(s, now)) return CHECK_WAIT;   /* not even tried: no oracle while waiting */
    if (pin_matches(s->hash, pin)) {
        s->failures = 0;
        s->wait_until = 0;
        lock_save(s);
        return CHECK_OK;
    }
    s->failures++;
    gint64 w = wait_for(s->failures);
    s->wait_until = w ? now + w : 0;
    lock_save(s);
    return CHECK_WRONG;
}

/* reason: "" ok, "format", "wrong_pin", "locked_out", "storage" */
static const char *lock_set_pin(LockState *s, const char *current, const char *pin, gint64 now) {
    if (*pin && !pin_format_ok(pin)) return "format";
    if (s->hash[0]) {
        CheckResult r = lock_check(s, current, now);
        if (r == CHECK_WAIT) return "locked_out";
        if (r == CHECK_WRONG) return "wrong_pin";
    }
    LockState next = *s;
    next.hash[0] = '\0';
    if (*pin && !hash_pin(pin, next.hash, sizeof(next.hash))) return "storage";
    next.failures = 0;
    next.wait_until = 0;
    if (!lock_save(&next)) return "storage";
    *s = next;
    return "";
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
        CheckResult r = g_lock.hash[0] ? lock_check(&g_lock, pin, now) : CHECK_OK;
        g_dbus_method_invocation_return_value(inv, result_variant(r == CHECK_OK,
            r == CHECK_OK ? "" : r == CHECK_WAIT ? "locked_out" : "wrong_pin", &g_lock, now));
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
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(lock_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0])
        g_dbus_connection_register_object(conn, "/org/zylos/Lock",
            info->interfaces[0], &lock_vtable, NULL, NULL, NULL);
    if (info) g_dbus_node_info_unref(info);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    lock_load(&g_lock);
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.Lock", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_lock_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
