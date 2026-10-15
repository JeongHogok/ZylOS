/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 블루투스 서비스 인터페이스
 * 수행범위: 서비스 생성·해제 (장치 접근은 Bluetooth HAL)
 * 의존방향: stdbool.h
 * SOLID: ISP — 서비스 수명만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_BLUETOOTH_H
#define ZYL_BLUETOOTH_H

#include <stdbool.h>

typedef struct ZylBluetoothService ZylBluetoothService;

ZylBluetoothService *zyl_bt_create(void);
void                 zyl_bt_destroy(ZylBluetoothService *svc);

#define ZYL_BT_DBUS_NAME "org.zylos.BluetoothService"
#define ZYL_BT_DBUS_PATH "/org/zylos/BluetoothService"

#endif /* ZYL_BLUETOOTH_H */
