#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 앱 샌드박싱 구현 — Linux namespace + seccomp + cgroup
 * 수행범위: 프로세스 격리, 시스콜 필터, 리소스 제한, 파일 접근 제어
 * 의존방향: sandbox.h, linux/seccomp.h, sched.h, sys/mount.h
 * SOLID: SRP — 프로세스 격리와 보안 정책만 담당
 * ────────────────────────────────────────────────────────── */

#include "sandbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <time.h>          /* clock_gettime, CLOCK_MONOTONIC */
#include <seccomp.h>       /* libseccomp API */
#include <pwd.h>           /* getpwnam for privilege drop */
#include <grp.h>           /* setgroups for privilege drop */
#include <spawn.h>         /* posix_spawn */
#include <sys/wait.h>      /* waitpid, WIFEXITED, WEXITSTATUS */
#include <json-glib/json-glib.h>  /* json-glib manifest parsing */

/* ─── 기본 리소스 제한 상수 ─── */
#define DEFAULT_MEMORY_LIMIT_MB   256
#define DEFAULT_CPU_SHARES        512
#define DEFAULT_MAX_PIDS          32
#define DEFAULT_DISK_QUOTA_MB     100
#define PATH_BUF_SIZE             512

/* ─── 내부 구조체 ─── */
struct ZylSandbox {
    char *cgroup_root;         /* cgroup v2 마운트 포인트 */
    char *app_data_root;       /* 앱 데이터 루트 (/data/apps/) */
    char *shared_storage;      /* 공유 저장소 (/data/shared/) */
};

/* ─── 유틸리티 ─── */
static bool mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_BUF_SIZE];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode) == 0 || errno == EEXIST;
}

static bool write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs(content, f);
    fclose(f);
    return true;
}

/* ─── 공개 API ─── */

ZylSandbox *zyl_sandbox_create(void) {
    ZylSandbox *sb = calloc(1, sizeof(ZylSandbox));
    if (!sb) return NULL;

    sb->cgroup_root = strdup("/sys/fs/cgroup");
    sb->app_data_root = strdup("/data/apps");
    sb->shared_storage = strdup("/data/shared");

    if (!sb->cgroup_root || !sb->app_data_root || !sb->shared_storage) {
        zyl_sandbox_destroy(sb);
        return NULL;
    }

    mkdir_p(sb->app_data_root, 0755);
    mkdir_p(sb->shared_storage, 0755);

    return sb;
}

void zyl_sandbox_destroy(ZylSandbox *sb) {
    if (!sb) return;
    free(sb->cgroup_root);
    free(sb->app_data_root);
    free(sb->shared_storage);
    free(sb);
}

/* ═══════════════════════════════════════════════════════
   L1: 파일시스템 격리 (mount namespace + bind mounts)
   ═══════════════════════════════════════════════════════ */
