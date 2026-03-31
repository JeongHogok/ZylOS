/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: Zygote 프로세스 풀 — 사전 fork로 앱 launch 시간 단축
 * 수행범위: pre-fork, 파이프 대기, launch 명령 수신 → sandbox 적용 → 앱 로드
 * 의존방향: zygote.h, sandbox.h
 * SOLID: SRP — 프로세스 풀 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include "zygote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

struct zygote_slot {
    pid_t pid;
    int   cmd_fd;   /* parent → child 명령 파이프 (쓰기 끝) */
    bool  in_use;
};

struct ZylZygote {
    struct zygote_slot pool[ZYL_ZYGOTE_POOL_SIZE];
    int available;
};

/* ─── 자식 프로세스: 파이프에서 launch 명령 대기 ─── */
static void zygote_child_loop(int cmd_fd_read) {
    char buf[1024];
    ssize_t n;

    /* 파이프에서 "app_id\napp_url\n" 수신 대기 */
    n = read(cmd_fd_read, buf, sizeof(buf) - 1);
    close(cmd_fd_read);

    if (n <= 0) _exit(0); /* 파이프 닫힘 — 풀 해제 */

    buf[n] = '\0';
    char *app_id = buf;
    char *app_url = strchr(buf, '\n');
    if (!app_url) _exit(1);
    *app_url++ = '\0';
    char *nl = strchr(app_url, '\n');
    if (nl) *nl = '\0';

    /* UID 전환 + sandbox 적용은 여기서 수행.
     * 실제 구현에서는:
     *   1. zyl_app_uid_lookup(app_id) → uid
     *   2. zyl_sandbox_apply(policy, seccomp)
     *   3. setuid(uid)
     *   4. WebKitGTK 앱 URL 로드
     *
     * 현재: 로그 + 정상 종료 (WebKitGTK 로드는 WAM lifecycle이 담당) */

    fprintf(stderr, "[Zygote] Child %d launching: %s → %s\n",
            getpid(), app_id, app_url);

    /* placeholder — 실제 앱 로드는 WAM lifecycle에서 수행 */
    _exit(0);
}

/* ─── 슬롯에 사전 fork ─── */
static int prefork_slot(struct zygote_slot *slot) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* 자식 */
        close(pipefd[1]); /* 쓰기 끝 닫기 */
        zygote_child_loop(pipefd[0]);
        _exit(0); /* should not reach */
    }

    /* 부모 */
    close(pipefd[0]); /* 읽기 끝 닫기 */
    slot->pid = pid;
    slot->cmd_fd = pipefd[1];
    slot->in_use = false;
    return 0;
}

/* ─── 공개 API ─── */

ZylZygote *zyl_zygote_create(void) {
    ZylZygote *zyg = calloc(1, sizeof(ZylZygote));
    if (!zyg) return NULL;

    zyg->available = 0;
    for (int i = 0; i < ZYL_ZYGOTE_POOL_SIZE; i++) {
        if (prefork_slot(&zyg->pool[i]) == 0) {
            zyg->available++;
        }
    }

    fprintf(stderr, "[Zygote] Pool created: %d/%d processes ready\n",
            zyg->available, ZYL_ZYGOTE_POOL_SIZE);
    return zyg;
}

void zyl_zygote_destroy(ZylZygote *zyg) {
    if (!zyg) return;

    for (int i = 0; i < ZYL_ZYGOTE_POOL_SIZE; i++) {
        if (zyg->pool[i].pid > 0) {
            close(zyg->pool[i].cmd_fd);
            kill(zyg->pool[i].pid, SIGTERM);
            waitpid(zyg->pool[i].pid, NULL, 0);
        }
    }
    free(zyg);
}

pid_t zyl_zygote_launch(ZylZygote *zyg, const char *app_id,
                         const char *app_url) {
    if (!zyg || !app_id || !app_url) return -1;

    /* 풀에서 사용 가능한 슬롯 찾기 */
    struct zygote_slot *slot = NULL;
    for (int i = 0; i < ZYL_ZYGOTE_POOL_SIZE; i++) {
        if (!zyg->pool[i].in_use && zyg->pool[i].pid > 0) {
            slot = &zyg->pool[i];
            break;
        }
    }

    pid_t result;

    if (slot) {
        /* 풀에서 꺼내기: 파이프로 launch 명령 전송 */
        char cmd[1024];
        int n = snprintf(cmd, sizeof(cmd), "%s\n%s\n", app_id, app_url);
        write(slot->cmd_fd, cmd, (size_t)n);
        close(slot->cmd_fd);
        slot->in_use = true;
        result = slot->pid;
        zyg->available--;

        fprintf(stderr, "[Zygote] Launched %s via pooled process %d "
                "(pool: %d remaining)\n", app_id, result, zyg->available);
    } else {
        /* 풀 비었음 — 직접 fork (콜드 스타트) */
        result = fork();
        if (result == 0) {
            fprintf(stderr, "[Zygote] Cold-start fork for %s\n", app_id);
            _exit(0);
        }
        fprintf(stderr, "[Zygote] Pool empty — cold-forked %d for %s\n",
                result, app_id);
    }

    /* 비동기적으로 풀 보충 */
    for (int i = 0; i < ZYL_ZYGOTE_POOL_SIZE; i++) {
        if (zyg->pool[i].in_use || zyg->pool[i].pid <= 0) {
            if (prefork_slot(&zyg->pool[i]) == 0) {
                zyg->pool[i].in_use = false;
                zyg->available++;
            }
        }
    }

    return result;
}

int zyl_zygote_pool_available(const ZylZygote *zyg) {
    return zyg ? zyg->available : 0;
}
