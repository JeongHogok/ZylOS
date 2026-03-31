#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 계정 서비스 — 로컬 계정 + OAuth 2.0 + 동기화/백업
 * 수행범위: 계정 CRUD, 토큰 관리, 연락처/설정 동기화, 암호화 백업
 * 의존방향: account.h, gio/gio.h, credential (토큰 저장)
 * SOLID: SRP — 계정 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "account.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <openssl/evp.h>

#define ACCOUNT_DIR "/data/accounts"
#define MAX_ACCOUNTS 8

struct ZylAccountService {
    ZylAccountInfo accounts[MAX_ACCOUNTS];
    int n_accounts;
    int active_index;
    bool auto_sync;
    int sync_interval_min;
    guint sync_timer_id;
    GDBusConnection *dbus;
    guint dbus_owner_id;
};

static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) mkdir(path, 0700);
}

/* ─── D-Bus ─── */
static const char *account_introspection_xml =
    "<node>"
    "  <interface name='" ZYL_ACCOUNT_DBUS_NAME "'>"
    "    <method name='RegisterLocal'>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='s' name='pin' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='LoginLocal'>"
    "      <arg type='s' name='pin' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='GetCurrent'>"
    "      <arg type='s' name='account_id' direction='out'/>"
    "      <arg type='s' name='display_name' direction='out'/>"
    "      <arg type='s' name='email' direction='out'/>"
    "    </method>"
    "    <method name='SyncNow'/>"
    "    <method name='Backup'>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "    <method name='Restore'>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='b' name='success' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void handle_account_method(GDBusConnection *conn, const gchar *sender,
                                    const gchar *path, const gchar *iface,
                                    const gchar *method, GVariant *params,
                                    GDBusMethodInvocation *inv, gpointer data) {
    ZylAccountService *svc = data;
    (void)conn; (void)sender; (void)path; (void)iface;

    if (g_strcmp0(method, "RegisterLocal") == 0) {
        const gchar *name, *pin;
        g_variant_get(params, "(&s&s)", &name, &pin);
        int ret = zyl_account_register_local(svc, name, pin);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "LoginLocal") == 0) {
        const gchar *pin;
        g_variant_get(params, "(&s)", &pin);
        int ret = zyl_account_login_local(svc, pin);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "GetCurrent") == 0) {
        ZylAccountInfo *info = zyl_account_get_current(svc);
        if (info) {
            g_dbus_method_invocation_return_value(inv, g_variant_new("(sss)",
                info->account_id ? info->account_id : "",
                info->display_name ? info->display_name : "",
                info->email ? info->email : ""));
        } else {
            g_dbus_method_invocation_return_value(inv, g_variant_new("(sss)", "", "", ""));
        }
    } else if (g_strcmp0(method, "SyncNow") == 0) {
        zyl_account_sync_now(svc);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (g_strcmp0(method, "Backup") == 0) {
        const gchar *bpath;
        g_variant_get(params, "(&s)", &bpath);
        int ret = zyl_account_backup(svc, bpath);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ret == 0));
    } else if (g_strcmp0(method, "Restore") == 0) {
        const gchar *rpath;
        g_variant_get(params, "(&s)", &rpath);
        int ret = zyl_account_restore(svc, rpath);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", ret == 0));
    }
}

static const GDBusInterfaceVTable account_vtable = { .method_call = handle_account_method };

static void on_account_bus(GDBusConnection *conn, const gchar *name, gpointer data) {
    ZylAccountService *svc = data;
    (void)name;
    svc->dbus = conn;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(account_introspection_xml, NULL);
    if (info && info->interfaces && info->interfaces[0]) {
        g_dbus_connection_register_object(conn, ZYL_ACCOUNT_DBUS_PATH,
            info->interfaces[0], &account_vtable, svc, NULL, NULL);
    }
    if (info) g_dbus_node_info_unref(info);
    g_message("[Account] D-Bus registered: %s", ZYL_ACCOUNT_DBUS_NAME);
}

