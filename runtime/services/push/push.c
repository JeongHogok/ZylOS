#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 푸시 서비스 (org.zylos.Push) — 앱 서버가 보낸 메시지가 화면에 없는 앱에도 닿게
 * 수행범위: 앱마다 추측할 수 없는 토픽(up+12자)과 끝점(UnifiedPush, ntfy 프로토콜 — 앱 서버는
 *           끝점에 POST/PUT), 등록과 마지막 메시지 id 의 저장, 서버(/etc/zyl-os/push.json
 *           의 url)로의 연결 하나(curl 스트림, 끊기면 늘어나는 간격으로 다시, 놓친 것은
 *           since 로), 메시지 → 떠 있는 앱에는 PushReceived(주인에게만), 아니면 그 앱이
 *           등록 때 준 알림. 지운 앱의 등록은 지운다(AppRemoved)
 * 의존방향: gio (D-Bus, GSubprocess → /usr/bin/curl, 셸 없이), json-glib, getrandom
 * SOLID: SRP — 받아서 건네기만. 메시지의 뜻(무엇을 가져올지)은 앱의 것
 * ────────────────────────────────────────────────────────── */

#include <gio/gio.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include <signal.h>
#include <string.h>
#include <sys/random.h>

#ifndef PUSH_CONFIG
#define PUSH_CONFIG "/etc/zyl-os/push.json"
#endif
#ifndef PUSH_DATA_DIR
#define PUSH_DATA_DIR "/data/push"
#endif
#define PUSH_STATE PUSH_DATA_DIR "/registrations.json"

#define BACKOFF_MIN_S 5
#define BACKOFF_MAX_S 300

typedef struct {
    char *topic;
    char *title;   /* the app's own words, shown when a message comes while it is not running */
    char *body;
} PushReg;

typedef struct {
    char            *url;      /* the server; "" — push is off on this image */
    GHashTable      *regs;     /* app id → PushReg */
    char            *since;    /* the last message's id: what a reconnect asks after */
    GDBusConnection *bus;
    GSubprocess     *curl;
    guint            retry;
    guint            backoff;
} Push;

static void reg_free(gpointer p) {
    PushReg *r = p;
    g_free(r->topic);
    g_free(r->title);
    g_free(r->body);
    g_free(r);
}

/* ─── Pure parts (tests/test_push.c) ─── */

/* "up" + 12 random letters and digits (~71 bits): nobody can guess an app's topic
   to push to it. Exactly ntfy's UnifiedPush form — only such topics have their
   rate limits counted on the subscriber (this device), not on the app server
   (visitor-subscriber-rate-limiting), so a server sending for many users from
   one address is not throttled */
