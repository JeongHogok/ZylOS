#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: VPN 서비스 — WireGuard 통합 (nmcli 경유)
 * 수행범위: WireGuard 프로필 임포트/제거, 연결/해제
 * 의존방향: vpn.h, stdio, spawn
 * SOLID: SRP — VPN 연결 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "vpn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include <dirent.h>
#include <errno.h>

#define VPN_CONFIG_DIR "/data/vpn"

static ZylVpnState g_state = ZYL_VPN_DISCONNECTED;
static char g_active_profile[128] = {0};

/* ─── 안전한 명령 실행 ─── */
static int safe_exec(const char *const argv[]) {
    pid_t pid;
    char *safe_env[] = { "PATH=/usr/bin:/bin:/sbin:/usr/sbin", NULL };
    int rc = posix_spawn(&pid, argv[0], NULL, NULL,
                         (char *const *)argv, safe_env);
    if (rc != 0) return -1;
    int status;
    if (waitpid(pid, &status, 0) == -1) return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* ─── 공개 API ─── */

int zyl_vpn_import_profile(const char *config_path, const char *name) {
    if (!config_path || !name) return -1;

    /* Validate config_path — reject path traversal */
    if (strstr(config_path, "..") != NULL) {
        fprintf(stderr, "[VPN] Rejected unsafe config path: %s\n", config_path);
        return -1;
    }

    mkdir(VPN_CONFIG_DIR, 0700);

    /* nmcli로 WireGuard 프로필 임포트 */
    const char *argv[] = {
        "/usr/bin/nmcli", "connection", "import",
        "type", "wireguard", "file", config_path, NULL
    };
    int ret = safe_exec(argv);
    if (ret == 0) {
        fprintf(stderr, "[VPN] Imported profile: %s from %s\n",
                name, config_path);
    }
    return ret;
}

int zyl_vpn_connect(const char *profile_name) {
    if (!profile_name) return -1;

    g_state = ZYL_VPN_CONNECTING;
    const char *argv[] = {
        "/usr/bin/nmcli", "connection", "up", profile_name, NULL
    };
    int ret = safe_exec(argv);

    if (ret == 0) {
        g_state = ZYL_VPN_CONNECTED;
        snprintf(g_active_profile, sizeof(g_active_profile), "%s",
                 profile_name);
        fprintf(stderr, "[VPN] Connected: %s\n", profile_name);
    } else {
        g_state = ZYL_VPN_ERROR;
        fprintf(stderr, "[VPN] Connection failed: %s\n", profile_name);
    }
    return ret;
}

int zyl_vpn_disconnect(void) {
    if (g_active_profile[0] == '\0') return -1;

    const char *argv[] = {
        "/usr/bin/nmcli", "connection", "down", g_active_profile, NULL
    };
    int ret = safe_exec(argv);

    g_state = ZYL_VPN_DISCONNECTED;
    g_active_profile[0] = '\0';
    fprintf(stderr, "[VPN] Disconnected\n");
    return ret;
}

ZylVpnState zyl_vpn_get_state(void) {
    return g_state;
}

int zyl_vpn_remove_profile(const char *name) {
    if (!name) return -1;
    const char *argv[] = {
        "/usr/bin/nmcli", "connection", "delete", name, NULL
    };
    return safe_exec(argv);
}

int zyl_vpn_list_profiles(ZylVpnProfile **out, int *count) {
    if (!out || !count) return -1;

    /* nmcli로 VPN 연결 목록 */
    FILE *fp = popen(
        "nmcli -t -f NAME,TYPE,ACTIVE connection show 2>/dev/null | "
        "grep wireguard", "r");
    if (!fp) { *out = NULL; *count = 0; return -1; }

    int cap = 8, n = 0;
    ZylVpnProfile *profiles = calloc(cap, sizeof(ZylVpnProfile));
    if (!profiles) { pclose(fp); return -1; }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        char *name = strtok(line, ":");
        char *type = strtok(NULL, ":");
        char *active = strtok(NULL, ":");
        (void)type;

        if (!name) continue;
        if (n >= cap) {
            cap *= 2;
            ZylVpnProfile *tmp = realloc(profiles, cap * sizeof(ZylVpnProfile));
            if (!tmp) break;
            profiles = tmp;
        }

        profiles[n].name = strdup(name);
        profiles[n].type = ZYL_VPN_TYPE_WIREGUARD;
        profiles[n].config_path = NULL;
        profiles[n].state = (active && strcmp(active, "yes") == 0)
            ? ZYL_VPN_CONNECTED : ZYL_VPN_DISCONNECTED;
        n++;
    }
    pclose(fp);

    *out = profiles;
    *count = n;
    return 0;
}

void zyl_vpn_profile_free(ZylVpnProfile *profiles, int count) {
    if (!profiles) return;
    for (int i = 0; i < count; i++) {
        free(profiles[i].name);
        free(profiles[i].config_path);
    }
    free(profiles);
}