static int apply_filesystem_isolation(const ZylSandboxPolicy *policy,
                                       const char *app_data_root) {
    /*
     * 앱별 격리된 파일시스템 뷰:
     *   /app/       → 앱 바이너리 (읽기 전용)
     *   /data/      → 앱 전용 데이터 (읽기/쓰기)
     *   /shared/    → 공유 저장소 (권한에 따라)
     *   /tmp/       → tmpfs (앱별 격리)
     *   /proc, /dev → 필터링된 뷰
     */

    /* 앱 전용 데이터 디렉토리 */
    char app_data[512];
    snprintf(app_data, sizeof(app_data), "%s/%s", app_data_root, policy->app_id);
    mkdir_p(app_data, 0700);

    /* 앱 전용 tmp */
    char app_tmp[512];
    snprintf(app_tmp, sizeof(app_tmp), "%s/%s/tmp", app_data_root, policy->app_id);
    mkdir_p(app_tmp, 0700);

    /* tmpfs 마운트 */
    if (mount("tmpfs", app_tmp, "tmpfs", MS_NOSUID | MS_NODEV, "size=64M") != 0) {
        fprintf(stderr, "[Sandbox] tmpfs mount failed: %s\n", strerror(errno));
        /* 실패해도 계속 진행 — 비루트 환경에서는 실패할 수 있음 */
    }

    /* 읽기 전용 바인드 마운트 (허용된 경로만) */
    if (policy->readable_paths) {
        for (int i = 0; policy->readable_paths[i]; i++) {
            /* bind mount를 읽기 전용으로 */
            mount(policy->readable_paths[i], policy->readable_paths[i],
                  NULL, MS_BIND | MS_RDONLY, NULL);
        }
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════
   L2: seccomp-bpf 시스콜 필터 (libseccomp)
   ═══════════════════════════════════════════════════════ */

/**
 * Blocked syscalls for DEFAULT profile — dangerous for sandboxed apps.
 * These can escalate privileges, modify kernel state, or escape isolation.
 */
static const int BLOCKED_SYSCALLS_DEFAULT[] = {
    SCMP_SYS(ptrace),           /* process tracing — escape sandbox */
    SCMP_SYS(mount),            /* filesystem manipulation */
    SCMP_SYS(umount2),          /* filesystem manipulation */
    SCMP_SYS(reboot),           /* system reboot */
    SCMP_SYS(kexec_load),       /* kernel replacement */
    SCMP_SYS(init_module),      /* kernel module loading */
    SCMP_SYS(delete_module),    /* kernel module removal */
    SCMP_SYS(finit_module),     /* kernel module loading (fd) */
    SCMP_SYS(pivot_root),       /* root filesystem change */
    SCMP_SYS(swapon),           /* swap manipulation */
    SCMP_SYS(swapoff),          /* swap manipulation */
    SCMP_SYS(acct),             /* process accounting */
    SCMP_SYS(settimeofday),     /* system time manipulation */
    SCMP_SYS(clock_settime),    /* system clock manipulation */
    SCMP_SYS(adjtimex),         /* kernel clock tuning */
    SCMP_SYS(personality),      /* execution domain change */
    SCMP_SYS(unshare),          /* namespace escape */
    SCMP_SYS(setns),            /* namespace injection */
    SCMP_SYS(keyctl),           /* kernel keyring manipulation */
    SCMP_SYS(add_key),          /* kernel keyring */
    SCMP_SYS(request_key),      /* kernel keyring */
    SCMP_SYS(mbind),            /* NUMA memory policy */
    SCMP_SYS(move_pages),       /* NUMA page migration */
    SCMP_SYS(perf_event_open),  /* performance monitoring */
    SCMP_SYS(bpf),              /* eBPF — kernel programmability */
    SCMP_SYS(userfaultfd),      /* page fault interception */
};
static const int N_BLOCKED_DEFAULT =
    sizeof(BLOCKED_SYSCALLS_DEFAULT) / sizeof(BLOCKED_SYSCALLS_DEFAULT[0]);

/**
 * Additional syscalls blocked for STRICT profile.
 * Restricts network, raw device I/O, and debugging.
 */
static const int BLOCKED_SYSCALLS_STRICT[] = {
    SCMP_SYS(socket),           /* network socket creation */
    SCMP_SYS(bind),             /* network bind */
    SCMP_SYS(listen),           /* network listen */
    SCMP_SYS(accept),           /* network accept */
    SCMP_SYS(accept4),          /* network accept */
    SCMP_SYS(connect),          /* network connect */
    SCMP_SYS(sendto),           /* network send */
    SCMP_SYS(recvfrom),         /* network receive */
    SCMP_SYS(sendmsg),          /* network send */
    SCMP_SYS(recvmsg),          /* network receive */
    SCMP_SYS(ioctl),            /* device I/O control */
    SCMP_SYS(execve),           /* process replacement */
    SCMP_SYS(execveat),         /* process replacement */
    SCMP_SYS(fork),             /* process creation */
    SCMP_SYS(vfork),            /* process creation */
    SCMP_SYS(clone),            /* process/thread creation */
    SCMP_SYS(clone3),           /* process/thread creation */
};
static const int N_BLOCKED_STRICT =
    sizeof(BLOCKED_SYSCALLS_STRICT) / sizeof(BLOCKED_SYSCALLS_STRICT[0]);

/**
 * Minimal syscalls blocked even for PERMISSIVE (system apps).
 */
static const int BLOCKED_SYSCALLS_PERMISSIVE[] = {
    SCMP_SYS(kexec_load),       /* kernel replacement */
    SCMP_SYS(reboot),           /* system reboot (use systemd) */
    SCMP_SYS(bpf),              /* eBPF */
};
static const int N_BLOCKED_PERMISSIVE =
    sizeof(BLOCKED_SYSCALLS_PERMISSIVE) / sizeof(BLOCKED_SYSCALLS_PERMISSIVE[0]);

static int apply_seccomp_filter(ZylSeccompProfile profile) {
    /* PR_SET_NO_NEW_PRIVS is mandatory before seccomp */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        fprintf(stderr, "[Sandbox] PR_SET_NO_NEW_PRIVS failed: %s\n",
                strerror(errno));
        return -1;
    }

    /* Default action: ALLOW — we block specific dangerous syscalls */
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (!ctx) {
        fprintf(stderr, "[Sandbox] seccomp_init failed\n");
        return -1;
    }

    int ret = -1;
    const int *blocked = NULL;
    int n_blocked = 0;

    switch (profile) {
    case ZYL_SECCOMP_PERMISSIVE:
        blocked = BLOCKED_SYSCALLS_PERMISSIVE;
        n_blocked = N_BLOCKED_PERMISSIVE;
        break;
    case ZYL_SECCOMP_DEFAULT:
        blocked = BLOCKED_SYSCALLS_DEFAULT;
        n_blocked = N_BLOCKED_DEFAULT;
        break;
    case ZYL_SECCOMP_STRICT:
        /* STRICT includes DEFAULT + extra */
        blocked = BLOCKED_SYSCALLS_DEFAULT;
        n_blocked = N_BLOCKED_DEFAULT;
        break;
    }

    /* Apply blocked syscalls */
    for (int i = 0; i < n_blocked; i++) {
        int rc = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM),
                                  blocked[i], 0);
        if (rc < 0) {
            fprintf(stderr, "[Sandbox] seccomp_rule_add failed for "
                    "syscall %d: %s\n", blocked[i], strerror(-rc));
            /* Non-fatal: continue with remaining rules */
        }
    }

    /* STRICT profile: add extra restrictions */
    if (profile == ZYL_SECCOMP_STRICT) {
        for (int i = 0; i < N_BLOCKED_STRICT; i++) {
            int rc = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM),
                                      BLOCKED_SYSCALLS_STRICT[i], 0);
            if (rc < 0) {
                fprintf(stderr, "[Sandbox] seccomp_rule_add(strict) "
                        "failed for syscall %d: %s\n",
                        BLOCKED_SYSCALLS_STRICT[i], strerror(-rc));
            }
        }
    }

    /* Load the BPF filter into the kernel */
    ret = seccomp_load(ctx);
    if (ret < 0) {
        fprintf(stderr, "[Sandbox] seccomp_load failed: %s\n",
                strerror(-ret));
    } else {
        fprintf(stderr, "[Sandbox] seccomp profile=%d loaded "
                "(%d rules active)\n", profile,
                n_blocked + (profile == ZYL_SECCOMP_STRICT
                             ? N_BLOCKED_STRICT : 0));
        ret = 0;
    }

    seccomp_release(ctx);
    return ret;
}