#define PUSH_TOPIC_LEN 14
static char *push_new_topic(void) {
    static const char ALNUM[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    char t[PUSH_TOPIC_LEN + 1] = "up";
    int n = 2;
    while (n < PUSH_TOPIC_LEN) {
        guint8 b[16];
        if (getrandom(b, sizeof(b), 0) != (ssize_t)sizeof(b)) return NULL;
        for (size_t i = 0; i < sizeof(b) && n < PUSH_TOPIC_LEN; i++)
            if (b[i] < 248) t[n++] = ALNUM[b[i] % 62];    /* 248 = 4·62: every letter as likely */
    }
    t[PUSH_TOPIC_LEN] = '\0';
    return g_strdup(t);
}

/* What the app hands its server: POST/PUT there (?up=1: the UnifiedPush form) */
static char *push_endpoint(const char *url, const char *topic) {
    return g_strdup_printf("%s/%s?up=1", url, topic);
}

static gint by_string(gconstpointer a, gconstpointer b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* One connection for every app's topic: <url>/<t1>,<t2>/json[?since=<id>]; NULL with none */
static char *push_stream_url(const char *url, GHashTable *regs, const char *since) {
    if (!url || !*url || g_hash_table_size(regs) == 0) return NULL;
    GPtrArray *topics = g_ptr_array_new();
    GHashTableIter it;
    gpointer v;
    g_hash_table_iter_init(&it, regs);
    while (g_hash_table_iter_next(&it, NULL, &v)) g_ptr_array_add(topics, ((PushReg *)v)->topic);
    g_ptr_array_sort(topics, by_string);
    g_ptr_array_add(topics, NULL);
    char *joined = g_strjoinv(",", (char **)topics->pdata);
    char *out = since && *since ? g_strdup_printf("%s/%s/json?since=%s", url, joined, since)
                                : g_strdup_printf("%s/%s/json", url, joined);
    g_free(joined);
    g_ptr_array_unref(topics);
    return out;
}

/* A line of the stream: TRUE for a message (keepalive, open … are not); out strings are new */
static gboolean push_parse_line(const char *line, char **id, char **topic, char **message) {
    JsonParser *p = json_parser_new();
    gboolean ok = FALSE;
    if (line && json_parser_load_from_data(p, line, -1, NULL)) {
        JsonNode *root = json_parser_get_root(p);
        JsonObject *o = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
        const char *ev = o ? json_object_get_string_member_with_default(o, "event", "") : "";
        const char *i = o ? json_object_get_string_member_with_default(o, "id", "") : "";
        const char *t = o ? json_object_get_string_member_with_default(o, "topic", "") : "";
        if (strcmp(ev, "message") == 0 && *i && *t) {
            *id = g_strdup(i);
            *topic = g_strdup(t);
            *message = g_strdup(o ? json_object_get_string_member_with_default(o, "message", "") : "");
            ok = TRUE;
        }
    }
    g_object_unref(p);
    return ok;
}

/* The app a topic belongs to (borrowed), or NULL */
static const char *push_owner(GHashTable *regs, const char *topic) {
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, regs);
    while (g_hash_table_iter_next(&it, &k, &v))
        if (g_strcmp0(((PushReg *)v)->topic, topic) == 0) return k;
    return NULL;
}

static char *config_url(const char *path) {
    JsonParser *p = json_parser_new();
    char *url = g_strdup("");
    if (json_parser_load_from_file(p, path, NULL)) {
        JsonNode *root = json_parser_get_root(p);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            const char *u = json_object_get_string_member_with_default(json_node_get_object(root), "url", "");
            if (g_str_has_prefix(u, "https://")) {
                g_free(url);
                url = g_strdup(u);
                size_t n = strlen(url);
                while (n && url[n - 1] == '/') url[--n] = '\0';
            }
        }
    }
    g_object_unref(p);
    return url;
}

static void state_save(Push *ps) {
    g_mkdir_with_parents(PUSH_DATA_DIR, 0700);
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "since");
    json_builder_add_string_value(b, ps->since ? ps->since : "");
    json_builder_set_member_name(b, "apps");
    json_builder_begin_object(b);
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, ps->regs);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        PushReg *r = v;
        json_builder_set_member_name(b, k);
        json_builder_begin_object(b);
        json_builder_set_member_name(b, "topic");
        json_builder_add_string_value(b, r->topic);
        json_builder_set_member_name(b, "title");
        json_builder_add_string_value(b, r->title);
        json_builder_set_member_name(b, "body");
        json_builder_add_string_value(b, r->body);
        json_builder_end_object(b);
    }
    json_builder_end_object(b);
    json_builder_end_object(b);
    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    gchar *text = json_generator_to_data(gen, NULL);
    if (!g_file_set_contents_full(PUSH_STATE, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL))
        g_warning("push: cannot save %s", PUSH_STATE);
    g_free(text);
    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(b);
}

static void state_load(Push *ps) {
    JsonParser *p = json_parser_new();
    if (json_parser_load_from_file(p, PUSH_STATE, NULL)) {
        JsonNode *root = json_parser_get_root(p);
        JsonObject *o = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
        if (o) {
            g_free(ps->since);
            ps->since = g_strdup(json_object_get_string_member_with_default(o, "since", ""));
            JsonNode *apps = json_object_get_member(o, "apps");
            JsonObject *a = apps && JSON_NODE_HOLDS_OBJECT(apps) ? json_node_get_object(apps) : NULL;
            GList *ids = a ? json_object_get_members(a) : NULL;
            for (GList *l = ids; l; l = l->next) {
                JsonNode *n = json_object_get_member(a, l->data);
                if (!JSON_NODE_HOLDS_OBJECT(n)) continue;
                JsonObject *r = json_node_get_object(n);
                const char *topic = json_object_get_string_member_with_default(r, "topic", "");
                if (!*topic) continue;
                PushReg *reg = g_new0(PushReg, 1);
                reg->topic = g_strdup(topic);
                reg->title = g_strdup(json_object_get_string_member_with_default(r, "title", ""));
                reg->body = g_strdup(json_object_get_string_member_with_default(r, "body", ""));
                g_hash_table_replace(ps->regs, g_strdup(l->data), reg);
            }
            g_list_free(ids);
        }
    }
    g_object_unref(p);
}

