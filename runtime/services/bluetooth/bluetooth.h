/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: Bluetooth 서비스 인터페이스 — BlueZ 기반 스캔/페어링/연결
 * 수행범위: 디바이스 검색, 페어링, 프로필 연결, 상태 조회
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — Bluetooth 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_BLUETOOTH_H
#define ZYL_BLUETOOTH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *address;
    char *name;
    char *device_type;
    bool  paired;
    bool  connected;
    int   rssi;
} ZylBtDevice;

typedef struct ZylBluetoothService ZylBluetoothService;

ZylBluetoothService *zyl_bt_create(void);
void                  zyl_bt_destroy(ZylBluetoothService *svc);

int  zyl_bt_start_scan(ZylBluetoothService *svc, int timeout_sec);
void zyl_bt_stop_scan(ZylBluetoothService *svc);
int  zyl_bt_get_devices(ZylBluetoothService *svc, ZylBtDevice **out,
                         int *count);
int  zyl_bt_pair(ZylBluetoothService *svc, const char *address);
int  zyl_bt_connect(ZylBluetoothService *svc, const char *address);
int  zyl_bt_disconnect(ZylBluetoothService *svc, const char *address);
int  zyl_bt_remove(ZylBluetoothService *svc, const char *address);
bool zyl_bt_is_enabled(ZylBluetoothService *svc);
void zyl_bt_set_enabled(ZylBluetoothService *svc, bool enabled);
void zyl_bt_device_free(ZylBtDevice *devices, int count);

#define ZYL_BT_DBUS_NAME "org.zylos.BluetoothService"
#define ZYL_BT_DBUS_PATH "/org/zylos/BluetoothService"

#endif /* ZYL_BLUETOOTH_H */
