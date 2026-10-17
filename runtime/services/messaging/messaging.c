/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 메시지 서비스 (org.zylos.Messaging) — 문자 대화와 그 기록
 * 수행범위: 대화 목록(최근 순), 대화의 메시지, 보내기(전화 서비스의 SendSms —
 *           결과를 sent/failed 로 그대로 기록), 받기(전화 서비스의 SmsReceived
 *           를 기록하고 MessageReceived 로 알림), 읽음, 대화 삭제.
 *           대화 하나 = /data/messages/<대화 id> 파일 하나(0600, 원자적).
 *           /data/messages 는 사용자 데이터라 부팅 뒤 첫 잠금 해제 전에는 쓸 수 없다
 *           (fscrypt) — 그동안 온 문자는 /data/messages-pending 에 두었다가 열리면
 *           대화로 옮긴다
 * 의존방향: gio/gio.h (D-Bus), org.zylos.Telephony (모뎀), runtime/lib/fscrypt
 * SOLID: SRP — 문자 보관만. 발송·수신 자체는 전화 서비스, 이름은 연락처 몫
 * ────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glib-unix.h>

#include "fscrypt.h"

#ifndef ZYL_MESSAGES_DIR
#define ZYL_MESSAGES_DIR "/data/messages"
#endif
/* Readable before the PIN, as on any phone: only what came in while locked */
#ifndef ZYL_PENDING_FILE
#define ZYL_PENDING_FILE "/data/messages-pending/received"
#endif

#define HEAD "thread"   /* the thread's own group; every other group is a message */

/* thread_<letters and digits of the number> — also a safe file name */
static char *thread_id_for(const char *number) {
    GString *s = g_string_new("thread_");
    for (const char *p = number; p && *p; p++)
        if (g_ascii_isalnum(*p)) g_string_append_c(s, *p);
    if (s->len == strlen("thread_")) g_string_append(s, "unknown");
    return g_string_free(s, FALSE);
}

/* An id from an app names a file: nothing but thread_[A-Za-z0-9]+ */
static bool valid_thread_id(const char *id) {
    if (!id || !g_str_has_prefix(id, "thread_") || !id[strlen("thread_")]) return false;
    for (const char *p = id + strlen("thread_"); *p; p++)
        if (!g_ascii_isalnum(*p)) return false;
    return true;
}

static GKeyFile *load_thread(const char *id) {
    if (!valid_thread_id(id)) return NULL;
    char *path = g_build_filename(ZYL_MESSAGES_DIR, id, NULL);
    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        kf = NULL;
    }
    g_free(path);
    return kf;
}

static bool save_thread(const char *id, GKeyFile *kf) {
    gsize len = 0;
    char *data = g_key_file_to_data(kf, &len, NULL);
    char *path = g_build_filename(ZYL_MESSAGES_DIR, id, NULL);
    g_mkdir_with_parents(ZYL_MESSAGES_DIR, 0700);
    bool ok = g_file_set_contents_full(path, data, (gssize)len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, NULL);
    g_free(path);
    g_free(data);
    return ok;
}

static void add_string(GVariantBuilder *d, const char *key, GKeyFile *kf, const char *group) {
    char *v = g_key_file_get_string(kf, group, key, NULL);
    g_variant_builder_add(d, "{sv}", key, g_variant_new_string(v ? v : ""));
    g_free(v);
}

/* { id, text, sent, time, status } — status: sent | failed | received */
static GVariant *message_variant(GKeyFile *kf, const char *mid) {
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "id", g_variant_new_string(mid));
    add_string(&d, "text", kf, mid);
    add_string(&d, "status", kf, mid);
    g_variant_builder_add(&d, "{sv}", "sent", g_variant_new_boolean(g_key_file_get_boolean(kf, mid, "sent", NULL)));
    g_variant_builder_add(&d, "{sv}", "time", g_variant_new_int64(g_key_file_get_int64(kf, mid, "time", NULL)));
    return g_variant_builder_end(&d);
}

