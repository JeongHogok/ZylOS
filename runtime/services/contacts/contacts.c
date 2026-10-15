/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 연락처 서비스 (org.zylos.Contacts) — 연락처 앱·전화·메시지가 함께 쓰는 주소록
 * 수행범위: 전체·하나·검색, 만들기, 고치기(준 필드만 — 별표와 이름이 서로를
 *           지우지 않게), 삭제. 즐겨찾기는 starred. 저장은 /data/contacts 에만
 *           (0600, 원자적) — 사용자 파일(Documents)에 두지 않아 파일 권한이 있는
 *           앱이 주소록을 읽지 못한다
 * 의존방향: gio/gio.h (D-Bus), glib (GKeyFile)
 * SOLID: SRP — 주소록 보관만. 앱별 접근은 WAM 브리지(contacts 권한)
 * ────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <glib-unix.h>

#ifndef ZYL_CONTACTS_FILE
#define ZYL_CONTACTS_FILE "/data/contacts/contacts"
#endif

static const char *const FIELDS[] = { "name", "phone", "email", NULL };

static GKeyFile *load(void) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, ZYL_CONTACTS_FILE, G_KEY_FILE_NONE, NULL);
    return kf;
}

static bool save(GKeyFile *kf) {
    gsize len = 0;
    char *data = g_key_file_to_data(kf, &len, NULL);
    char *dir = g_path_get_dirname(ZYL_CONTACTS_FILE);
    g_mkdir_with_parents(dir, 0700);
    bool ok = g_file_set_contents_full(ZYL_CONTACTS_FILE, data, (gssize)len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, NULL);
    g_free(dir);
    g_free(data);
    return ok;
}

/* Same fields as the emulator's contacts service: { id, name, phone, email, starred } */
static GVariant *contact_variant(GKeyFile *kf, const char *id) {
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "id", g_variant_new_string(id));
    for (int i = 0; FIELDS[i]; i++) {
        char *v = g_key_file_get_string(kf, id, FIELDS[i], NULL);
        g_variant_builder_add(&d, "{sv}", FIELDS[i], g_variant_new_string(v ? v : ""));
        g_free(v);
    }
    g_variant_builder_add(&d, "{sv}", "starred",
                          g_variant_new_boolean(g_key_file_get_boolean(kf, id, "starred", NULL)));
    return g_variant_builder_end(&d);
}

/* Case-folded: the daemon runs in the C locale, where "Yuna" sorts before "alex" */
static char *name_key(GKeyFile *kf, const char *id) {
    char *name = g_key_file_get_string(kf, id, "name", NULL);
    char *key = g_utf8_casefold(name ? name : "", -1);
    g_free(name);
    return key;
}

static gint by_name(gconstpointer a, gconstpointer b, gpointer kf) {
    char *x = name_key(kf, *(char *const *)a);
    char *y = name_key(kf, *(char *const *)b);
    gint r = g_utf8_collate(x, y);
    g_free(x);
    g_free(y);
    return r;
}

/* query NULL: everyone; else name or phone containing it (case-insensitive) */
static GVariant *list(GKeyFile *kf, const char *query) {
    char **groups = g_key_file_get_groups(kf, NULL);
    GPtrArray *ids = g_ptr_array_new();
    for (char **g = groups; *g; g++) g_ptr_array_add(ids, *g);
    g_ptr_array_sort_with_data(ids, by_name, kf);
    char *q = query && *query ? g_utf8_casefold(query, -1) : NULL;
    GVariantBuilder out;
    g_variant_builder_init(&out, G_VARIANT_TYPE("aa{sv}"));
    for (guint i = 0; i < ids->len; i++) {
        const char *id = g_ptr_array_index(ids, i);
        if (q) {
            char *name = g_key_file_get_string(kf, id, "name", NULL);
            char *phone = g_key_file_get_string(kf, id, "phone", NULL);
            char *fold = g_utf8_casefold(name ? name : "", -1);
            bool hit = strstr(fold, q) || (phone && strstr(phone, query));
            g_free(fold);
            g_free(name);
            g_free(phone);
            if (!hit) continue;
        }
        g_variant_builder_add_value(&out, contact_variant(kf, id));
    }
    g_free(q);
    g_ptr_array_free(ids, TRUE);
    g_strfreev(groups);
    return g_variant_builder_end(&out);
}