/* An app's endpoint, its topic made on first ask; NULL when push is off */
static char *push_register(Push *ps, const char *app_id, const char *title, const char *body) {
    if (!ps->url || !*ps->url || !app_id || !*app_id) return NULL;
    PushReg *r = g_hash_table_lookup(ps->regs, app_id);
    if (!r) {
        char *topic = push_new_topic();
        if (!topic) return NULL;
        r = g_new0(PushReg, 1);
        r->topic = topic;
        g_hash_table_replace(ps->regs, g_strdup(app_id), r);
    }
    g_free(r->title);
    g_free(r->body);
    r->title = g_strdup(title ? title : "");
    r->body = g_strdup(body ? body : "");
    return push_endpoint(ps->url, r->topic);
}

#ifndef ZYL_TEST
static void connect_stream(Push *ps);

static gboolean reconnect_now(gpointer data) {
    Push *ps = data;
    ps->retry = 0;
    connect_stream(ps);
    return G_SOURCE_REMOVE;
}

static void schedule_reconnect(Push *ps) {
    if (ps->retry) return;
    ps->backoff = ps->backoff ? MIN(ps->backoff * 2, BACKOFF_MAX_S) : BACKOFF_MIN_S;
    ps->retry = g_timeout_add_seconds(ps->backoff, reconnect_now, ps);
}

/* What the topics are changed: the connection follows */
static void restart_stream(Push *ps) {
    if (ps->curl) g_subprocess_force_exit(ps->curl);   /* its exit reconnects */
    else connect_stream(ps);
}

typedef struct {
    Push  *ps;
    char  *topic;
    char  *app_id;
    char  *message;
} Arrival;

static void arrival_free(Arrival *a) {
    g_free(a->topic);
    g_free(a->app_id);
    g_free(a->message);
    g_free(a);
}

/* A running app gets the message itself; otherwise the user sees what it registered */
static void on_running(GObject *src, GAsyncResult *res, gpointer data) {
    Arrival *a = data;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, NULL);
    gboolean running = FALSE;
    if (r) {
        GVariantIter *ids = NULL;
        const gchar *id;
        g_variant_get(r, "(asi)", &ids, NULL);
        while (g_variant_iter_next(ids, "&s", &id)) if (strcmp(id, a->app_id) == 0) running = TRUE;
        g_variant_iter_free(ids);
        g_variant_unref(r);
    }
    PushReg *reg = g_hash_table_lookup(a->ps->regs, a->app_id);
    if (running) {
        g_dbus_connection_emit_signal(a->ps->bus, NULL, "/org/zylos/Push", "org.zylos.Push", "PushReceived",
            g_variant_new("(sss)", a->topic, a->app_id, a->message), NULL);
    } else if (reg && *reg->title) {
        g_dbus_connection_call(a->ps->bus, "org.zylos.Notification", "/org/zylos/Notification",
            "org.zylos.Notification", "Post",
            g_variant_new("(sssssi)", a->app_id, "push", reg->title, reg->body, "", 1),
            NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
    arrival_free(a);
}

static void on_line(GObject *src, GAsyncResult *res, gpointer data) {
    Push *ps = data;
    GDataInputStream *in = G_DATA_INPUT_STREAM(src);
    gsize len = 0;
    char *line = g_data_input_stream_read_line_finish(in, res, &len, NULL);
    if (!line) {                                            /* the stream ended: the exit handler reconnects */
        g_object_unref(in);
        return;
    }
    ps->backoff = 0;                                        /* the server answered */
    char *id = NULL, *topic = NULL, *message = NULL;
    if (push_parse_line(line, &id, &topic, &message)) {
        g_free(ps->since);
        ps->since = id;
        state_save(ps);
        const char *owner = push_owner(ps->regs, topic);
        if (owner && ps->bus) {
            Arrival *a = g_new0(Arrival, 1);
            a->ps = ps;
            a->topic = topic;
            a->app_id = g_strdup(owner);
            a->message = message;
            g_dbus_connection_call(ps->bus, "org.zylos.WebAppManager", "/org/zylos/WebAppManager",
                "org.zylos.WebAppManager", "ListRunning", NULL, G_VARIANT_TYPE("(asi)"),
                G_DBUS_CALL_FLAGS_NONE, 5000, NULL, on_running, a);
            topic = message = NULL;
        }
    }
    g_free(topic);
    g_free(message);
    g_free(line);
    g_data_input_stream_read_line_async(in, G_PRIORITY_DEFAULT, NULL, on_line, ps);
}

static void on_curl_exit(GObject *src, GAsyncResult *res, gpointer data) {
    Push *ps = data;
    g_subprocess_wait_finish(G_SUBPROCESS(src), res, NULL);
    if (ps->curl == G_SUBPROCESS(src)) g_clear_object(&ps->curl);
    g_object_unref(src);
    if (g_hash_table_size(ps->regs)) schedule_reconnect(ps);
}

static void connect_stream(Push *ps) {
    char *url = push_stream_url(ps->url, ps->regs, ps->since);
    if (!url || ps->curl) {
        g_free(url);
        return;
    }
    GError *err = NULL;
    GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &err,
        "/usr/bin/curl", "-s", "-N", "-f", "--proto", "=https", "--connect-timeout", "15",
        "--keepalive-time", "60", url, NULL);
    g_free(url);
    if (!p) {
        g_warning("push: cannot start curl: %s", err->message);
        g_error_free(err);
        schedule_reconnect(ps);
        return;
    }
    ps->curl = p;
    GDataInputStream *in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(p));
    g_data_input_stream_read_line_async(in, G_PRIORITY_DEFAULT, NULL, on_line, ps);
    g_subprocess_wait_async(g_object_ref(p), NULL, on_curl_exit, ps);
}