/* ═══════════════════════════════════════════════════════
   L3: cgroup 리소스 제한
   ═══════════════════════════════════════════════════════ */
static int apply_cgroup_limits(const char *cgroup_root,
                                const char *app_id,
                                const ZylResourceLimits *limits) {
    /* cgroup v2 계층 구조:
     *   /sys/fs/cgroup/zyl-os/
     *     └── {app_id}/
     *         ├── memory.max
     *         ├── cpu.weight
     *         └── pids.max
     */

    char cgroup_path[512];
    snprintf(cgroup_path, sizeof(cgroup_path), "%s/zyl-os/%s", cgroup_root, app_id);
    mkdir_p(cgroup_path, 0755);

    char path[512];

    /* 메모리 제한 */
    if (limits->memory_limit_bytes > 0) {
        snprintf(path, sizeof(path), "%s/memory.max", cgroup_path);
        char val[32];
        snprintf(val, sizeof(val), "%lu", (unsigned long)limits->memory_limit_bytes);
        write_file(path, val);
    }

    /* CPU 가중치 (cgroup v2: cpu.weight 1-10000, 기본 100) */
    if (limits->cpu_shares > 0) {
        snprintf(path, sizeof(path), "%s/cpu.weight", cgroup_path);
        /* cgroup v1 shares를 v2 weight로 변환: weight = shares * 100 / 1024 */
        int weight = limits->cpu_shares * 100 / 1024;
        if (weight < 1) weight = 1;
        if (weight > 10000) weight = 10000;
        char val[32];
        snprintf(val, sizeof(val), "%d", weight);
        write_file(path, val);
    }

    /* 최대 프로세스 수 */
    if (limits->max_pids > 0) {
        snprintf(path, sizeof(path), "%s/pids.max", cgroup_path);
        char val[32];
        snprintf(val, sizeof(val), "%d", limits->max_pids);
        write_file(path, val);
    }

    /* 현재 프로세스를 cgroup에 등록 */
    snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_path);
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d", getpid());
    write_file(path, pid_str);

    return 0;
}