/* Only the fields given change. create/update return NULL when nothing was stored. */
static void apply_fields(GKeyFile *kf, const char *id, GVariant *fields) {
    for (int i = 0; FIELDS[i]; i++) {
        const gchar *v = NULL;
        if (g_variant_lookup(fields, FIELDS[i], "&s", &v)) g_key_file_set_string(kf, id, FIELDS[i], v);
    }
    gboolean starred;
    if (g_variant_lookup(fields, "starred", "b", &starred)) g_key_file_set_boolean(kf, id, "starred", starred);
}

static GVariant *create(GVariant *fields) {
    GKeyFile *kf = load();
    char id[40];
    gint64 now = g_get_real_time() / 1000;
    do snprintf(id, sizeof(id), "c_%" G_GINT64_FORMAT, now++); while (g_key_file_has_group(kf, id));
    for (int i = 0; FIELDS[i]; i++) g_key_file_set_string(kf, id, FIELDS[i], "");
    g_key_file_set_boolean(kf, id, "starred", FALSE);
    apply_fields(kf, id, fields);
    GVariant *c = save(kf) ? contact_variant(kf, id) : NULL;
    g_key_file_free(kf);
    return c;
}

static GVariant *update(GVariant *fields) {
    const gchar *id = NULL;
    GKeyFile *kf = load();
    GVariant *c = NULL;
    if (g_variant_lookup(fields, "id", "&s", &id) && g_key_file_has_group(kf, id)) {
        apply_fields(kf, id, fields);
        if (save(kf)) c = contact_variant(kf, id);
    }
    g_key_file_free(kf);
    return c;
}

static bool remove_contact(const char *id) {
    GKeyFile *kf = load();
    bool ok = g_key_file_remove_group(kf, id, NULL) && save(kf);
    g_key_file_free(kf);
    return ok;
}

#ifndef ZYL_TEST
static const char *contacts_xml =
    "<node>"
    "  <interface name='org.zylos.Contacts'>"
    "    <method name='GetAll'><arg type='aa{sv}' name='contacts' direction='out'/></method>"
    "    <method name='GetById'><arg type='s' name='id' direction='in'/>"
    "      <arg type='a{sv}' name='contact' direction='out'/></method>"
    "    <method name='Search'><arg type='s' name='query' direction='in'/>"
    "      <arg type='aa{sv}' name='contacts' direction='out'/></method>"
    "    <method name='Create'><arg type='a{sv}' name='params' direction='in'/>"
    "      <arg type='a{sv}' name='contact' direction='out'/></method>"
    "    <method name='Update'><arg type='a{sv}' name='params' direction='in'/>"
    "      <arg type='a{sv}' name='contact' direction='out'/></method>"
    "    <method name='Delete'><arg type='s' name='id' direction='in'/>"
    "      <arg type='b' name='deleted' direction='out'/></method>"
    "  </interface>"
    "</node>";

static void handle(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)data;
    const gchar *s = NULL;
    if (g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) g_variant_get(params, "(&s)", &s);
    GVariant *arg = g_variant_is_of_type(params, G_VARIANT_TYPE("(a{sv})"))
                    ? g_variant_get_child_value(params, 0) : NULL;
    bool store = g_strcmp0(method, "Create") == 0 || g_strcmp0(method, "Update") == 0;
    GKeyFile *kf = load();
    GVariant *out = NULL;
    if (g_strcmp0(method, "GetAll") == 0) out = g_variant_new("(@aa{sv})", list(kf, NULL));
    else if (g_strcmp0(method, "Search") == 0) out = g_variant_new("(@aa{sv})", list(kf, s));
    else if (g_strcmp0(method, "GetById") == 0)
        out = g_variant_new("(@a{sv})", g_key_file_has_group(kf, s) ? contact_variant(kf, s)
                                        : g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
    else if (store) {
        GVariant *contact = method[0] == 'C' ? create(arg) : update(arg);
        if (contact) out = g_variant_new("(@a{sv})", contact);
    }
    else if (g_strcmp0(method, "Delete") == 0) out = g_variant_new("(b)", remove_contact(s));
    g_key_file_free(kf);
    if (arg) g_variant_unref(arg);
    if (store && !out)
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "contact not stored");
    else
        g_dbus_method_invocation_return_value(inv, out);
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(contacts_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/Contacts", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SESSION, "org.zylos.Contacts", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
