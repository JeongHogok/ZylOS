/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Library
 *
 * 역할: fscrypt v2 ioctl — fscrypt.h 의 구현
 * 수행범위: 키 올리기, 정책 걸기·읽기, 키가 올라 있는지, 열릴 때까지 기다리기
 * 의존방향: linux/fscrypt.h, libc, gio
 * SOLID: SRP — 커널 인터페이스만
 * ────────────────────────────────────────────────────────── */

#include "fscrypt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fscrypt.h>
#include <gio/gio.h>

static int open_dir(const char *path) {
    return open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

bool zyl_fscrypt_add_key(const char *path, const unsigned char key[ZYL_FSCRYPT_KEY_LEN],
                         unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    int fd = open_dir(path);
    if (fd < 0) return false;
    struct fscrypt_add_key_arg *arg = calloc(1, sizeof(*arg) + ZYL_FSCRYPT_KEY_LEN);
    bool ok = false;
    if (arg) {
        arg->key_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
        arg->raw_size = ZYL_FSCRYPT_KEY_LEN;
        memcpy(arg->raw, key, ZYL_FSCRYPT_KEY_LEN);
        ok = ioctl(fd, FS_IOC_ADD_ENCRYPTION_KEY, arg) == 0;
        if (ok) memcpy(id, arg->key_spec.u.identifier, ZYL_FSCRYPT_ID_LEN);
        explicit_bzero(arg->raw, ZYL_FSCRYPT_KEY_LEN);
        free(arg);
    }
    int saved = errno;
    close(fd);
    errno = saved;
    return ok;
}

bool zyl_fscrypt_set_policy(const char *dir, const unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    int fd = open_dir(dir);
    if (fd < 0) return false;
    struct fscrypt_policy_v2 p;
    memset(&p, 0, sizeof(p));
    p.version = FSCRYPT_POLICY_V2;
    p.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS;
    p.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS;
    p.flags = FSCRYPT_POLICY_FLAGS_PAD_32;
    memcpy(p.master_key_identifier, id, ZYL_FSCRYPT_ID_LEN);
    bool ok = ioctl(fd, FS_IOC_SET_ENCRYPTION_POLICY, &p) == 0;
    int saved = errno;
    close(fd);
    errno = saved;
    return ok;
}

int zyl_fscrypt_get_policy(const char *dir, unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    int fd = open_dir(dir);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct fscrypt_get_policy_ex_arg arg;
    memset(&arg, 0, sizeof(arg));
    arg.policy_size = sizeof(arg.policy);
    int r = ioctl(fd, FS_IOC_GET_ENCRYPTION_POLICY_EX, &arg);
    int saved = errno;
    close(fd);
    if (r != 0)    /* ENODATA: not encrypted; ENOTTY/EOPNOTSUPP: cannot be (tmpfs, overlay) */
        return saved == ENODATA || saved == ENOTTY || saved == EOPNOTSUPP ? 0 : -1;
    if (arg.policy.version != FSCRYPT_POLICY_V2) return -1;
    memcpy(id, arg.policy.v2.master_key_identifier, ZYL_FSCRYPT_ID_LEN);
    return 1;
}

int zyl_fscrypt_key_present(const char *path, const unsigned char id[ZYL_FSCRYPT_ID_LEN]) {
    int fd = open_dir(path);
    if (fd < 0) return -1;
    struct fscrypt_get_key_status_arg arg;
    memset(&arg, 0, sizeof(arg));
    arg.key_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
    memcpy(arg.key_spec.u.identifier, id, ZYL_FSCRYPT_ID_LEN);
    int r = ioctl(fd, FS_IOC_GET_ENCRYPTION_KEY_STATUS, &arg);
    close(fd);
    if (r != 0) return -1;
    return arg.status == FSCRYPT_KEY_STATUS_PRESENT ? 1 : 0;
}

bool zyl_fscrypt_unlocked(const char *dir) {
    unsigned char id[ZYL_FSCRYPT_ID_LEN];
    int p = zyl_fscrypt_get_policy(dir, id);
    if (p == 0) return true;
    return p == 1 && zyl_fscrypt_key_present(dir, id) == 1;
}

typedef struct {
    char            *dir;
    ZylUnlockedFn    fn;
    void            *data;
    GDBusConnection *bus;
    guint            sub;
} Waiter;

static void waiter_done(Waiter *w) {
    g_dbus_connection_signal_unsubscribe(w->bus, w->sub);
    g_object_unref(w->bus);
    w->fn(w->data);
    g_free(w->dir);
    g_free(w);
}

static void on_storage_unlocked(GDBusConnection *c, const gchar *sender, const gchar *path,
                                const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)signal; (void)params;
    Waiter *w = data;
    if (zyl_fscrypt_unlocked(w->dir)) waiter_done(w);
}

void zyl_fscrypt_when_unlocked(const char *dir, ZylUnlockedFn fn, void *data) {
    if (zyl_fscrypt_unlocked(dir)) { fn(data); return; }
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!bus) { g_critical("fscrypt: %s is locked and there is no bus to hear it open", dir); return; }
    Waiter *w = g_new0(Waiter, 1);
    w->dir = g_strdup(dir);
    w->fn = fn;
    w->data = data;
    w->bus = bus;
    w->sub = g_dbus_connection_signal_subscribe(bus, "org.zylos.Lock", "org.zylos.Lock",
        "StorageUnlocked", "/org/zylos/Lock", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_storage_unlocked, w, NULL);
    g_message("fscrypt: %s is locked until the first unlock", dir);
    if (zyl_fscrypt_unlocked(dir)) waiter_done(w);   /* opened before we listened */
}