/* { id, number, lastTime, lastMessage, unread } */
static GVariant *thread_variant(GKeyFile *kf, const char *id) {
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "id", g_variant_new_string(id));
    add_string(&d, "number", kf, HEAD);
    add_string(&d, "lastMessage", kf, HEAD);
    g_variant_builder_add(&d, "{sv}", "lastTime", g_variant_new_int64(g_key_file_get_int64(kf, HEAD, "lastTime", NULL)));
    g_variant_builder_add(&d, "{sv}", "unread", g_variant_new_int32(g_key_file_get_integer(kf, HEAD, "unread", NULL)));
    return g_variant_builder_end(&d);
}

/* Appends a message to the number's thread (made by the first one). Returns
   the thread id, or NULL when it could not be stored. */
static char *record(const char *number, const char *text, bool sent, const char *status,
                    gint64 time_ms, GVariant **message) {
    char *id = thread_id_for(number);
    GKeyFile *kf = load_thread(id);
    if (!kf) {
        kf = g_key_file_new();
        g_key_file_set_string(kf, HEAD, "number", number);
        g_key_file_set_integer(kf, HEAD, "unread", 0);
    }
    char mid[40];
    gint64 n = time_ms;
    do snprintf(mid, sizeof(mid), "m_%" G_GINT64_FORMAT, n++); while (g_key_file_has_group(kf, mid));
    g_key_file_set_string(kf, mid, "text", text);
    g_key_file_set_boolean(kf, mid, "sent", sent);
    g_key_file_set_int64(kf, mid, "time", time_ms);
    g_key_file_set_string(kf, mid, "status", status);
    g_key_file_set_int64(kf, HEAD, "lastTime", time_ms);
    g_key_file_set_string(kf, HEAD, "lastMessage", text);
    if (!sent) g_key_file_set_integer(kf, HEAD, "unread", g_key_file_get_integer(kf, HEAD, "unread", NULL) + 1);
    if (save_thread(id, kf)) {
        if (message) *message = message_variant(kf, mid);
    } else {
        g_clear_pointer(&id, g_free);
    }
    g_key_file_free(kf);
    return id;
}

static gint newest_first(gconstpointer a, gconstpointer b) {
    gint64 x = 0, y = 0;
    g_variant_lookup(*(GVariant *const *)a, "lastTime", "x", &x);
    g_variant_lookup(*(GVariant *const *)b, "lastTime", "x", &y);
    return x < y ? 1 : x > y ? -1 : 0;
}

static GVariant *list_threads(void) {
    GPtrArray *all = g_ptr_array_new();
    GDir *dir = g_dir_open(ZYL_MESSAGES_DIR, 0, NULL);
    const char *name;
    while (dir && (name = g_dir_read_name(dir))) {
        GKeyFile *kf = load_thread(name);
        if (!kf) continue;
        g_ptr_array_add(all, thread_variant(kf, name));
        g_key_file_free(kf);
    }
    if (dir) g_dir_close(dir);
    g_ptr_array_sort(all, newest_first);
    GVariant *out = g_variant_new_array(G_VARIANT_TYPE("a{sv}"), (GVariant **)all->pdata, all->len);
    g_ptr_array_free(all, TRUE);
    return out;
}

/* Oldest first, as written */
static GVariant *list_messages(const char *id) {
    GVariantBuilder out;
    g_variant_builder_init(&out, G_VARIANT_TYPE("aa{sv}"));
    GKeyFile *kf = load_thread(id);
    if (kf) {
        char **groups = g_key_file_get_groups(kf, NULL);
        for (char **g = groups; *g; g++)
            if (strcmp(*g, HEAD) != 0) g_variant_builder_add_value(&out, message_variant(kf, *g));
        g_strfreev(groups);
        g_key_file_free(kf);
    }
    return g_variant_builder_end(&out);
}

static bool mark_read(const char *id) {
    GKeyFile *kf = load_thread(id);
    if (!kf) return false;
    g_key_file_set_integer(kf, HEAD, "unread", 0);
    bool ok = save_thread(id, kf);
    g_key_file_free(kf);
    return ok;
}

