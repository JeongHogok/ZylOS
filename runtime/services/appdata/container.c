/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Service
 *
 * 역할: 앱 컨테이너 위치 (container.h)
 * 수행범위: id 검증, 경로, 루트와 같은 소유로 만들기
 * 의존방향: container.h, glib
 * SOLID: SRP — 컨테이너 위치 규칙만
 * ────────────────────────────────────────────────────────── */

#include "container.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>

bool zyl_container_valid_id(const char *id) {
    if (!id || !*id || strlen(id) > 128 || strcmp(id, ".") == 0 || strstr(id, "..")) return false;
    for (const char *p = id; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '.' && *p != '_' && *p != '-') return false;
    return true;
}

char *zyl_container_dir(const char *root, const char *id, const char *sub, bool make) {
    if (!zyl_container_valid_id(id)) return NULL;
    char *top = g_build_filename(root, id, NULL);
    char *dir = sub ? g_build_filename(top, sub, NULL) : g_strdup(top);
    bool ok = true;
    if (make) {
        struct stat st;
        bool fresh = stat(top, &st) != 0;
        ok = g_mkdir_with_parents(top, 0700) == 0;
        /* Owned like the root (the WAM's), whoever makes it first */
        if (ok && fresh && geteuid() == 0 && stat(root, &st) == 0)
            ok = chown(top, st.st_uid, st.st_gid) == 0;
        if (ok && sub) ok = g_mkdir_with_parents(dir, 0700) == 0;
    }
    g_free(top);
    if (!ok) g_clear_pointer(&dir, g_free);
    return dir;
}