/* ═══ 샌드박스 적용 (fork 후 exec 전) ═══ */
int zyl_sandbox_apply(ZylSandbox *sb, const ZylSandboxPolicy *policy,
                       ZylSeccompProfile seccomp) {
    if (!sb || !policy) return -1;

    fprintf(stderr, "[Sandbox] Applying sandbox for %s (perms=0x%x)\n",
            policy->app_id, policy->permissions);

    /* 시스템 앱은 최소 격리 */
    if (policy->permissions & ZYL_PERM_SYSTEM) {
        fprintf(stderr, "[Sandbox] System app — minimal isolation\n");
        return apply_seccomp_filter(ZYL_SECCOMP_PERMISSIVE);
    }

    /* L1: 파일시스템 격리 */
    if (unshare(CLONE_NEWNS) == 0) {
        /* 마운트 전파 방지 */
        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
        apply_filesystem_isolation(policy, sb->app_data_root);
    } else {
        fprintf(stderr, "[Sandbox] mount namespace failed (need CAP_SYS_ADMIN): %s\n",
                strerror(errno));
    }

    /* L3: 네트워크 격리 (권한 없으면) */
    if (!(policy->permissions & ZYL_PERM_NETWORK)) {
        if (unshare(CLONE_NEWNET) == 0) {
            /* Bring up loopback in the new namespace so localhost works */
            pid_t lo_pid;
            char *lo_argv[] = {"ip", "link", "set", "lo", "up", NULL};
            char *lo_envp[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};
            int lo_up = posix_spawn(&lo_pid, "/sbin/ip", NULL, NULL, lo_argv, lo_envp);
            if (lo_up == 0) { int st; waitpid(lo_pid, &st, 0); lo_up = WIFEXITED(st) ? WEXITSTATUS(st) : -1; }
            if (lo_up != 0) {
                fprintf(stderr, "[Sandbox] loopback up failed "
                        "(ip command may be unavailable)\n");
            }
            fprintf(stderr, "[Sandbox] Network namespace created "
                    "(loopback only)\n");
        } else {
            fprintf(stderr, "[Sandbox] network namespace failed "
                    "(need CAP_SYS_ADMIN): %s\n", strerror(errno));
        }
    }

    /* L4: cgroup 리소스 제한 */
    apply_cgroup_limits(sb->cgroup_root, policy->app_id, &policy->limits);

    /* L5-IPC: D-Bus 정책 적용 — 앱별 접근 가능 서비스 제한 */
    {
        /*
         * Validate app_id for safe use in filesystem paths.
         * app_id follows reverse-DNS notation (e.g. com.zylos.browser)
         * which allows letters, digits, dot, dash, underscore.
         * Forward slash is the only truly unsafe character for path construction.
         * We sanitize by replacing '.' with '_' in the generated filename,
         * not by rejecting valid app_ids.
         */
        bool app_id_valid = (policy->app_id && policy->app_id[0] != '\0');
        if (app_id_valid) {
            for (const char *p = policy->app_id; *p; p++) {
                /* Reject only path-unsafe and shell-unsafe characters */
                if (*p == '/' || *p == '\0') { app_id_valid = false; break; }
            }
            /* Reject path traversal */
            if (strstr(policy->app_id, "..") != NULL) app_id_valid = false;
        }
        if (!app_id_valid) {
            fprintf(stderr, "[Sandbox] Invalid app_id for D-Bus policy: %s\n",
                    policy->app_id ? policy->app_id : "(null)");
            return -1;
        }

        char dbus_xml[2048];
        if (zyl_sandbox_generate_dbus_policy(policy, dbus_xml,
                                              sizeof(dbus_xml)) == 0) {
            /* Sanitize app_id for filename: replace '.' with '-' */
            char safe_app_id[256];
            snprintf(safe_app_id, sizeof(safe_app_id), "%s", policy->app_id);
            for (char *q = safe_app_id; *q; q++) {
                if (*q == '.') *q = '-';
            }
            char policy_path[512];
            snprintf(policy_path, sizeof(policy_path),
                     "/etc/dbus-1/system.d/zyl-app-%s.conf", safe_app_id);
            FILE *pf = fopen(policy_path, "w");
            if (pf) {
                fputs(dbus_xml, pf);
                fclose(pf);
                /* Reload D-Bus configuration */
                {
                    pid_t dbus_pid;
                    char *dbus_argv[] = {"dbus-send", "--system", "--type=method_call",
                                         "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus.ReloadConfig", NULL};
                    char *dbus_envp[] = {"PATH=/usr/bin:/bin", NULL};
                    int dbus_rc = posix_spawn(&dbus_pid, "/usr/bin/dbus-send", NULL, NULL, dbus_argv, dbus_envp);
                    if (dbus_rc == 0) { int st; waitpid(dbus_pid, &st, 0); }
                }
                fprintf(stderr, "[Sandbox] D-Bus policy applied: %s\n",
                        policy_path);
            }
        }
    }

    /* L2: seccomp 시스콜 필터 (마지막에 적용) */
    apply_seccomp_filter(seccomp);

    /* L5: 권한 드롭 — setgid/setuid to unprivileged "zyl-app" user.
     * This MUST be the last step — after namespaces, cgroups, and seccomp.
     * Failure to drop privileges is a security-critical error — we must
     * abort the sandbox setup to prevent running with elevated privileges. */
    if (!(policy->permissions & ZYL_PERM_SYSTEM)) {
        struct passwd *pw = getpwnam("zyl-app");
        if (pw) {
            /* Drop supplementary groups */
            if (setgroups(0, NULL) != 0) {
                fprintf(stderr, "[Sandbox] setgroups(0) failed: %s\n",
                        strerror(errno));
                return -1;
            }
            /* Set GID before UID (cannot setgid after dropping root) */
            if (setgid(pw->pw_gid) != 0) {
                fprintf(stderr, "[Sandbox] setgid(%d) failed: %s\n",
                        pw->pw_gid, strerror(errno));
                return -1;
            }
            if (setuid(pw->pw_uid) != 0) {
                fprintf(stderr, "[Sandbox] setuid(%d) failed: %s\n",
                        pw->pw_uid, strerror(errno));
                return -1;
            }
            fprintf(stderr, "[Sandbox] Dropped to uid=%d gid=%d\n",
                    pw->pw_uid, pw->pw_gid);
        } else {
            fprintf(stderr, "[Sandbox] FATAL: user 'zyl-app' not found "
                    "— refusing to run without privilege drop\n");
            return -1;
        }
    }

    return 0;
}

