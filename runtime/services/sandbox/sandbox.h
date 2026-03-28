/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 앱 샌드박싱 인터페이스 — seccomp-bpf, 네임스페이스, 파일 접근 제어
 * 수행범위: 앱별 권한 정책 정의, 프로세스 격리, 리소스 제한
 * 의존방향: stdbool.h, stdint.h, sys/types.h
 * SOLID: ISP — 샌드박싱 관련 인터페이스만 노출
 *        DIP — 구현이 아닌 정책 인터페이스에 의존
 *
 * 보안 계층:
 *   L1: 파일시스템 격리 (chroot/pivot_root + mount namespace)
 *   L2: 시스콜 필터링 (seccomp-bpf)
 *   L3: 네트워크 격리 (network namespace)
 *   L4: 리소스 제한 (cgroup)
 *   L5: IPC 제어 (D-Bus 정책)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_SANDBOX_H
#define ZYL_SANDBOX_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* ─── 앱 권한 플래그 ─── */
typedef enum {
    ZYL_PERM_NONE           = 0,
    ZYL_PERM_NETWORK        = (1 << 0),   /* 네트워크 접근 */
    ZYL_PERM_CAMERA         = (1 << 1),   /* 카메라 디바이스 */
    ZYL_PERM_MICROPHONE     = (1 << 2),   /* 마이크 */
    ZYL_PERM_LOCATION       = (1 << 3),   /* GPS/위치 */
    ZYL_PERM_STORAGE_READ   = (1 << 4),   /* 공유 저장소 읽기 */
    ZYL_PERM_STORAGE_WRITE  = (1 << 5),   /* 공유 저장소 쓰기 */
    ZYL_PERM_CONTACTS       = (1 << 6),   /* 연락처 */
    ZYL_PERM_PHONE          = (1 << 7),   /* 전화 기능 */
    ZYL_PERM_BLUETOOTH      = (1 << 8),   /* 블루투스 */
    ZYL_PERM_NOTIFICATION   = (1 << 9),   /* 알림 생성 */
    ZYL_PERM_BACKGROUND     = (1 << 10),  /* 백그라운드 실행 */
    ZYL_PERM_SYSTEM         = (1 << 31),  /* 시스템 앱 (모든 권한) */
} ZylPermission;

/* ─── 리소스 제한 ─── */
typedef struct {
    uint64_t memory_limit_bytes;   /* 최대 메모리 (0=무제한) */
    int cpu_shares;                /* cgroup cpu.shares (기본 1024) */
    int max_pids;                  /* 최대 프로세스 수 */
    uint64_t disk_quota_bytes;     /* 앱 데이터 디스크 제한 */
} ZylResourceLimits;

/* ─── 샌드박스 정책 ─── */
typedef struct {
    char *app_id;                  /* 앱 ID */
    uint32_t permissions;          /* ZylPermission 비트 마스크 */
    ZylResourceLimits limits;      /* 리소스 제한 */
    bool allow_dbus_system;        /* 시스템 D-Bus 접근 */
    bool allow_dbus_session;       /* 세션 D-Bus 접근 */
    char **readable_paths;         /* 읽기 허용 경로 목록 (NULL 종료) */
    char **writable_paths;         /* 쓰기 허용 경로 목록 (NULL 종료) */
    char **device_paths;           /* 접근 허용 디바이스 (NULL 종료) */
} ZylSandboxPolicy;

/* ─── seccomp 프로필 ─── */
typedef enum {
    ZYL_SECCOMP_STRICT,            /* 최소 시스콜만 허용 */
    ZYL_SECCOMP_DEFAULT,           /* 일반 앱용 (위험 시스콜 차단) */
    ZYL_SECCOMP_PERMISSIVE,        /* 시스템 앱용 (대부분 허용) */
} ZylSeccompProfile;

/* ─── 샌드박스 서비스 인터페이스 ─── */
typedef struct ZylSandbox ZylSandbox;

/* 서비스 생성/해제 */
ZylSandbox *zyl_sandbox_create(void);
void        zyl_sandbox_destroy(ZylSandbox *sb);

/*
 * 앱 프로세스를 샌드박스에 넣기
 * 이 함수는 fork() 후 자식 프로세스에서 exec() 전에 호출한다.
 * 호출 순서:
 *   1. mount namespace 생성
 *   2. 파일시스템 격리 (bind mount)
 *   3. cgroup 설정
 *   4. seccomp 필터 적용
 *   5. 권한 드롭 (setuid/setgid)
 */
int zyl_sandbox_apply(ZylSandbox *sb, const ZylSandboxPolicy *policy,
                       ZylSeccompProfile seccomp);

/* 정책 생성 헬퍼 */
ZylSandboxPolicy *zyl_sandbox_policy_from_manifest(const char *app_json_path);
void              zyl_sandbox_policy_free(ZylSandboxPolicy *policy);

/* 권한 검사 (런타임) */
bool zyl_sandbox_check_permission(const ZylSandboxPolicy *policy,
                                   ZylPermission perm);

/* 리소스 사용량 조회 */
int zyl_sandbox_get_memory_usage(const char *app_id, uint64_t *out_bytes);
int zyl_sandbox_get_cpu_usage(const char *app_id, float *out_percent);

/* D-Bus 정책 생성 */
int zyl_sandbox_generate_dbus_policy(const ZylSandboxPolicy *policy,
                                      char *out_xml, size_t max_len);

/* D-Bus 상수 */
#define ZYL_SANDBOX_DBUS_NAME "org.zylos.Sandbox"
#define ZYL_SANDBOX_DBUS_PATH "/org/zylos/Sandbox"

#endif /* ZYL_SANDBOX_H */
