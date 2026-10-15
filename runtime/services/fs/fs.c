/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 파일 서비스 (org.zylos.FileSystem) — 사용자 파일(문서·사진·음악…)
 * 수행범위: 사용자 파일 루트 아래에서만 목록·읽기(텍스트, base64)·쓰기(텍스트,
 *           명시적 base64)·폴더 만들기·삭제·이름 바꾸기. 경로는 루트 상대:
 *           ".." 거부, 보호 경로(settings.json, .credentials, .system — 에뮬레이터
 *           filesystem.rs·apps/system/security.js 와 같은 규칙) 거부, 실제 경로가
 *           루트 밖이면(심볼릭 링크) 거부. 숨김(.으로 시작, lost+found)은 목록에서 뺌
 * 의존방향: gio/gio.h (D-Bus), glib (파일)
 * SOLID: SRP — 사용자 파일 접근만. 앱별 권한은 WAM 브리지(fs → storage/files.*)
 * ────────────────────────────────────────────────────────── */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include "../appdata/container.h"

#ifndef ZYL_FILES_ROOT
#define ZYL_FILES_ROOT "/data/users/0/files"
#endif
#ifndef ZYL_CONTAINERS_ROOT
#define ZYL_CONTAINERS_ROOT ZYL_CONTAINERS
#endif
#define MAX_READ (64 * 1024 * 1024)

static const char *const STANDARD_DIRS[] = { "Documents", "Pictures", "Music", "Downloads", NULL };

/* ─── Path policy ─── */
static bool protected_path(const char *rel) {
    char *lower = g_ascii_strdown(rel, -1);
    bool p = strcmp(lower, "settings.json") == 0 || g_str_has_prefix(lower, ".credentials") ||
             g_str_has_prefix(lower, ".system");
    g_free(lower);
    return p;
}

static bool inside_root(const char *base, const char *real) {
    char root[PATH_MAX];
    if (!realpath(base, root)) return false;
    size_t n = strlen(root);
    return strncmp(real, root, n) == 0 && (real[n] == '\0' || real[n] == '/');
}

/* The absolute path for an app's relative one, or NULL when it may not be
   touched. What does not exist yet (a new file, new folders) is judged by its
   nearest existing ancestor: the rest of the path has no ".." to climb with */
static char *resolve(const char *base, const char *rel) {
    if (!base || !rel || strstr(rel, "..")) return NULL;
    while (*rel == '/') rel++;
    if (protected_path(rel)) return NULL;
    char *full = g_build_filename(base, rel, NULL);
    char *probe = g_strdup(full);
    char real[PATH_MAX];
    bool ok = false;
    for (;;) {
        if (realpath(probe, real)) { ok = inside_root(base, real); break; }
        if (errno != ENOENT) break;
        char *up = g_path_get_dirname(probe);
        bool top = strcmp(up, probe) == 0;
        g_free(probe);
        probe = up;
        if (top) break;
    }
    g_free(probe);
    if (!ok) { g_free(full); return NULL; }
    return full;
}

static const char *file_type(const char *name, bool dir) {
    if (dir) return "folder";
    const char *dot = strrchr(name, '.');
    char *ext = g_ascii_strdown(dot ? dot + 1 : "", -1);
    static const struct { const char *type, *exts; } TYPES[] = {
        { "image", " jpg jpeg png gif bmp svg webp " }, { "video", " mp4 mkv avi mov webm " },
        { "audio", " mp3 wav ogg flac aac " },          { "document", " pdf doc docx txt rtf odt " },
        { "archive", " zip tar gz 7z rar " },           { "code", " c h js py rs java html css " },
    };
    const char *t = "unknown";
    char key[40];
    snprintf(key, sizeof(key), " %s ", ext);
    for (size_t i = 0; i < G_N_ELEMENTS(TYPES) && *ext; i++)
        if (strstr(TYPES[i].exts, key)) { t = TYPES[i].type; break; }
    g_free(ext);
    return t;
}

typedef struct { char *name; bool dir; guint64 size; gint64 mtime; } Entry;

static gint by_folder_then_name(gconstpointer a, gconstpointer b) {
    const Entry *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return g_ascii_strcasecmp(x->name, y->name);
}