/*
 * Permission string → bitmask mapping.
 * app.json "permissions" array entry → ZylPermission flag.
 */
static uint32_t permission_from_string(const char *perm) {
    if (!perm) return 0;
    if (strcmp(perm, "camera")          == 0) return ZYL_PERM_CAMERA;
    if (strcmp(perm, "network")         == 0) return ZYL_PERM_NETWORK;
    if (strcmp(perm, "storage.read")    == 0) return ZYL_PERM_STORAGE_READ;
    if (strcmp(perm, "storage.write")   == 0) return ZYL_PERM_STORAGE_WRITE;
    if (strcmp(perm, "storage.shared")  == 0) return ZYL_PERM_STORAGE_READ | ZYL_PERM_STORAGE_WRITE;
    if (strcmp(perm, "location")        == 0) return ZYL_PERM_LOCATION;
    if (strcmp(perm, "bluetooth")       == 0) return ZYL_PERM_BLUETOOTH;
    if (strcmp(perm, "phone")           == 0) return ZYL_PERM_PHONE;
    if (strcmp(perm, "contacts")        == 0) return ZYL_PERM_CONTACTS;
    if (strcmp(perm, "notifications")   == 0) return ZYL_PERM_NOTIFICATIONS;
    if (strcmp(perm, "system")          == 0) return ZYL_PERM_SYSTEM;
    return 0;
}