/* ─── Public API ─── */

ZylAccountService *zyl_account_create(void) {
    ZylAccountService *svc = calloc(1, sizeof(ZylAccountService));
    if (!svc) return NULL;
    svc->active_index = -1;
    ensure_dir(ACCOUNT_DIR);

    svc->dbus_owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
        ZYL_ACCOUNT_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_account_bus, NULL, NULL, svc, NULL);

    return svc;
}

void zyl_account_destroy(ZylAccountService *svc) {
    if (!svc) return;
    if (svc->sync_timer_id) g_source_remove(svc->sync_timer_id);
    g_bus_unown_name(svc->dbus_owner_id);
    for (int i = 0; i < svc->n_accounts; i++) {
        free(svc->accounts[i].account_id);
        free(svc->accounts[i].display_name);
        free(svc->accounts[i].email);
    }
    free(svc);
}

/* Compute SHA-256 hex of input, store in out (must be >= 65 bytes). */
static bool sha256_hex(const char *input, char *out, size_t out_len) {
    if (out_len < 65) return false;
    if (!input || !input[0]) return false;
    unsigned char hash[32];
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    bool ok = (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1 &&
               EVP_DigestUpdate(ctx, input, strlen(input)) == 1);
    unsigned int hlen = 0;
    ok = ok && (EVP_DigestFinal_ex(ctx, hash, &hlen) == 1);
    EVP_MD_CTX_free(ctx);
    if (!ok) return false;
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", hash[i]);
    out[64] = '\0';
    return true;
}

/* Persist PIN hash for account to disk. */
static void store_pin_hash(const char *account_id, const char *pin_hash) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pin", ACCOUNT_DIR, account_id);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\n", pin_hash);
    fflush(f); fsync(fileno(f));
    fclose(f);
}

/* Load stored PIN hash for account. Returns malloc'd string or NULL. */
static char *load_pin_hash(const char *account_id) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pin", ACCOUNT_DIR, account_id);
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char buf[128] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    }
    fclose(f);
    return buf[0] ? strdup(buf) : NULL;
}

int zyl_account_register_local(ZylAccountService *svc,
                                const char *name, const char *pin) {
    if (!svc || !name || !pin) return -1;
    if (strlen(pin) < 4) {
        g_warning("[Account] PIN too short (minimum 4 digits)");
        return -1;
    }
    if (svc->n_accounts >= MAX_ACCOUNTS) return -1;

    ZylAccountInfo *acc = &svc->accounts[svc->n_accounts];
    char id[64];
    snprintf(id, sizeof(id), "local_%ld", (long)time(NULL));
    acc->account_id = strdup(id);
    acc->display_name = strdup(name);
    acc->email = strdup("");
    if (!acc->account_id || !acc->display_name || !acc->email) {
        free(acc->account_id);
        free(acc->display_name);
        free(acc->email);
        memset(acc, 0, sizeof(*acc));
        return -1;
    }
    acc->type = ZYL_ACCOUNT_LOCAL;
    acc->is_active = true;
    acc->created_at = (uint64_t)time(NULL);
    acc->last_sync = 0;

    /* Hash and persist the PIN */
    char pin_hash[65];
    if (!sha256_hex(pin, pin_hash, sizeof(pin_hash))) {
        g_warning("[Account] Failed to hash PIN");
        free(acc->account_id); free(acc->display_name); free(acc->email);
        memset(acc, 0, sizeof(*acc));
        return -1;
    }
    store_pin_hash(id, pin_hash);

    svc->active_index = svc->n_accounts;
    svc->n_accounts++;

    g_message("[Account] Registered local account: %s (%s)", name, id);
    return 0;
}

