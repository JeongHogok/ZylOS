/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - HAL (Linux)
 *
 * 역할: Board HAL Linux 구현 — 이 기기가 무엇인지
 * 수행범위: 모델(디바이스 트리 model, 없으면 DMI product_name), SoC(디바이스
 *           트리 compatible 의 마지막 — SoC 를 가리킨다, 없으면 /proc/cpuinfo 의
 *           model name), 메모리(/proc/meminfo MemTotal), 커널(uname)
 * 의존방향: hal.h (Domain), glib, uname
 * SOLID: SRP — 기기 식별 정보 읽기만. 표시 형식은 서비스·앱 몫
 * ────────────────────────────────────────────────────────── */

#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <glib.h>

#ifndef ZYL_SYSFS_ROOT
#define ZYL_SYSFS_ROOT ""
#endif

static char *read_file(const char *path, gsize *len) {
    char *text = NULL;
    return g_file_get_contents(path, &text, len, NULL) ? text : NULL;
}

/* Device tree strings are NUL-terminated; "compatible" is a list of them */
static void board_soc(char *out, size_t size) {
    gsize len = 0;
    char *compat = read_file(ZYL_SYSFS_ROOT "/proc/device-tree/compatible", &len);
    if (compat && len > 1) {
        const char *last = compat;
        for (gsize i = 0; i + 1 < len; i++) if (compat[i] == '\0') last = compat + i + 1;
        g_strlcpy(out, last, size);
    } else {
        char *info = read_file(ZYL_SYSFS_ROOT "/proc/cpuinfo", NULL);
        char *line = info ? strstr(info, "model name") : NULL;
        char *colon = line ? strchr(line, ':') : NULL;
        if (colon) {
            char *end = strchr(colon, '\n');
            if (end) *end = '\0';
            g_strlcpy(out, g_strstrip(colon + 1), size);
        }
        g_free(info);
    }
    g_free(compat);
}

static bool board_get(ZylBoardInfo *out) {
    memset(out, 0, sizeof(*out));
    char *model = read_file(ZYL_SYSFS_ROOT "/proc/device-tree/model", NULL);
    if (!model) model = read_file(ZYL_SYSFS_ROOT "/sys/class/dmi/id/product_name", NULL);
    if (model) g_strlcpy(out->model, g_strstrip(model), sizeof(out->model));
    g_free(model);
    board_soc(out->soc, sizeof(out->soc));
    char *mem = read_file(ZYL_SYSFS_ROOT "/proc/meminfo", NULL);
    char *total = mem ? strstr(mem, "MemTotal:") : NULL;
    if (total) out->ram_bytes = (uint64_t)strtoull(total + 9, NULL, 10) * 1024;
    g_free(mem);
    struct utsname u;
    if (uname(&u) == 0) g_strlcpy(out->kernel, u.release, sizeof(out->kernel));
    return true;
}

static const ZylBoardHal board_hal = { .get = board_get };

const ZylBoardHal *zyl_hal_board(void) { return &board_hal; }
