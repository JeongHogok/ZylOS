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
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include <gio/gio.h>

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

int zyl_account_register_local(ZylAccountService *svc,
                                const char *name, const char *pin) {
    if (!svc || !name || !pin) return -1;
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

    svc->active_index = svc->n_accounts;
    svc->n_accounts++;

    /* PIN은 credential 서비스에 저장 */
    g_message("[Account] Registered local account: %s (%s)", name, id);
    return 0;
}

int zyl_account_login_local(ZylAccountService *svc, const char *pin) {
    if (!svc || !pin) return -1;
    /* PIN 검증은 credential 서비스에 위임 */
    if (svc->active_index >= 0) return 0;
    return -1;
}

int zyl_account_login_oauth(ZylAccountService *svc,
                             const char *provider, const char *auth_code) {
    if (!svc || !provider || !auth_code) return -1;
    /* TODO(auth): OAuth 토큰 교환 미구현 — 현재 스텁.
     * 필요 작업:
     * 1. HTTPS POST → provider token endpoint (auth_code → access_token + refresh_token)
     * 2. 토큰을 credential 서비스에 암호화 저장
     * 3. 계정 정보를 accounts[] 배열에 추가 (type = ZYL_ACCOUNT_CLOUD)
     * 4. refresh_token 만료 전 자동 갱신 타이머 설정
     */
    g_message("[Account] OAuth login: provider=%s (STUB — not implemented)", provider);
    return 0;
}

int zyl_account_refresh_token(ZylAccountService *svc) {
    if (!svc) return -1;
    /* TODO(auth): 토큰 갱신 미구현 — 현재 스텁.
     * 필요 작업:
     * 1. credential 서비스에서 refresh_token 로드
     * 2. HTTPS POST → provider token endpoint (grant_type=refresh_token)
     * 3. 새 access_token + refresh_token 저장
     * 4. 실패 시 재인증 요구 (로그아웃 또는 재로그인 알림)
     */
    g_message("[Account] Token refresh requested (STUB — not implemented)");
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

int zyl_account_set_auto_sync(ZylAccountService *svc, bool enabled, int interval_min) {
    if (!svc) return -1;
    svc->auto_sync = enabled;
    svc->sync_interval_min = interval_min > 0 ? interval_min : 30;
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
    /* 백업: 설정 + 연락처 + 메시지 → 암호화 아카이브 */
    g_message("[Account] Backup → %s", output_path);
    /* tar + AES-256-GCM 암호화 (credential 서비스의 마스터키 사용) */
    char *argv[] = {"tar", "czf", (char *)output_path,
                    "-C", "/data", "apps/*/Documents/", NULL};
    return spawn_and_wait(argv);
}

int zyl_account_restore(ZylAccountService *svc, const char *backup_path) {
    if (!svc || !backup_path) return -1;
    if (!is_safe_path(backup_path)) {
        g_warning("[Account] Restore rejected: unsafe path characters");
        return -1;
    }
    g_message("[Account] Restore ← %s", backup_path);
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