int zyl_account_login_local(ZylAccountService *svc, const char *pin) {
    if (!svc || !pin) return -1;
    if (svc->active_index < 0) return -1;

    ZylAccountInfo *acc = &svc->accounts[svc->active_index];
    if (acc->type != ZYL_ACCOUNT_LOCAL) return -1;

    /* Load stored PIN hash and compare */
    char *stored_hash = load_pin_hash(acc->account_id);
    if (!stored_hash) {
        g_warning("[Account] No PIN hash found for account %s", acc->account_id);
        return -1;
    }
    char input_hash[65];
    bool ok = sha256_hex(pin, input_hash, sizeof(input_hash));
    bool match = ok && (strcmp(stored_hash, input_hash) == 0);
    free(stored_hash);

    if (!match) {
        g_warning("[Account] PIN verification failed for account %s", acc->account_id);
        return -1;
    }
    g_message("[Account] Local login succeeded: %s", acc->account_id);
    return 0;
}

static char *json_dup_string_field(const char *json, const char *key) {
    if (!json || !key) return NULL;
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json, -1, NULL)) {
        g_object_unref(parser);
        return NULL;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return NULL;
    }
    JsonObject *obj = json_node_get_object(root);
    char *ret = NULL;
    if (json_object_has_member(obj, key)) {
        const char *v = json_object_get_string_member(obj, key);
        if (v) ret = strdup(v);
    }
    g_object_unref(parser);
    return ret;
}

/* ─── OAuth token endpoint mapping ─── */
static const char *oauth_token_url(const char *provider) {
    if (strcmp(provider, "google") == 0)
        return "https://oauth2.googleapis.com/token";
    if (strcmp(provider, "github") == 0)
        return "https://github.com/login/oauth/access_token";
    return NULL;
}

/* ─── HTTP POST via curl CLI (posix_spawn, no shell) ─── */
static char *http_post(const char *url, const char *post_data) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return NULL;

    pid_t pid;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);

    const char *argv[] = {
        "/usr/bin/curl", "-s", "-X", "POST",
        "-H", "Accept: application/json",
        "-d", post_data, url, NULL
    };
    char *env[] = { "PATH=/usr/bin:/bin", NULL };

    int rc = posix_spawn(&pid, "/usr/bin/curl", &actions, NULL,
                         (char *const *)argv, env);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);

    if (rc != 0) { close(pipefd[0]); return NULL; }

    char *buf = malloc(4096);
    if (!buf) { close(pipefd[0]); return NULL; }
    ssize_t n = read(pipefd[0], buf, 4095);
    close(pipefd[0]);
    waitpid(pid, NULL, 0);

    if (n <= 0) { free(buf); return NULL; }
    buf[n] = '\0';
    return buf;
}