/* Same fields as the emulator: { name, is_dir, size, modified (epoch s), file_type } */
static GVariant *list_dir(const char *base, const char *rel) {
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    char *dir = resolve(base, rel);
    GDir *d = dir ? g_dir_open(dir, 0, NULL) : NULL;
    GArray *entries = g_array_new(FALSE, TRUE, sizeof(Entry));
    const char *name;
    while (d && (name = g_dir_read_name(d))) {
        if (name[0] == '.' || strcmp(name, "lost+found") == 0 || protected_path(name)) continue;
        char *p = g_build_filename(dir, name, NULL);
        struct stat st;
        if (lstat(p, &st) == 0 && !S_ISLNK(st.st_mode)) {
            Entry e = { g_strdup(name), S_ISDIR(st.st_mode), (guint64)st.st_size, (gint64)st.st_mtime };
            g_array_append_val(entries, e);
        }
        g_free(p);
    }
    if (d) g_dir_close(d);
    g_array_sort(entries, by_folder_then_name);
    for (guint i = 0; i < entries->len; i++) {
        Entry *e = &g_array_index(entries, Entry, i);
        char mtime[24];
        snprintf(mtime, sizeof(mtime), "%" G_GINT64_FORMAT, e->mtime);
        GVariantBuilder f;
        g_variant_builder_init(&f, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&f, "{sv}", "name", g_variant_new_string(e->name));
        g_variant_builder_add(&f, "{sv}", "is_dir", g_variant_new_boolean(e->dir));
        g_variant_builder_add(&f, "{sv}", "size", g_variant_new_uint64(e->size));
        g_variant_builder_add(&f, "{sv}", "modified", g_variant_new_string(mtime));
        g_variant_builder_add(&f, "{sv}", "file_type", g_variant_new_string(file_type(e->name, e->dir)));
        g_variant_builder_add_value(&list, g_variant_builder_end(&f));
        g_free(e->name);
    }
    g_array_free(entries, TRUE);
    g_free(dir);
    return g_variant_builder_end(&list);
}

static char *read_file(const char *base, const char *rel, gsize *len) {
    char *path = resolve(base, rel), *data = NULL;
    struct stat st;
    if (path && stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= MAX_READ)
        g_file_get_contents(path, &data, len, NULL);
    g_free(path);
    return data;
}

static bool write_file(const char *base, const char *rel, const char *content, const char *encoding) {
    char *path = resolve(base, rel);
    if (!path) return false;
    guchar *bytes = NULL;
    gsize len = strlen(content);
    if (g_strcmp0(encoding, "base64") == 0) bytes = g_base64_decode(content, &len);
    char *parent = g_path_get_dirname(path);
    bool ok = g_mkdir_with_parents(parent, 0700) == 0 &&
              g_file_set_contents_full(path, bytes ? (const char *)bytes : content, (gssize)len,
                                       G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
    g_free(parent);
    g_free(bytes);
    g_free(path);
    return ok;
}

static bool remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        GDir *d = g_dir_open(path, 0, NULL);
        const char *name;
        while (d && (name = g_dir_read_name(d))) {
            char *child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        if (d) g_dir_close(d);
        return g_rmdir(path) == 0;
    }
    return g_unlink(path) == 0;
}

static bool remove_path(const char *base, const char *rel) {
    char *path = resolve(base, rel);
    char root[PATH_MAX], real[PATH_MAX];
    /* Never the root itself */
    bool ok = path && realpath(path, real) && realpath(base, root) &&
              strcmp(real, root) != 0 && remove_tree(path);
    g_free(path);
    return ok;
}

static bool rename_path(const char *base, const char *from, const char *to) {
    char *a = resolve(base, from), *b = resolve(base, to);
    bool ok = a && b && g_rename(a, b) == 0;
    g_free(a);
    g_free(b);
    return ok;
}

static bool make_dir(const char *base, const char *rel) {
    char *path = resolve(base, rel);
    bool ok = path && g_mkdir_with_parents(path, 0700) == 0;
    g_free(path);
    return ok;
}

/* Where a request's paths live: the user's files for system apps (container
   ""), else the app's own container — the bridge names it, never the app. */
static char *root_for(const char *container) {
    if (!container || !*container) return g_strdup(ZYL_FILES_ROOT);
    return zyl_container_dir(ZYL_CONTAINERS_ROOT, container, "files", true);
}

