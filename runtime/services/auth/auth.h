/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 생체 인증 서비스 인터페이스 — 지문 등록/검증 HAL
 * 수행범위: libfprint 기반 지문 등록(enroll), 검증(verify), 존재 확인,
 *          D-Bus org.zylos.AuthService 인터페이스 노출
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 생체 인증 관련 인터페이스만 노출
 *        DIP — 구체 libfprint 구현에 비의존, 인터페이스로만 참조
 *
 * 실기기: libfprint + net.reactivated.Fprint D-Bus 프록시
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_AUTH_H
#define ZYL_AUTH_H

#include <stdbool.h>
#include <stdint.h>

/* ─── 인증 결과 코드 ─── */
typedef enum {
    ZYL_AUTH_OK            =  0,   /* 성공 */
    ZYL_AUTH_ERR_GENERAL   = -1,   /* 일반 오류 */
    ZYL_AUTH_ERR_NO_DEVICE = -2,   /* 지문 장치 없음 */
    ZYL_AUTH_ERR_TIMEOUT   = -3,   /* 인증 타임아웃 */
    ZYL_AUTH_ERR_NO_MATCH  = -4,   /* 지문 불일치 */
    ZYL_AUTH_ERR_ALREADY   = -5,   /* 이미 등록됨 */
    ZYL_AUTH_ERR_CANCELLED = -6,   /* 사용자 취소 */
    ZYL_AUTH_ERR_DBUS      = -7,   /* D-Bus 통신 오류 */
} ZylAuthResult;

/* ─── 등록 진행 콜백 ─── */
typedef enum {
    ZYL_ENROLL_STEP_PLACE,         /* 손가락 올려놓기 */
    ZYL_ENROLL_STEP_LIFT,          /* 손가락 떼기 */
    ZYL_ENROLL_STEP_AGAIN,         /* 다시 올려놓기 */
    ZYL_ENROLL_STEP_SWIPE,         /* 스와이프 (스와이프 센서) */
    ZYL_ENROLL_STEP_COMPLETE,      /* 등록 완료 */
    ZYL_ENROLL_STEP_FAIL,          /* 이 단계 실패, 재시도 요구 */
} ZylEnrollStep;

typedef void (*zyl_auth_enroll_progress_fn)(ZylEnrollStep step,
                                             int percent_done,
                                             void *user_data);

/* ─── 서비스 인터페이스 ─── */
typedef struct ZylAuthService ZylAuthService;

/**
 * zyl_auth_create: AuthService 인스턴스 생성.
 * D-Bus net.reactivated.Fprint 프록시 연결 포함.
 * @return 인스턴스 포인터, 실패 시 NULL
 */
ZylAuthService *zyl_auth_create(void);

/**
 * zyl_auth_destroy: AuthService 인스턴스 해제.
 */
void zyl_auth_destroy(ZylAuthService *svc);

/**
 * zyl_auth_enroll_fingerprint: 지문 등록 시작.
 * 등록 완료까지 블로킹 (별도 스레드 내부 구동).
 * @param svc      서비스 인스턴스
 * @param username 등록할 시스템 사용자명
 * @param finger   지문 식별자 (0=오른쪽 검지, 1=왼쪽 검지 등)
 * @param progress 단계별 진행 콜백 (NULL 가능)
 * @param user_data 콜백에 전달할 사용자 데이터
 * @return ZylAuthResult
 */
ZylAuthResult zyl_auth_enroll_fingerprint(ZylAuthService *svc,
                                           const char *username,
                                           int finger,
                                           zyl_auth_enroll_progress_fn progress,
                                           void *user_data);

/**
 * zyl_auth_verify_fingerprint: 저장된 지문과 입력 지문 비교.
 * @param svc      서비스 인스턴스
 * @param username 검증할 사용자명
 * @return ZYL_AUTH_OK(일치), ZYL_AUTH_ERR_NO_MATCH(불일치), 기타 오류코드
 */
ZylAuthResult zyl_auth_verify_fingerprint(ZylAuthService *svc,
                                           const char *username);

/**
 * zyl_auth_has_fingerprint: 해당 사용자에 등록된 지문이 있는지 확인.
 * @param svc      서비스 인스턴스
 * @param username 확인할 사용자명
 * @return true=등록됨, false=없음
 */
bool zyl_auth_has_fingerprint(const ZylAuthService *svc,
                               const char *username);

/* ─── D-Bus 상수 ─── */
#define ZYL_AUTH_DBUS_NAME    "org.zylos.AuthService"
#define ZYL_AUTH_DBUS_PATH    "/org/zylos/AuthService"
#define ZYL_AUTH_DBUS_IFACE   "org.zylos.AuthService"

/* libfprint 백엔드 D-Bus (fprintd) */
#define FPRINT_DBUS_NAME      "net.reactivated.Fprint"
#define FPRINT_DBUS_MANAGER   "/net/reactivated/Fprint/Manager"
#define FPRINT_IFACE_MANAGER  "net.reactivated.Fprint.Manager"
#define FPRINT_IFACE_DEVICE   "net.reactivated.Fprint.Device"

#endif /* ZYL_AUTH_H */
