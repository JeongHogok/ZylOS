/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Library
 *
 * 역할: 사용자 데이터의 파일 단위 암호화(fscrypt v2) — 커널에 키를 올리고, 빈
 *       디렉터리에 정책을 걸고, 지금 열려 있는지 묻고, 열릴 때를 기다린다
 * 수행범위: FS_IOC_ADD_ENCRYPTION_KEY / SET·GET_ENCRYPTION_POLICY_EX /
 *           GET_ENCRYPTION_KEY_STATUS 감싸기, 잠금 서비스의 StorageUnlocked 기다리기.
 *           키를 무엇으로 감싸 둘지(PIN)·어느 디렉터리를 잠글지는 잠금 서비스
 *           (runtime/services/lock)의 일
 * 의존방향: linux/fscrypt.h (ioctl), gio (org.zylos.Lock 시그널)
 * SOLID: SRP — 커널 fscrypt 인터페이스만. 쓰는 곳: lock(키·정책), credential·messaging·
 *        user(잠긴 동안 기다리기)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_FSCRYPT_H
#define ZYL_FSCRYPT_H

#include <stdbool.h>

#define ZYL_FSCRYPT_KEY_LEN 64      /* raw master key (AES-256-XTS needs 64) */
#define ZYL_FSCRYPT_ID_LEN  16      /* the kernel's key identifier */

/* Puts key on the filesystem that holds path; id: the identifier it is known
   by (what a policy names). FALSE on failure (errno set) */
bool zyl_fscrypt_add_key(const char *path, const unsigned char key[ZYL_FSCRYPT_KEY_LEN],
                         unsigned char id[ZYL_FSCRYPT_ID_LEN]);

/* Gives the empty directory dir the v2 policy of key id — AES-256-XTS for
   contents, AES-256-CTS for names. FALSE (errno set) if it is not empty, already
   has a policy, or the filesystem cannot (no "encrypt" feature) */
bool zyl_fscrypt_set_policy(const char *dir, const unsigned char id[ZYL_FSCRYPT_ID_LEN]);

/* 1: dir has a v2 policy (id filled), 0: not encrypted (or not there, or a
   filesystem without fscrypt), -1: cannot tell */
int zyl_fscrypt_get_policy(const char *dir, unsigned char id[ZYL_FSCRYPT_ID_LEN]);

/* 1: key id is on path's filesystem (unlocked), 0: it is not, -1: cannot tell */
int zyl_fscrypt_key_present(const char *path, const unsigned char id[ZYL_FSCRYPT_ID_LEN]);

/* Whether dir can be read and written now: not encrypted, or its key is there */
bool zyl_fscrypt_unlocked(const char *dir);

/* Calls fn(data) once, when dir is unlocked: now if it is, else when the lock
   service has opened the user data (org.zylos.Lock StorageUnlocked — the first
   right PIN after boot) */
typedef void (*ZylUnlockedFn)(void *data);
void zyl_fscrypt_when_unlocked(const char *dir, ZylUnlockedFn fn, void *data);

#endif /* ZYL_FSCRYPT_H */
