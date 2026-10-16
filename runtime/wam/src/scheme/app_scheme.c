/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Service
 *
 * 역할: 앱 출처 이름 해석 (app_scheme.h)
 * 수행범위: 경로 → 파일, 확장자 → MIME, 진입 URI, 앱의 CSP
 * 의존방향: app_scheme.h
 * SOLID: SRP — 해석만
 * ────────────────────────────────────────────────────────── */

#include "app_scheme.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const OS_DIRS[] = { "shared", "keyboard", NULL };

/* The OS directory a path is in ("shared", "keyboard"), or NULL */
static const char *os_dir_of(const char *path) {
    for (int i = 0; OS_DIRS[i]; i++) {
        size_t n = strlen(OS_DIRS[i]);
        if (path[0] == '/' && strncmp(path + 1, OS_DIRS[i], n) == 0 && path[1 + n] == '/')
            return OS_DIRS[i];
    }
    return NULL;
}

gboolean zyl_app_scheme_is_os(const char *path) {
    return path && os_dir_of(path) != NULL;
}

/* real is root itself or inside it */
static gboolean within(const char *real, const char *root) {
    size_t n = strlen(root);
    return strncmp(real, root, n) == 0 && (real[n] == '/' || real[n] == '\0');
}

char *zyl_app_scheme_file(const char *app_dir, const char *os_dir, const char *path) {
    if (!app_dir || !os_dir || !path || path[0] != '/') return NULL;
    gchar **segs = g_strsplit(path, "/", -1);
    gboolean dotdot = FALSE;
    for (gchar **s = segs; *s; s++) if (strcmp(*s, "..") == 0) dotdot = TRUE;
    g_strfreev(segs);
    if (dotdot) return NULL;

    const char *os = os_dir_of(path);
    char *os_base = os ? g_build_filename(os_dir, os, NULL) : NULL;
    const char *base = os ? os_base : app_dir;
    const char *rel = os ? path + 2 + strlen(os) : path + 1;
    char *full = g_build_filename(base, rel, NULL);
    char real[PATH_MAX], root[PATH_MAX];
    struct stat st;
    char *out = NULL;
    /* A link inside the app pointing out of it is refused like a .. */
    if (realpath(full, real) && realpath(base, root) && within(real, root) &&
        stat(real, &st) == 0 && S_ISREG(st.st_mode))
        out = g_strdup(real);
    g_free(full);
    g_free(os_base);
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

gboolean zyl_app_scheme_is_own(const char *app_id, const char *uri) {
    if (!app_id || !*app_id || !uri) return FALSE;
    char *origin = g_strdup_printf(ZYL_APP_SCHEME "://%s", app_id);
    size_t n = strlen(origin);
    gboolean own = strncmp(uri, origin, n) == 0 && (uri[n] == '/' || uri[n] == '\0');
    g_free(origin);
    return own;
}

gboolean zyl_app_scheme_may_load(const char *app_id, const char *uri) {
    if (!uri) return FALSE;
    if (zyl_app_scheme_is_own(app_id, uri)) return TRUE;
    if (strcmp(uri, "about:blank") == 0 || strcmp(uri, "about:srcdoc") == 0) return TRUE;
    return g_str_has_prefix(uri, "blob:") && zyl_app_scheme_is_own(app_id, uri + 5);
}

char *zyl_app_scheme_csp(gboolean network) {
    const char *web = network ? " https:" : "";
    return g_strdup_printf(
        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data: blob:%s; media-src 'self' blob:%s; connect-src 'self'%s; "
        "font-src 'self'%s; frame-src 'self'; object-src 'none'",
        web, web, network ? " https: wss:" : "", web);
}
