/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 전화 서비스 인터페이스 — 음성통화, SMS, SIM/네트워크 상태
 * 수행범위: ModemManager 연동, 통화 제어, SMS 송수신, 신호 강도 조회
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 전화/메시징 관련 인터페이스만 노출
 *
 * 실기기: ModemManager D-Bus API (org.freedesktop.ModemManager1)
 * 에뮬레이터: JS로 시뮬레이션
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_TELEPHONY_H
#define ZYL_TELEPHONY_H

#include <stdbool.h>
#include <stdint.h>

/* ─── 통화 상태 ─── */
typedef enum {
    ZYL_CALL_STATE_IDLE,         /* 통화 없음 */
    ZYL_CALL_STATE_RINGING,      /* 수신 벨 울림 */
    ZYL_CALL_STATE_ACTIVE,       /* 통화 중 */
    ZYL_CALL_STATE_HELD,         /* 보류 */
    ZYL_CALL_STATE_DIALING,      /* 발신 중 */
} ZylCallState;

/* ─── 네트워크 타입 ─── */
typedef enum {
    ZYL_NETWORK_TYPE_NONE,
    ZYL_NETWORK_TYPE_2G,         /* GSM/GPRS/EDGE */
    ZYL_NETWORK_TYPE_3G,         /* UMTS/HSPA */
    ZYL_NETWORK_TYPE_4G,         /* LTE */
    ZYL_NETWORK_TYPE_5G,         /* NR */
} ZylNetworkType;

/* ─── 전화 상태 정보 ─── */
typedef struct {
    bool sim_present;            /* SIM 카드 존재 여부 */
    char operator_name[64];      /* 통신사 이름 */
    ZylNetworkType network_type; /* 접속 네트워크 타입 */
    int signal_strength;         /* 0-4 bars */
    char imei[20];               /* 단말기 IMEI */
    char phone_number[20];       /* 자기 전화번호 */
} ZylTelephonyState;

/* ─── 통화 상태 콜백 ─── */
typedef void (*zyl_call_state_fn)(ZylCallState old_state,
                                   ZylCallState new_state,
                                   const char *number,
                                   void *user_data);

/* ─── SMS 수신 콜백 ─── */
typedef void (*zyl_sms_received_fn)(const char *sender,
                                     const char *body,
                                     uint64_t timestamp_ms,
                                     void *user_data);

/* ─── 서비스 인터페이스 ─── */
typedef struct ZylTelephonyService ZylTelephonyService;

/* 서비스 생성/해제 */
ZylTelephonyService *zyl_telephony_create(void);
void                 zyl_telephony_destroy(ZylTelephonyService *svc);

/* 상태 조회 */
int  zyl_telephony_get_state(const ZylTelephonyService *svc,
                              ZylTelephonyState *out);
ZylCallState zyl_telephony_get_call_state(const ZylTelephonyService *svc);

/* 통화 제어 */
int  zyl_telephony_dial(ZylTelephonyService *svc, const char *number);
int  zyl_telephony_answer(ZylTelephonyService *svc);
int  zyl_telephony_hangup(ZylTelephonyService *svc);

/* SMS 발송 */
int  zyl_telephony_send_sms(ZylTelephonyService *svc,
                             const char *number, const char *body);

/* 콜백 등록 */
void zyl_telephony_on_call_state(ZylTelephonyService *svc,
                                  zyl_call_state_fn cb, void *data);
void zyl_telephony_on_sms_received(ZylTelephonyService *svc,
                                    zyl_sms_received_fn cb, void *data);

/* D-Bus 상수 */
#define ZYL_TELEPHONY_DBUS_NAME "org.zylos.Telephony"
#define ZYL_TELEPHONY_DBUS_PATH "/org/zylos/Telephony"

/* ModemManager D-Bus 상수 */
#define MM_DBUS_NAME     "org.freedesktop.ModemManager1"
#define MM_DBUS_PATH     "/org/freedesktop/ModemManager1"
#define MM_MODEM_IFACE   "org.freedesktop.ModemManager1.Modem"
#define MM_VOICE_IFACE   "org.freedesktop.ModemManager1.Modem.Voice"
#define MM_MSG_IFACE     "org.freedesktop.ModemManager1.Modem.Messaging"
#define MM_SIM_IFACE     "org.freedesktop.ModemManager1.Sim"
#define MM_SIGNAL_IFACE  "org.freedesktop.ModemManager1.Modem.Signal"

#endif /* ZYL_TELEPHONY_H */
