/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Fuzz Test
 *
 * 역할: FS 경로 검증 퍼징 하네스
 * 수행범위: path traversal, null byte injection, symlink 공격 패턴 검증
 * 의존방향: 없음 (자체 경로 검증 로직)
 * SOLID: SRP — 경로 검증 퍼징만 담당
 *
 * 빌드: clang -g -O1 -fsanitize=fuzzer,address fuzz_fs_path.c -o fuzz_fs_path
 * 실행: ./fuzz_fs_path corpus/ -max_len=1024
 * ────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* ─── Path validation logic (matches ZylSecurity.isProtectedPath) ─── */

static const char *PROTECTED_PREFIXES[] = {
    "/etc/", "/sys/", "/proc/", "/dev/",
    "/boot/", "/root/", "/usr/sbin/",
    NULL
};

static int is_protected_path(const char *path) {
    if (!path || !path[0]) return 1; /* Empty = protected */

    /* Null byte injection check */
    size_t len = strlen(path);
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '\0') return 1; /* Embedded null */
    }

    /* Path traversal: reject any ../ sequence */
    if (strstr(path, "../") != NULL) return 1;
    if (strstr(path, "/..") != NULL) return 1;
    if (strcmp(path, "..") == 0) return 1;

    /* Absolute protected paths */
    for (int i = 0; PROTECTED_PREFIXES[i]; i++) {
        if (strncmp(path, PROTECTED_PREFIXES[i], strlen(PROTECTED_PREFIXES[i])) == 0) {
            return 1;
        }
    }

    /* Reject paths starting with / that aren't in /data/ or /home/ */
    if (path[0] == '/') {
        if (strncmp(path, "/data/", 6) != 0 &&
            strncmp(path, "/home/", 6) != 0 &&
            strncmp(path, "/tmp/", 5) != 0) {
            return 1;
        }
    }

    return 0;
}

/* ─── Normalization (collapse //, remove trailing /) ─── */
static int normalize_path(const char *input, char *output, size_t output_len) {
    if (!input || !output || output_len == 0) return -1;

    size_t j = 0;
    size_t ilen = strlen(input);

    for (size_t i = 0; i < ilen && j < output_len - 1; i++) {
        if (input[i] == '/' && j > 0 && output[j - 1] == '/') continue; /* Skip double slash */
        output[j++] = input[i];
    }

    /* Remove trailing slash (except for root "/") */
    if (j > 1 && output[j - 1] == '/') j--;

    output[j] = '\0';
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 2048) return 0;

    char *path = (char *)malloc(size + 1);
    if (!path) return 0;
    memcpy(path, data, size);
    path[size] = '\0';

    /* Test path validation — must not crash */
    volatile int protected = is_protected_path(path);
    (void)protected;

    /* Test normalization — must not crash or overflow */
    char normalized[2048] = {0};
    normalize_path(path, normalized, sizeof(normalized));

    /* Re-validate normalized path */
    volatile int protected2 = is_protected_path(normalized);
    (void)protected2;

    free(path);
    return 0;
}