static bool delete_thread(const char *id) {
    if (!valid_thread_id(id)) return false;
    char *path = g_build_filename(ZYL_MESSAGES_DIR, id, NULL);
    bool ok = g_unlink(path) == 0;
    g_free(path);
    return ok;
}

static bool save_held(GKeyFile *kf) {
    gsize len = 0;
    char *data = g_key_file_to_data(kf, &len, NULL);
    char *dir = g_path_get_dirname(ZYL_PENDING_FILE);
    g_mkdir_with_parents(dir, 0700);
    bool ok = g_file_set_contents_full(ZYL_PENDING_FILE, data, (gssize)len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, NULL);
    g_free(dir);
    g_free(data);
    return ok;
}

/* A message that came in before the first unlock, kept until it can go into its thread */
static bool hold(const char *from, const char *body, gint64 ts) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, ZYL_PENDING_FILE, G_KEY_FILE_NONE, NULL);
    gsize n = 0;
    g_strfreev(g_key_file_get_groups(kf, &n));
    char group[48];
    do snprintf(group, sizeof(group), "m_%" G_GSIZE_FORMAT, n++); while (g_key_file_has_group(kf, group));
    g_key_file_set_string(kf, group, "number", from);
    g_key_file_set_string(kf, group, "text", body);
    g_key_file_set_int64(kf, group, "time", ts);
    bool ok = save_held(kf);
    g_key_file_free(kf);
    return ok;
}

/* Storage open: what was held goes into its threads. One that cannot is kept
   for the next time, the rest are not recorded twice */
static void release_held(void *data) {
    (void)data;
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, ZYL_PENDING_FILE, G_KEY_FILE_NONE, NULL)) {
        gchar **groups = g_key_file_get_groups(kf, NULL);
        for (gchar **g = groups; *g; g++) {
            char *from = g_key_file_get_string(kf, *g, "number", NULL);
            char *text = g_key_file_get_string(kf, *g, "text", NULL);
            char *id = from && text
                ? record(from, text, false, "received", g_key_file_get_int64(kf, *g, "time", NULL), NULL) : NULL;
            if (id || !from || !text) g_key_file_remove_group(kf, *g, NULL);
            g_free(id);
            g_free(from);
            g_free(text);
        }
        g_strfreev(groups);
        gsize left = 0;
        g_strfreev(g_key_file_get_groups(kf, &left));
        if (!left) g_unlink(ZYL_PENDING_FILE);
        else save_held(kf);
    }
    g_key_file_free(kf);
}

#ifndef ZYL_TEST
static GDBusConnection *g_bus;

static const char *messaging_xml =
    "<node>"
    "  <interface name='org.zylos.Messaging'>"
    "    <method name='GetThreads'><arg type='aa{sv}' name='threads' direction='out'/></method>"
    "    <method name='GetMessages'><arg type='s' name='thread_id' direction='in'/>"
    "      <arg type='aa{sv}' name='messages' direction='out'/></method>"
    "    <method name='Send'><arg type='a{sv}' name='params' direction='in'/>"
    "      <arg type='a{sv}' name='message' direction='out'/></method>"
    "    <method name='MarkRead'><arg type='s' name='thread_id' direction='in'/>"
    "      <arg type='b' name='done' direction='out'/></method>"
    "    <method name='DeleteThread'><arg type='s' name='thread_id' direction='in'/>"
    "      <arg type='b' name='deleted' direction='out'/></method>"
    "    <signal name='MessageReceived'><arg type='a{sv}' name='message'/></signal>"
    "  </interface>"
    "</node>";

typedef struct {
    GDBusMethodInvocation *inv;
    char *number;
    char *text;
} Sending;