/* ═══ 매니페스트에서 정책 생성 ═══ */
ZylSandboxPolicy *zyl_sandbox_policy_from_manifest(const char *app_json_path) {
    /*
     * Parse app.json and convert the "permissions" array into a bitmask.
     * Also extracts "id", "memory_limit_mb", "cpu_shares", "max_pids".
     *
     * app.json minimal structure:
     * {
     *   "id": "com.example.app",
     *   "permissions": ["camera", "network"],
     *   "memory_limit_mb": 128,
     *   "cpu_shares": 512,
     *   "max_pids": 32
     * }
     */
    ZylSandboxPolicy *policy = calloc(1, sizeof(ZylSandboxPolicy));
    if (!policy) return NULL;

    /* Apply defaults */
    policy->limits.memory_limit_bytes = (size_t)DEFAULT_MEMORY_LIMIT_MB * 1024 * 1024;
    policy->limits.cpu_shares = DEFAULT_CPU_SHARES;
    policy->limits.max_pids = DEFAULT_MAX_PIDS;
    policy->limits.disk_quota_bytes = (size_t)DEFAULT_DISK_QUOTA_MB * 1024 * 1024;

    if (!app_json_path || app_json_path[0] == '\0') {
        fprintf(stderr, "[Sandbox] policy_from_manifest: no path provided, "
                "using defaults\n");
        return policy;
    }

    GError *gerr = NULL;
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, app_json_path, &gerr)) {
        fprintf(stderr, "[Sandbox] Failed to parse manifest %s: %s\n",
                app_json_path, gerr ? gerr->message : "unknown");
        g_clear_error(&gerr);
        g_object_unref(parser);
        return policy;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        fprintf(stderr, "[Sandbox] Manifest root is not a JSON object: %s\n",
                app_json_path);
        g_object_unref(parser);
        return policy;
    }

    JsonObject *obj = json_node_get_object(root);

    /* Extract app_id */
    if (json_object_has_member(obj, "id")) {
        const char *id = json_object_get_string_member(obj, "id");
        if (id && id[0]) {
            policy->app_id = strdup(id);
        }
    }

    /* Parse permissions array */
    if (json_object_has_member(obj, "permissions")) {
        JsonNode *perms_node = json_object_get_member(obj, "permissions");
        if (JSON_NODE_HOLDS_ARRAY(perms_node)) {
            JsonArray *perms_arr = json_node_get_array(perms_node);
            guint n = json_array_get_length(perms_arr);
            for (guint i = 0; i < n; i++) {
                JsonNode *pnode = json_array_get_element(perms_arr, i);
                if (JSON_NODE_HOLDS_VALUE(pnode) &&
                    json_node_get_value_type(pnode) == G_TYPE_STRING) {
                    const char *pstr = json_node_get_string(pnode);
                    policy->permissions |= permission_from_string(pstr);
                }
            }
        }
    }

    /* Optional resource override fields */
    if (json_object_has_member(obj, "memory_limit_mb")) {
        gint64 mb = json_object_get_int_member(obj, "memory_limit_mb");
        if (mb > 0 && mb <= 4096)
            policy->limits.memory_limit_bytes = (size_t)mb * 1024 * 1024;
    }
    if (json_object_has_member(obj, "cpu_shares")) {
        gint64 shares = json_object_get_int_member(obj, "cpu_shares");
        if (shares > 0 && shares <= 10000)
            policy->limits.cpu_shares = (int)shares;
    }
    if (json_object_has_member(obj, "max_pids")) {
        gint64 mp = json_object_get_int_member(obj, "max_pids");
        if (mp > 0 && mp <= 4096)
            policy->limits.max_pids = (int)mp;
    }

    g_object_unref(parser);

    fprintf(stderr, "[Sandbox] Parsed manifest %s: app_id=%s perms=0x%x\n",
            app_json_path,
            policy->app_id ? policy->app_id : "(none)",
            policy->permissions);
    return policy;
}

void zyl_sandbox_policy_free(ZylSandboxPolicy *policy) {
    if (!policy) return;
    free(policy->app_id);
    if (policy->readable_paths) {
        for (int i = 0; policy->readable_paths[i]; i++)
            free(policy->readable_paths[i]);
        free(policy->readable_paths);
    }
    if (policy->writable_paths) {
        for (int i = 0; policy->writable_paths[i]; i++)
            free(policy->writable_paths[i]);
        free(policy->writable_paths);
    }
    if (policy->device_paths) {
        for (int i = 0; policy->device_paths[i]; i++)
            free(policy->device_paths[i]);
        free(policy->device_paths);
    }
    free(policy);
}