/* ─── D-Bus ─── */
static const char *push_xml =
    "<node>"
    "  <interface name='org.zylos.Push'>"
    "    <method name='Register'>"
    "      <arg type='s' name='caller' direction='in'/>"
    "      <arg type='s' name='title' direction='in'/>"
    "      <arg type='s' name='body' direction='in'/>"
    "      <arg type='s' name='endpoint' direction='out'/>"
    "    </method>"
    "    <method name='Unregister'>"
    "      <arg type='s' name='caller' direction='in'/>"
    "      <arg type='b' name='removed' direction='out'/>"
    "    </method>"
    "    <signal name='PushReceived'>"
    "      <arg type='s' name='topic'/>"
    "      <arg type='s' name='app_id'/>"
    "      <arg type='s' name='message'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static void handle(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
                   const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)c; (void)sender; (void)path; (void)iface;
    Push *ps = data;
    if (g_strcmp0(method, "Register") == 0) {
        const gchar *caller, *title, *body;
        g_variant_get(params, "(&s&s&s)", &caller, &title, &body);
        gboolean known = g_hash_table_contains(ps->regs, caller);
        char *endpoint = push_register(ps, caller, title, body);
        if (!endpoint) {
            g_dbus_method_invocation_return_error(inv, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "push unavailable");
            return;
        }
        state_save(ps);
        if (!known) restart_stream(ps);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(s)", endpoint));
        g_free(endpoint);
    } else if (g_strcmp0(method, "Unregister") == 0) {
        const gchar *caller;
        g_variant_get(params, "(&s)", &caller);
        gboolean removed = g_hash_table_remove(ps->regs, caller);
        if (removed) {
            state_save(ps);
            restart_stream(ps);
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", removed));
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = handle };

static void on_app_removed(GDBusConnection *conn, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)signal;
    Push *ps = data;
    const gchar *app_id = NULL;
    g_variant_get(params, "(&s)", &app_id);
    if (g_hash_table_remove(ps->regs, app_id)) {
        state_save(ps);
        restart_stream(ps);
    }
}

static void on_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    (void)name;
    Push *ps = data;
    ps->bus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(push_xml, NULL);
    g_dbus_connection_register_object(conn, "/org/zylos/Push", info->interfaces[0], &vtable, ps, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_dbus_connection_signal_subscribe(conn, "org.zylos.AppStore", "org.zylos.AppStore", "AppRemoved",
        "/org/zylos/AppStore", NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_app_removed, ps, NULL);
    connect_stream(ps);
}

static gboolean on_term(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void) {
    Push ps = { 0 };
    ps.url = config_url(PUSH_CONFIG);
    ps.regs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, reg_free);
    state_load(&ps);
    if (!*ps.url) g_message("push: no server in %s — push is off on this image", PUSH_CONFIG);
    g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.zylos.Push", G_BUS_NAME_OWNER_FLAGS_NONE, on_bus, NULL, NULL, &ps, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_term, loop);
    g_main_loop_run(loop);
    if (ps.curl) g_subprocess_force_exit(ps.curl);
    g_main_loop_unref(loop);
    return 0;
}
#endif /* ZYL_TEST */
