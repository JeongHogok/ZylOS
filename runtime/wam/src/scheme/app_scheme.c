/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Service
 *
 * 역할: 앱 출처 이름 해석 (app_scheme.h)
 * 수행범위: 경로 → 파일, 확장자 → MIME, 진입 URI
 * 의존방향: app_scheme.h
 * SOLID: SRP — 해석만
 * ────────────────────────────────────────────────────────── */

#include "app_scheme.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SHARED_PREFIX "/shared/"

/* real is root itself or inside it */
static gboolean within(const char *real, const char *root) {
    size_t n = strlen(root);
    return strncmp(real, root, n) == 0 && (real[n] == '/' || real[n] == '\0');
}

char *zyl_app_scheme_file(const char *app_dir, const char *shared_dir, const char *path) {
    if (!app_dir || !shared_dir || !path || path[0] != '/') return NULL;
    gchar **segs = g_strsplit(path, "/", -1);
    gboolean dotdot = FALSE;
    for (gchar **s = segs; *s; s++) if (strcmp(*s, "..") == 0) dotdot = TRUE;
    g_strfreev(segs);
    if (dotdot) return NULL;

    gboolean shared = g_str_has_prefix(path, SHARED_PREFIX);
    const char *base = shared ? shared_dir : app_dir;
    const char *rel = shared ? path + strlen(SHARED_PREFIX) : path + 1;
    char *full = g_build_filename(base, rel, NULL);
    char real[PATH_MAX], root[PATH_MAX];
    struct stat st;
    char *out = NULL;
    /* A link inside the app pointing out of it is refused like a .. */
    if (realpath(full, real) && realpath(base, root) && within(real, root) &&
        stat(real, &st) == 0 && S_ISREG(st.st_mode))
        out = g_strdup(real);
    g_free(full);
    return out;
}

const char *zyl_app_scheme_mime(const char *file) {
    static const char *const table[][2] = {
        { ".html", "text/html" }, { ".htm", "text/html" },
        { ".js", "text/javascript" }, { ".mjs", "text/javascript" },
        { ".css", "text/css" }, { ".json", "application/json" },
        { ".svg", "image/svg+xml" }, { ".png", "image/png" },
        { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" },
        { ".gif", "image/gif" }, { ".webp", "image/webp" }, { ".ico", "image/x-icon" },
        { ".woff2", "font/woff2" }, { ".woff", "font/woff" },
        { ".ttf", "font/ttf" }, { ".otf", "font/otf" },
        { ".mp3", "audio/mpeg" }, { ".ogg", "audio/ogg" }, { ".wav", "audio/wav" },
        { ".mp4", "video/mp4" }, { ".webm", "video/webm" },
        { ".txt", "text/plain" },
    };
    const char *dot = file ? strrchr(file, '.') : NULL;
    if (dot && !strchr(dot, '/')) {
        for (size_t i = 0; i < G_N_ELEMENTS(table); i++)
            if (g_ascii_strcasecmp(dot, table[i][0]) == 0) return table[i][1];
    }
    return "application/octet-stream";
}

char *zyl_app_scheme_uri(const char *app_id, const char *entry) {
    return g_strdup_printf(ZYL_APP_SCHEME "://%s/%s", app_id, entry && *entry ? entry : "index.html");
}