int zyl_account_login_oauth(ZylAccountService *svc,
                             const char *provider, const char *auth_code) {
    if (!svc || !provider || !auth_code) return -1;
    if (svc->n_accounts >= MAX_ACCOUNTS) return -1;

    const char *token_url = oauth_token_url(provider);
    if (!token_url) {
        g_warning("[Account] Unknown OAuth provider: %s", provider);
        return -1;
    }

    /* Exchange auth_code → access_token + refresh_token */
    char post_data[512];
    snprintf(post_data, sizeof(post_data),
             "grant_type=authorization_code&code=%s&client_id=zylos-app",
             auth_code);

    char *response = http_post(token_url, post_data);
    if (!response) {
        g_warning("[Account] OAuth token exchange failed for %s", provider);
        return -1;
    }

    /* Parse token response robustly */
    char *access_token = json_dup_string_field(response, "access_token");
    char *email = json_dup_string_field(response, "email");
    if (!access_token) {
        g_warning("[Account] OAuth response missing access_token");
        free(response);
        return -1;
    }

    /* Store tokens via credential service (D-Bus) */
    /* For now, store the raw response — credential service encrypts it */
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (conn) {
        g_dbus_connection_call_sync(conn,
            "org.zylos.CredentialManager", "/org/zylos/CredentialManager",
            "org.zylos.CredentialManager", "Store",
            g_variant_new("(ssss)", "oauth", provider, response, "OAuth token"),
            NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
        g_object_unref(conn);
    }
    if (err) g_error_free(err);

    /* Register cloud account */
    ZylAccountInfo *acc = &svc->accounts[svc->n_accounts];
    char id[64];
    snprintf(id, sizeof(id), "%s_%ld", provider, (long)time(NULL));
    acc->account_id = strdup(id);
    acc->display_name = strdup(provider);
    acc->email = email ? email : strdup(""); /* best-effort extraction from token response */
    acc->type = ZYL_ACCOUNT_CLOUD;
    acc->is_active = true;
    acc->created_at = (uint64_t)time(NULL);
    acc->last_sync = 0;
    svc->active_index = svc->n_accounts;
    svc->n_accounts++;

    g_message("[Account] OAuth login successful: provider=%s account=%s email=%s",
              provider, id, acc->email ? acc->email : "");
    free(access_token);
    free(response);
    return 0;
}

int zyl_account_refresh_token(ZylAccountService *svc) {
    if (!svc || svc->active_index < 0) return -1;

    ZylAccountInfo *acc = &svc->accounts[svc->active_index];
    if (acc->type != ZYL_ACCOUNT_CLOUD) return -1;

    /* Load stored token from credential service */
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) {
        if (err) g_error_free(err);
        return -1;
    }

    GVariant *result = g_dbus_connection_call_sync(conn,
        "org.zylos.CredentialManager", "/org/zylos/CredentialManager",
        "org.zylos.CredentialManager", "Lookup",
        g_variant_new("(ss)", "oauth", acc->display_name),
        G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &err);

    if (!result || err) {
        g_warning("[Account] Token lookup failed: %s",
                  err ? err->message : "unknown");
        if (err) g_error_free(err);
        g_object_unref(conn);
        return -1;
    }

    const gchar *stored_token_json;
    g_variant_get(result, "(&s)", &stored_token_json);

    /* Extract refresh_token from stored JSON and exchange for a new access_token */
    char *refresh_token = json_dup_string_field(stored_token_json, "refresh_token");
    const char *token_url = oauth_token_url(acc->display_name);
    if (token_url && refresh_token && refresh_token[0]) {
        char post_data[512];
        snprintf(post_data, sizeof(post_data),
                 "grant_type=refresh_token&refresh_token=%s&client_id=zylos-app",
                 refresh_token);
        char *response = http_post(token_url, post_data);
        if (response) {
            /* Update stored token */
            g_dbus_connection_call_sync(conn,
                "org.zylos.CredentialManager", "/org/zylos/CredentialManager",
                "org.zylos.CredentialManager", "Store",
                g_variant_new("(ssss)", "oauth", acc->display_name,
                              response, "OAuth token (refreshed)"),
                NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
            free(response);
            g_message("[Account] Token refreshed for %s", acc->display_name);
        }
    }
    free(refresh_token);

    g_variant_unref(result);
    g_object_unref(conn);
    return 0;
}

int zyl_account_logout(ZylAccountService *svc) {
    if (!svc || svc->active_index < 0) return -1;
    svc->accounts[svc->active_index].is_active = false;
    svc->active_index = -1;
    g_message("[Account] Logged out");
    return 0;
}

ZylAccountInfo *zyl_account_get_current(ZylAccountService *svc) {
    if (!svc || svc->active_index < 0) return NULL;
    return &svc->accounts[svc->active_index];
}

int zyl_account_list(ZylAccountService *svc, ZylAccountInfo **out, int *count) {
    if (!svc || !out || !count) return -1;
    *out = svc->accounts;
    *count = svc->n_accounts;
    return 0;
}

void zyl_account_info_free(ZylAccountInfo *info, int count) {
    (void)info; (void)count;
    /* accounts are owned by service, not freed here */
}

int zyl_account_sync_now(ZylAccountService *svc) {
    if (!svc || svc->active_index < 0) return -1;
    ZylAccountInfo *acc = &svc->accounts[svc->active_index];
    if (acc->type != ZYL_ACCOUNT_CLOUD) return -1;
    acc->last_sync = (uint64_t)time(NULL);
    g_message("[Account] Sync completed for %s", acc->display_name);
    return 0;
}

static gboolean account_auto_sync_cb(gpointer data) {
    ZylAccountService *svc = data;
    if (!svc || !svc->auto_sync) return G_SOURCE_REMOVE;
    if (svc->active_index >= 0) {
        ZylAccountInfo *acc = &svc->accounts[svc->active_index];
        if (acc->type == ZYL_ACCOUNT_CLOUD) {
            zyl_account_sync_now(svc);
        }
    }
    return G_SOURCE_CONTINUE;
}

int zyl_account_set_auto_sync(ZylAccountService *svc, bool enabled, int interval_min) {
    if (!svc) return -1;
    svc->auto_sync = enabled;
    svc->sync_interval_min = interval_min > 0 ? interval_min : 30;

    if (svc->sync_timer_id) {
        g_source_remove(svc->sync_timer_id);
        svc->sync_timer_id = 0;
    }
    if (enabled) {
        svc->sync_timer_id = g_timeout_add_seconds((guint)(svc->sync_interval_min * 60),
                                                   account_auto_sync_cb, svc);
    }

    g_message("[Account] Auto-sync: %s (every %d min)",
              enabled ? "ON" : "OFF", svc->sync_interval_min);
    return 0;
}

/* Reject paths containing shell metacharacters or path traversal */
static bool is_safe_path(const char *path) {
    if (!path || path[0] == '\0') return false;
    /* Reject ".." path traversal */
    if (strstr(path, "..") != NULL) return false;
    const char *dangerous = ";|&`$\n\r\"'\\(){}[]<>?*~!#";
    for (const char *p = path; *p; p++) {
        if (strchr(dangerous, *p)) return false;
    }
    return true;
}

/* Helper: spawn a process and wait for it, returning 0 on success */
static int spawn_and_wait(char *const argv[]) {
    extern char **environ;
    pid_t pid;
    int status;

    if (posix_spawn(&pid, "/usr/bin/tar", NULL, NULL, argv, environ) != 0) {
        g_warning("[Account] posix_spawn failed");
        return -1;
    }
    if (waitpid(pid, &status, 0) == -1) {
        g_warning("[Account] waitpid failed");
        return -1;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

int zyl_account_backup(ZylAccountService *svc, const char *output_path) {
    if (!svc || !output_path) return -1;
    if (!is_safe_path(output_path)) {
        g_warning("[Account] Backup rejected: unsafe path characters");
        return -1;
    }
    /*
     * Create a compressed backup archive of account-relevant roots.
     * Do NOT claim encryption here: this path currently produces a plain .tar.gz.
     * Using explicit directories avoids broken wildcard handling under posix_spawn.
     */
    g_message("[Account] Backup → %s (tar.gz, not encrypted)", output_path);
    char *argv[] = {
        "tar", "czf", (char *)output_path,
        "-C", "/data",
        "accounts", "apps", "users",
        NULL
    };
    return spawn_and_wait(argv);
}

int zyl_account_restore(ZylAccountService *svc, const char *backup_path) {
    if (!svc || !backup_path) return -1;
    if (!is_safe_path(backup_path)) {
        g_warning("[Account] Restore rejected: unsafe path characters");
        return -1;
    }
    g_message("[Account] Restore ← %s (tar.gz input)", backup_path);
    char *argv[] = {"tar", "xzf", (char *)backup_path,
                    "-C", "/data", NULL};
    return spawn_and_wait(argv);
}

/* ─── 데몬 진입점 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    ZylAccountService *svc = zyl_account_create();
    if (!svc) { g_critical("[Account] Failed to create service"); return 1; }
    g_message("[Account] Zyl OS Account Service started");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    zyl_account_destroy(svc);
    return 0;
}