bool zyl_sandbox_check_permission(const ZylSandboxPolicy *policy,
                                   ZylPermission perm) {
    if (!policy) return false;
    if (policy->permissions & ZYL_PERM_SYSTEM) return true;
    return (policy->permissions & perm) != 0;
}

int zyl_sandbox_get_memory_usage(const char *app_id, uint64_t *out_bytes) {
    if (!app_id || !out_bytes) return -1;

    char path[512];
    snprintf(path, sizeof(path),
             "/sys/fs/cgroup/zyl-os/%s/memory.current", app_id);

    FILE *f = fopen(path, "r");
    if (!f) { *out_bytes = 0; return -1; }
    if (fscanf(f, "%lu", (unsigned long *)out_bytes) != 1) *out_bytes = 0;
    fclose(f);
    return 0;
}

int zyl_sandbox_get_cpu_usage(const char *app_id, float *out_percent) {
    if (!app_id || !out_percent) return -1;

    char path[512];
    snprintf(path, sizeof(path),
             "/sys/fs/cgroup/zyl-os/%s/cpu.stat", app_id);

    FILE *f = fopen(path, "r");
    if (!f) { *out_percent = 0.0f; return -1; }

    uint64_t usage_usec = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "usage_usec", 10) == 0) {
            sscanf(line, "usage_usec %lu", (unsigned long *)&usage_usec);
        }
    }
    fclose(f);

    /* CPU% = delta_usage / delta_wall * 100.
     * Single-sample approximation: use usage since boot / uptime.
     * For accurate per-interval measurement, caller should track
     * previous values and compute delta. */
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t wall_usec = (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
        if (wall_usec > 0) {
            *out_percent = (float)((double)usage_usec / (double)wall_usec * 100.0);
        } else {
            *out_percent = 0.0f;
        }
    }
    return 0;
}

int zyl_sandbox_generate_dbus_policy(const ZylSandboxPolicy *policy,
                                      char *out_xml, size_t max_len) {
    if (!policy || !out_xml) return -1;

    /*
     * D-Bus 정책 XML 생성:
     * 앱이 접근할 수 있는 D-Bus 서비스를 제한합니다.
     *
     * 예:
     *   <policy context="default">
     *     <deny send_destination="*"/>
     *     <allow send_destination="org.zylos.WebAppManager"/>
     *     <allow send_destination="org.zylos.NotificationManager"/>
     *     <!-- 권한에 따라 추가 -->
     *     <allow send_destination="org.zylos.LocationService"/>  <!-- if LOCATION perm -->
     *   </policy>
     */

    int written = snprintf(out_xml, max_len,
        "<busconfig>\n"
        "  <policy user=\"zyl-app\">\n"
        "    <deny send_destination=\"*\"/>\n"
        "    <allow send_destination=\"org.zylos.WebAppManager\"/>\n"
        "    <allow send_destination=\"org.zylos.Notification\"/>\n");

    /* Guard against snprintf truncation: if written >= max_len, buffer is full */
    if (written < 0 || (size_t)written >= max_len) return -1;

    if (policy->permissions & ZYL_PERM_LOCATION) {
        int n = snprintf(out_xml + written, max_len - written,
            "    <allow send_destination=\"org.zylos.LocationService\"/>\n");
        if (n > 0) written += n;
        if ((size_t)written >= max_len) return -1;
    }
    if (policy->permissions & ZYL_PERM_BLUETOOTH) {
        int n = snprintf(out_xml + written, max_len - written,
            "    <allow send_destination=\"org.bluez\"/>\n");
        if (n > 0) written += n;
        if ((size_t)written >= max_len) return -1;
    }
    if (policy->permissions & ZYL_PERM_PHONE) {
        int n = snprintf(out_xml + written, max_len - written,
            "    <allow send_destination=\"org.freedesktop.ModemManager1\"/>\n");
        if (n > 0) written += n;
        if ((size_t)written >= max_len) return -1;
    }

    snprintf(out_xml + written, max_len - written,
        "  </policy>\n"
        "</busconfig>\n");

    return 0;
}
