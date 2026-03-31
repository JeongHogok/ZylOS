/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: NFC 서비스 인터페이스 — 태그 스캔/읽기/쓰기 HAL
 * 수행범위: neard D-Bus 프록시 기반 NFC 태그 스캔, NDEF 읽기/쓰기,
 *          D-Bus org.zylos.NfcService 인터페이스 노출
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — NFC 관련 인터페이스만 노출
 *        DIP — 구체 neard/libnfc 구현에 비의존, 인터페이스로만 참조
 *
 * 실기기: neard D-Bus (org.neard) 프록시
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_NFC_H
#define ZYL_NFC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ─── NFC 결과 코드 ─── */
typedef enum {
    ZYL_NFC_OK              =  0,
    ZYL_NFC_ERR_GENERAL     = -1,
    ZYL_NFC_ERR_NO_ADAPTER  = -2,   /* NFC 어댑터 없음 */
    ZYL_NFC_ERR_NO_TAG      = -3,   /* 태그 미감지 */
    ZYL_NFC_ERR_TIMEOUT     = -4,   /* 스캔 타임아웃 */
    ZYL_NFC_ERR_READ        = -5,   /* 읽기 실패 */
    ZYL_NFC_ERR_WRITE       = -6,   /* 쓰기 실패 */
    ZYL_NFC_ERR_DBUS        = -7,   /* D-Bus 통신 오류 */
    ZYL_NFC_ERR_NOT_READY   = -8,   /* 서비스 미준비 */
} ZylNfcResult;

/* ─── NFC 태그 타입 ─── */
typedef enum {
    ZYL_NFC_TAG_UNKNOWN = 0,
    ZYL_NFC_TAG_TYPE1,    /* Topaz */
    ZYL_NFC_TAG_TYPE2,    /* MIFARE Ultralight, NTAG */
    ZYL_NFC_TAG_TYPE3,    /* FeliCa */
    ZYL_NFC_TAG_TYPE4,    /* MIFARE DESFire, ISO-DEP */
    ZYL_NFC_TAG_ISO15693,
} ZylNfcTagType;

/* ─── NDEF 레코드 ─── */
typedef struct {
    char     *type;        /* MIME 타입 또는 RTD ("text/plain", "U"=URI 등) */
    uint8_t  *payload;     /* 페이로드 바이트 */
    size_t    payload_len;
} ZylNdefRecord;

/* ─── NFC 태그 ─── */
typedef struct {
    char         *path;           /* neard 객체 경로 (e.g. /org/neard/nfc0/tag0) */
    ZylNfcTagType type;
    char          uid[32];        /* 태그 UID (hex 문자열) */
    ZylNdefRecord *records;       /* NDEF 레코드 배열 */
    int            record_count;
} ZylNfcTag;

/* ─── 태그 감지 콜백 ─── */
typedef void (*zyl_nfc_tag_detected_fn)(const ZylNfcTag *tag, void *user_data);

/* ─── 서비스 인터페이스 ─── */
typedef struct ZylNfcService ZylNfcService;

/**
 * zyl_nfc_create: NfcService 인스턴스 생성.
 * neard D-Bus 연결 포함.
 * @return 인스턴스 포인터, 실패 시 NULL
 */
ZylNfcService *zyl_nfc_create(void);

/**
 * zyl_nfc_destroy: NfcService 인스턴스 해제.
 */
void zyl_nfc_destroy(ZylNfcService *svc);

/**
 * zyl_nfc_start_scan: NFC 태그 스캔 시작.
 * 태그 감지 시 callback 호출.
 * @param timeout_sec 0=무제한
 * @return ZylNfcResult
 */
ZylNfcResult zyl_nfc_start_scan(ZylNfcService *svc,
                                 int timeout_sec,
                                 zyl_nfc_tag_detected_fn callback,
                                 void *user_data);

/**
 * zyl_nfc_stop_scan: 진행 중인 스캔 중지.
 */
void zyl_nfc_stop_scan(ZylNfcService *svc);

/**
 * zyl_nfc_read_tag: 감지된 태그에서 NDEF 데이터 읽기.
 * @param tag_path neard 태그 객체 경로
 * @param out      결과 ZylNfcTag (caller: zyl_nfc_tag_free로 해제)
 * @return ZylNfcResult
 */
ZylNfcResult zyl_nfc_read_tag(ZylNfcService *svc,
                               const char *tag_path,
                               ZylNfcTag *out);

/**
 * zyl_nfc_write_tag: 태그에 NDEF 레코드 쓰기.
 * @param tag_path neard 태그 객체 경로
 * @param record   쓸 NDEF 레코드
 * @return ZylNfcResult
 */
ZylNfcResult zyl_nfc_write_tag(ZylNfcService *svc,
                                const char *tag_path,
                                const ZylNdefRecord *record);

/**
 * zyl_nfc_tag_free: ZylNfcTag 내부 메모리 해제.
 */
void zyl_nfc_tag_free(ZylNfcTag *tag);

/* ─── D-Bus 상수 ─── */
#define ZYL_NFC_DBUS_NAME   "org.zylos.NfcService"
#define ZYL_NFC_DBUS_PATH   "/org/zylos/NfcService"
#define ZYL_NFC_DBUS_IFACE  "org.zylos.NfcService"

/* neard D-Bus */
#define NEARD_DBUS_NAME       "org.neard"
#define NEARD_DBUS_MANAGER    "/org/neard"
#define NEARD_IFACE_MANAGER   "org.neard.Manager"
#define NEARD_IFACE_ADAPTER   "org.neard.Adapter"
#define NEARD_IFACE_TAG       "org.neard.Tag"
#define NEARD_IFACE_RECORD    "org.neard.Record"

#endif /* ZYL_NFC_H */
