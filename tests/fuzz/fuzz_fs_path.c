/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Fuzz Test
 *
 * 역할: 파일 서비스의 경로 판정(runtime/services/fs/fs.c 의 resolve) 퍼징
 * 수행범위: 앱이 준 상대 경로가 무엇이든 — 결과는 루트 안, 보호 경로·".." 는
 *           거절, 루트 밖을 가리키는 심볼릭 링크로 나가지 못함
 * 의존방향: 실제 fs.c (ZYL_TEST 로 main 제외), runtime/lib/container.c, glib/gio
 * SOLID: SRP — 경로 판정 퍼징만 담당
 *
 * 빌드·실행: .github/workflows/native.yml 의 fuzz 잡
 * ────────────────────────────────────────────────────────── */

#define ZYL_TEST
#define ROOT_ "/tmp/zyl-fuzz-fs"
#define ZYL_FILES_ROOT      ROOT_ "/files"
#define ZYL_CONTAINERS_ROOT ROOT_ "/appdata"
#include "../../runtime/services/fs/fs.c"
#include "../../runtime/lib/container.c"
#include <assert.h>
#include <stdint.h>
#include <unistd.h>

static char root_real[PATH_MAX];

static void setup(void) {
    if (root_real[0]) return;
    ensure_layout();
    /* A link out of the root: following it must never be allowed */
    symlink("/etc", ZYL_FILES_ROOT "/escape");
    assert(realpath(ZYL_FILES_ROOT, root_real));
    char *out = resolve(ZYL_FILES_ROOT, "escape/passwd");
    assert(out == NULL);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    setup();
    char *rel = g_strndup((const char *)data, size);   /* stops at an embedded NUL */
    char *full = resolve(ZYL_FILES_ROOT, rel);
    if (full) {
        const char *r = rel;
        while (*r == '/') r++;
        assert(!strstr(rel, ".."));
        assert(!protected_path(r));
        assert(g_str_has_prefix(full, ZYL_FILES_ROOT));
        /* Whatever part exists resolves inside the root */
        char real[PATH_MAX];
        char *probe = g_strdup(full);
        while (!realpath(probe, real)) {
            char *up = g_path_get_dirname(probe);
            g_free(probe);
            probe = up;
        }
        size_t n = strlen(root_real);
        assert(strncmp(real, root_real, n) == 0 && (real[n] == '\0' || real[n] == '/'));
        g_free(probe);
        g_free(full);
    }
    g_free(rel);
    return 0;
}
