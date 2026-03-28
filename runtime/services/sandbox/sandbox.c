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
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>

/* seccomp BPF 매크로 (리눅스 커널 헤더) */
#ifndef SECCOMP_SET_MODE_FILTER
#define SECCOMP_SET_MODE_FILTER 1
#endif

/* ─── 내부 구조체 ─── */
struct ZylSandbox {
    char *cgroup_root;         /* cgroup v2 마운트 포인트 */
    char *app_data_root;       /* 앱 데이터 루트 (/data/apps/) */
    char *shared_storage;      /* 공유 저장소 (/data/shared/) */
};

/* ─── 유틸리티 ─── */
static bool mkdir_p(const char *path, mode_t mode) {
    char tmp[512];
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
   L2: seccomp-bpf 시스콜 필터
   ═══════════════════════════════════════════════════════ */
static int apply_seccomp_filter(ZylSeccompProfile profile) {
    /*
     * BPF 프로그램으로 위험한 시스콜을 차단:
     *   - STRICT: read, write, exit, sigreturn, mmap 등만 허용
     *   - DEFAULT: ptrace, mount, reboot, kexec 등 차단
     *   - PERMISSIVE: kexec, reboot만 차단
     *
     * 실제 프로덕션에서는 libseccomp를 사용하여
     * 아키텍처 독립적인 필터를 생성합니다.
     */

    /* prctl로 no_new_privs 설정 (seccomp 필수 전제조건) */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        fprintf(stderr, "[Sandbox] PR_SET_NO_NEW_PRIVS failed: %s\n", strerror(errno));
        return -1;
    }

    if (profile == ZYL_SECCOMP_PERMISSIVE) {
        /* 시스템 앱: 최소 제한만 */
        return 0;
    }

    /*
     * 실제 구현에서는 libseccomp를 사용:
     *
     *   scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
     *   // 위험 시스콜 차단
     *   seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(ptrace), 0);
     *   seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(mount), 0);
     *   seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(umount2), 0);
     *   seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(reboot), 0);
     *   seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(kexec_load), 0);
     *   seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(init_module), 0);
     *   seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(delete_module), 0);
     *
     *   if (profile == ZYL_SECCOMP_STRICT) {
     *       // 추가 제한: 네트워크 소켓, raw IO 등
     *       seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(socket), 0);
     *       seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(ioctl), 0);
     *   }
     *
     *   seccomp_load(ctx);
     *   seccomp_release(ctx);
     */

    fprintf(stderr, "[Sandbox] seccomp profile=%d applied (stub — needs libseccomp)\n",
            profile);
    return 0;
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
        if (unshare(CLONE_NEWNET) != 0) {
            fprintf(stderr, "[Sandbox] network namespace failed: %s\n", strerror(errno));
        }
    }

    /* L4: cgroup 리소스 제한 */
    apply_cgroup_limits(sb->cgroup_root, policy->app_id, &policy->limits);

    /* L2: seccomp 시스콜 필터 (마지막에 적용) */
    apply_seccomp_filter(seccomp);

    /* 권한 드롭: setuid/setgid to unprivileged user */
    /*
     * 실제 구현:
     *   struct passwd *pw = getpwnam("zyl-app");
     *   setgid(pw->pw_gid);
     *   setuid(pw->pw_uid);
     */

    return 0;
}

/* ═══ 매니페스트에서 정책 생성 ═══ */
ZylSandboxPolicy *zyl_sandbox_policy_from_manifest(const char *app_json_path) {
    /*
     * app.json의 permissions 배열을 파싱하여 비트마스크로 변환:
     *   "camera" → ZYL_PERM_CAMERA
     *   "network" → ZYL_PERM_NETWORK
     *   "storage.shared" → ZYL_PERM_STORAGE_READ | ZYL_PERM_STORAGE_WRITE
     *   등
     *
     * 실제 구현에서는 json-glib로 파싱합니다.
     */
    (void)app_json_path;

    ZylSandboxPolicy *policy = calloc(1, sizeof(ZylSandboxPolicy));
    if (!policy) return NULL;

    /* 기본 리소스 제한 */
    policy->limits.memory_limit_bytes = 256 * 1024 * 1024;  /* 256MB */
    policy->limits.cpu_shares = 512;                         /* 절반 가중치 */
    policy->limits.max_pids = 32;                            /* 최대 32 프로세스 */
    policy->limits.disk_quota_bytes = 100 * 1024 * 1024;     /* 100MB */

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

    /* 간이 CPU% 계산 — 실제로는 두 시점 간 차이를 사용 */
    *out_percent = (float)(usage_usec % 100000) / 1000.0f;
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

    if (policy->permissions & ZYL_PERM_LOCATION) {
        written += snprintf(out_xml + written, max_len - written,
            "    <allow send_destination=\"org.zylos.LocationService\"/>\n");
    }
    if (policy->permissions & ZYL_PERM_BLUETOOTH) {
        written += snprintf(out_xml + written, max_len - written,
            "    <allow send_destination=\"org.bluez\"/>\n");
    }
    if (policy->permissions & ZYL_PERM_PHONE) {
        written += snprintf(out_xml + written, max_len - written,
            "    <allow send_destination=\"org.freedesktop.ModemManager1\"/>\n");
    }

    snprintf(out_xml + written, max_len - written,
        "  </policy>\n"
        "</busconfig>\n");

    return 0;
}
