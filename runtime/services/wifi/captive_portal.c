#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Application Layer - Service
 *
 * 역할: 캡티브 포털 감지 — WiFi 연결 후 인터넷 접근성 확인
 * 수행범위: HTTP 204 프로브, 302 리다이렉트 감지, 포털 URL 추출
 * 의존방향: gio/gio.h, stdio
 * SOLID: SRP — 캡티브 포털 감지만 담당
 * ────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <spawn.h>

#define CAPTIVE_PORTAL_URL "http://connectivitycheck.zylos.dev/generate_204"
#define CAPTIVE_PORTAL_FALLBACK "http://clients3.google.com/generate_204"

typedef enum {
    ZYL_NETWORK_CONNECTED = 0,     /* 인터넷 정상 */
    ZYL_NETWORK_CAPTIVE_PORTAL = 1, /* 캡티브 포털 감지 */
    ZYL_NETWORK_NO_INTERNET = 2,   /* 인터넷 없음 */
} ZylNetworkStatus;

typedef struct {
    ZylNetworkStatus status;
    char portal_url[512]; /* 캡티브 포털 리다이렉트 URL (있을 때만) */
} ZylCaptiveResult;

/**
 * 캡티브 포털 감지.
 * HTTP 204 → 인터넷 정상
 * HTTP 302/301 → 캡티브 포털 (Location 헤더 추출)
 * 타임아웃/에러 → 인터넷 없음
 */
ZylCaptiveResult zyl_captive_portal_check(void) {
    ZylCaptiveResult result = { ZYL_NETWORK_NO_INTERNET, "" };

    /* curl로 HTTP 상태 코드 + Location 헤더 확인 */
    int pipefd[2];
    if (pipe(pipefd) < 0) return result;

    pid_t pid;
    const char *argv[] = {
        "/usr/bin/curl", "-s", "-o", "/dev/null",
        "-w", "%{http_code}\\n%{redirect_url}",
        "-m", "5", "--connect-timeout", "3",
        "-L", "--max-redirs", "0",
        CAPTIVE_PORTAL_URL, NULL
    };
    char *safe_env[] = { "PATH=/usr/bin:/bin", NULL };

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);

    int rc = posix_spawn(&pid, "/usr/bin/curl", &actions, NULL,
                         (char *const *)argv, safe_env);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);

    if (rc != 0) {
        close(pipefd[0]);
        return result;
    }

    /* 자식 출력 읽기 */
    char buf[1024] = {0};
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (n <= 0) return result;
    buf[n] = '\0';

    /* 파싱: "http_code\nredirect_url" */
    char *nl = strchr(buf, '\n');
    int http_code = atoi(buf);
    char *redirect_url = nl ? nl + 1 : "";

    /* 후행 공백/개행 제거 */
    size_t rlen = strlen(redirect_url);
    while (rlen > 0 && (redirect_url[rlen - 1] == '\n' ||
                         redirect_url[rlen - 1] == '\r' ||
                         redirect_url[rlen - 1] == ' ')) {
        redirect_url[--rlen] = '\0';
    }

    if (http_code == 204) {
        result.status = ZYL_NETWORK_CONNECTED;
    } else if (http_code >= 300 && http_code < 400) {
        result.status = ZYL_NETWORK_CAPTIVE_PORTAL;
        if (rlen > 0 && rlen < sizeof(result.portal_url)) {
            strncpy(result.portal_url, redirect_url,
                    sizeof(result.portal_url) - 1);
        }
        fprintf(stderr, "[CaptivePortal] Detected: HTTP %d → %s\n",
                http_code, result.portal_url);
    } else if (http_code == 200) {
        /* 200은 포털 페이지 자체를 반환한 것일 수 있음 */
        result.status = ZYL_NETWORK_CAPTIVE_PORTAL;
    } else {
        result.status = ZYL_NETWORK_NO_INTERNET;
    }

    return result;
}