/* The modem's answer decides what is recorded: a message that did not leave is "failed" */
static void on_sent(GObject *src, GAsyncResult *res, gpointer user) {
    Sending *s = user;
    GError *err = NULL;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (r) g_variant_unref(r);
    if (err) g_warning("[Messaging] SMS to %s not sent: %s", s->number, err->message);
    GVariant *msg = NULL;
    char *id = record(s->number, s->text, true, err ? "failed" : "sent", g_get_real_time() / 1000, &msg);
    if (id) {
        GVariantDict d;
        g_variant_dict_init(&d, msg);
        g_variant_dict_insert(&d, "threadId", "s", id);
        g_variant_unref(msg);
        g_dbus_method_invocation_return_value(s->inv, g_variant_new("(@a{sv})", g_variant_dict_end(&d)));
    } else {
        g_dbus_method_invocation_return_error(s->inv, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "message not stored");
    }
    g_clear_error(&err);
    g_free(id);
    g_free(s->number);
    g_free(s->text);
    g_free(s);
}

static void send_sms(GVariant *params, GDBusMethodInvocation *inv) {
    const gchar *number = NULL, *text = NULL;
    g_variant_lookup(params, "number", "&s", &number);
    g_variant_lookup(params, "text", "&s", &text);
    if (!number || !*number || !text || !*text) {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "number and text required");
        return;
    }
    Sending *s = g_new0(Sending, 1);
    s->inv = inv;
    s->number = g_strdup(number);
    s->text = g_strdup(text);
    g_dbus_connection_call(g_bus, "org.zylos.Telephony", "/org/zylos/Telephony", "org.zylos.Telephony",
                           "SendSms", g_variant_new("(ss)", number, text), NULL,
                           G_DBUS_CALL_FLAGS_NONE, 60000, NULL, on_sent, s);
}

static void on_sms(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *signal, GVariant *params, gpointer data) {
    (void)sender; (void)path; (void)iface; (void)signal; (void)data;
    const gchar *from = NULL, *body = NULL;
    guint64 ts = 0;
    g_variant_get(params, "(&s&st)", &from, &body, &ts);
    char *id = zyl_fscrypt_unlocked(ZYL_MESSAGES_DIR)
        ? record(from, body, false, "received", (gint64)ts, NULL)
        : hold(from, body, (gint64)ts) ? thread_id_for(from) : NULL;
    if (!id) return;
    GVariantBuilder d;
    g_variant_builder_init(&d, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&d, "{sv}", "threadId", g_variant_new_string(id));
    g_variant_builder_add(&d, "{sv}", "number", g_variant_new_string(from));
    g_variant_builder_add(&d, "{sv}", "text", g_variant_new_string(body));
    g_variant_builder_add(&d, "{sv}", "time", g_variant_new_int64((gint64)ts));
    g_dbus_connection_emit_signal(conn, NULL, "/org/zylos/Messaging", "org.zylos.Messaging",
                                  "MessageReceived", g_variant_new("(@a{sv})", g_variant_builder_end(&d)), NULL);
    g_free(id);
}

static void handle(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface; (void)data;
    const gchar *id = NULL;
    if (g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) g_variant_get(params, "(&s)", &id);
    if (g_strcmp0(method, "Send") == 0) {
        GVariant *arg = g_variant_get_child_value(params, 0);
        send_sms(arg, inv);
        g_variant_unref(arg);
    } else if (g_strcmp0(method, "GetThreads") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@aa{sv})", list_threads()));
    } else if (g_strcmp0(method, "GetMessages") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@aa{sv})", list_messages(id)));
    } else if (g_strcmp0(method, "MarkRead") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", mark_read(id)));
    } else if (g_strcmp0(method, "DeleteThread") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", delete_thread(id)));
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name; (void)data;
    g_bus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(messaging_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/Messaging", info->interfaces[0], &vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_dbus_connection_signal_subscribe(conn, "org.zylos.Telephony", "org.zylos.Telephony", "SmsReceived",
                                       "/org/zylos/Telephony", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                       on_sms, NULL, NULL);
    zyl_fscrypt_when_unlocked(ZYL_MESSAGES_DIR, release_held, NULL);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.Messaging", G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_bus, NULL, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