/* The folders apps expect (camera → Pictures, notes → Documents, music → Music) */
static void ensure_layout(void) {
    g_mkdir_with_parents(ZYL_FILES_ROOT, 0700);
    for (int i = 0; STANDARD_DIRS[i]; i++) {
        char *p = g_build_filename(ZYL_FILES_ROOT, STANDARD_DIRS[i], NULL);
        g_mkdir_with_parents(p, 0700);
        g_free(p);
    }
}

#ifndef ZYL_TEST
/* ─── D-Bus ─── */
static const char *fs_xml =
    "<node>"
    "  <interface name='org.zylos.FileSystem'>"
    "    <method name='GetDirectory'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='aa{sv}' name='entries' direction='out'/>"
    "    </method>"
    "    <method name='GetAllData'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='a{sv}' name='data' direction='out'/>"
    "    </method>"
    "    <method name='GetFileContent'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='s' name='content' direction='out'/>"
    "    </method>"
    "    <method name='ReadBinary'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='s' name='base64' direction='out'/>"
    "    </method>"
    "    <method name='WriteFile'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='a{sv}' name='params' direction='in'/>"
    "      <arg type='b' name='written' direction='out'/>"
    "    </method>"
    "    <method name='Mkdir'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='b' name='made' direction='out'/>"
    "    </method>"
    "    <method name='Remove'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='b' name='removed' direction='out'/>"
    "    </method>"
    "    <method name='Rename'>"
    "      <arg type='s' name='container' direction='in'/>"
    "      <arg type='s' name='old_path' direction='in'/>"
    "      <arg type='s' name='new_path' direction='in'/>"
    "      <arg type='b' name='renamed' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void fail(GDBusMethodInvocation *inv, const char *what) {
    g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", what);
}

static void handle(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)data;
    /* First argument of every method: whose files (the bridge's container) */
    const gchar *container = NULL, *p = NULL;
    g_variant_get_child(params, 0, "&s", &container);
    if (g_str_has_prefix(g_variant_get_type_string(params), "(ss")) g_variant_get_child(params, 1, "&s", &p);
    char *root = root_for(container);
    if (!root) { fail(inv, "no container"); return; }

    if (g_strcmp0(method, "GetDirectory") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@aa{sv})", list_dir(root, p)));
    } else if (g_strcmp0(method, "GetAllData") == 0) {
        /* The emulator's shape: { tree: { "/": entries }, unixTree: {}, fileContents: {} } */
        GVariantBuilder tree, d;
        g_variant_builder_init(&tree, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&tree, "{sv}", "/", list_dir(root, "/"));
        g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&d, "{sv}", "tree", g_variant_builder_end(&tree));
        g_variant_builder_add(&d, "{sv}", "unixTree", g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
        g_variant_builder_add(&d, "{sv}", "fileContents", g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sv})", &d));
    } else if (g_strcmp0(method, "GetFileContent") == 0 || g_strcmp0(method, "ReadBinary") == 0) {
        gsize len = 0;
        char *bytes = read_file(root, p, &len);
        bool text = g_strcmp0(method, "GetFileContent") == 0;
        if (!bytes) fail(inv, "cannot read");
        else if (text && !g_utf8_validate(bytes, (gssize)len, NULL)) fail(inv, "not text");
        else {
            char *out = text ? bytes : g_base64_encode((const guchar *)bytes, len);
            g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", out));
            if (!text) g_free(out);
        }
        g_free(bytes);
    } else if (g_strcmp0(method, "WriteFile") == 0) {
        GVariant *d = g_variant_get_child_value(params, 1);
        const gchar *wp = NULL, *content = NULL, *encoding = NULL;
        g_variant_lookup(d, "path", "&s", &wp);
        g_variant_lookup(d, "content", "&s", &content);
        g_variant_lookup(d, "encoding", "&s", &encoding);
        bool ok = wp && content && write_file(root, wp, content, encoding);
        g_variant_unref(d);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ok));
    } else if (g_strcmp0(method, "Mkdir") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", make_dir(root, p)));
    } else if (g_strcmp0(method, "Remove") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", remove_path(root, p)));
    } else if (g_strcmp0(method, "Rename") == 0) {
        const gchar *from = NULL, *to = NULL;
        g_variant_get(params, "(&s&s&s)", &container, &from, &to);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", rename_path(root, from, to)));
    }
    g_free(root);
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(fs_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/FileSystem", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    ensure_layout();
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.FileSystem", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
