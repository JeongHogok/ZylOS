/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: USB/MTP 관리 서비스 인터페이스 — USB 모드 전환, 연결 감지
 * 수행범위: USB 가젯 모드(MTP/PTP/ADB/테더링) 설정, 연결 상태 감시
 * 의존방향: stdbool.h
 * SOLID: ISP — USB 관리 관련 인터페이스만 노출
 *
 * 실기기: configfs USB gadget + sysfs 감시
 * 에뮬레이터: D-Bus 인터페이스로 시뮬레이션
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_USB_H
#define ZYL_USB_H

#include <stdbool.h>

/* ─── USB 모드 ─── */
typedef enum {
    ZYL_USB_MODE_NONE,
    ZYL_USB_MODE_CHARGING,
    ZYL_USB_MODE_MTP,        /* File transfer */
    ZYL_USB_MODE_PTP,        /* Photo transfer */
    ZYL_USB_MODE_ADB,        /* Debug bridge */
    ZYL_USB_MODE_TETHERING,  /* USB tethering */
} ZylUsbMode;

/* ─── 서비스 인터페이스 ─── */
typedef struct ZylUsbService ZylUsbService;

/* 서비스 생성/해제 */
ZylUsbService *zyl_usb_create(void);
void           zyl_usb_destroy(ZylUsbService *svc);

/* 모드 설정/조회 */
int        zyl_usb_set_mode(ZylUsbService *svc, ZylUsbMode mode);
ZylUsbMode zyl_usb_get_mode(const ZylUsbService *svc);
bool       zyl_usb_is_connected(const ZylUsbService *svc);

/* D-Bus 상수 */
#define ZYL_USB_DBUS_NAME "org.zylos.UsbManager"
#define ZYL_USB_DBUS_PATH "/org/zylos/UsbManager"

#endif /* ZYL_USB_H */
