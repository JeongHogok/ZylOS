#define _GNU_SOURCE
/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Service
 *
 * 역할: 앱별 UID/GID 관리 — 설치 시 고유 사용자 할당
 * 수행범위: UID 할당, /etc/passwd 등록, 데이터 디렉토리 소유권 설정
 * 의존방향: sandbox.h, stdio, pwd.h, grp.h
 * SOLID: SRP — 앱 사용자 관리만 담당
 * ────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <spawn.h>
#include <sys/wait.h>

/* 앱 UID 범위: 10000~19999 (Android 호환 범위) */
#define APP_UID_MIN  10000
#define APP_UID_MAX  19999
#define APP_DATA_DIR "/data/apps"

/**
 * 앱 ID 유효성 검사 — [a-z0-9._] 문자만 허용 (command injection 방지).
 */
static bool is_valid_app_id(const char *id) {
    if (!id || !id[0]) return false;
    for (const char *p = id; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_'))
            return false;
    }
    return true;
}

/**
 * 앱 ID → 유닉스 사용자명 변환.
 * "com.zylos.browser" → "zyl_com_zylos_browser"
 */
static void app_id_to_username(const char *app_id, char *out, size_t len) {
    snprintf(out, len, "zyl_%s", app_id);
    /* 점(.)을 밑줄(_)로 변환 — 유닉스 사용자명 규칙 */
    for (char *p = out; *p; p++) {
        if (*p == '.') *p = '_';
    }
}

/**
 * 사용 가능한 다음 UID를 찾는다.
 */
static uid_t find_next_uid(void) {
    for (uid_t uid = APP_UID_MIN; uid <= APP_UID_MAX; uid++) {
        if (getpwuid(uid) == NULL) return uid;
    }
    return 0; /* 공간 부족 */
}

/**
 * 앱 설치 시 호출: 고유 UID 할당 + 데이터 디렉토리 생성.
 * Returns: 할당된 UID, 또는 실패 시 -1.
 */
int zyl_app_uid_create(const char *app_id) {
    if (!app_id) return -1;
    if (!is_valid_app_id(app_id)) return -1;

    char username[128];
    app_id_to_username(app_id, username, sizeof(username));

    /* 이미 존재하면 기존 UID 반환 */
    struct passwd *existing = getpwnam(username);
    if (existing) return (int)existing->pw_uid;

    uid_t uid = find_next_uid();
    if (uid == 0) {
        fprintf(stderr, "[AppUID] No available UIDs in range %d-%d\n",
                APP_UID_MIN, APP_UID_MAX);
        return -1;
    }

    /* useradd — posix_spawn으로 실행 (command injection 방지) */
    char home_dir[256];
    snprintf(home_dir, sizeof(home_dir), "%s/%s", APP_DATA_DIR, app_id);
    char uid_str[16];
    snprintf(uid_str, sizeof(uid_str), "%d", uid);
    char *argv[] = {"useradd", "-r", "-M", "-d", home_dir, "-s", "/usr/sbin/nologin", "-u", uid_str, username, NULL};
    char *envp[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};
    pid_t pid;
    int ret = posix_spawn(&pid, "/usr/sbin/useradd", NULL, NULL, argv, envp);
    if (ret == 0) waitpid(pid, &ret, 0);
    if (ret != 0) {
        /* useradd 실패 — /etc/passwd 직접 추가 폴백 */
        FILE *pw = fopen("/etc/passwd", "a");
        if (pw) {
            fprintf(pw, "%s:x:%d:%d:ZylOS App %s:%s/%s:/usr/sbin/nologin\n",
                    username, uid, uid, app_id, APP_DATA_DIR, app_id);
            fclose(pw);
        } else {
            fprintf(stderr, "[AppUID] Failed to create user %s: %s\n",
                    username, strerror(errno));
            return -1;
        }
    }

    /* 앱 데이터 디렉토리 생성 + 소유권 설정 */
    char data_dir[256];
    snprintf(data_dir, sizeof(data_dir), "%s/%s", APP_DATA_DIR, app_id);
    mkdir(data_dir, 0700);
    chown(data_dir, uid, uid);

    fprintf(stderr, "[AppUID] Created user %s (uid=%d) for %s\n",
            username, uid, app_id);
    return (int)uid;
}

/**
 * 앱 제거 시 호출: UID 삭제 + 데이터 디렉토리 삭제.
 */
int zyl_app_uid_remove(const char *app_id) {
    if (!app_id) return -1;
    if (!is_valid_app_id(app_id)) return -1;

    char username[128];
    app_id_to_username(app_id, username, sizeof(username));

    /* userdel — posix_spawn으로 실행 (command injection 방지) */
    char *argv[] = {"userdel", username, NULL};
    char *envp[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};
    pid_t pid;
    int rc = posix_spawn(&pid, "/usr/sbin/userdel", NULL, NULL, argv, envp);
    if (rc == 0) waitpid(pid, &rc, 0);

    /* 데이터 디렉토리 삭제는 appstore가 담당 */
    fprintf(stderr, "[AppUID] Removed user %s for %s\n", username, app_id);
    return 0;
}

/**
 * 앱의 UID를 조회한다.
 * Returns: UID, 또는 미등록 시 -1.
 */
int zyl_app_uid_lookup(const char *app_id) {
    if (!app_id) return -1;
    char username[128];
    app_id_to_username(app_id, username, sizeof(username));
    struct passwd *pw = getpwnam(username);
    return pw ? (int)pw->pw_uid : -1;
}
