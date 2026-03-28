/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 센서 서비스 인터페이스 — 가속도계, 자이로, 근접, 조도, 자기장
 * 수행범위: IIO 센서 장치 탐색, 폴링, 이벤트 전달, D-Bus 시그널
 * 의존방향: stdbool.h, stdint.h
 * SOLID: ISP — 센서 데이터 수집/전달 인터페이스만 노출
 *
 * 실기기: Linux IIO subsystem (/sys/bus/iio/devices/)
 * 에뮬레이터: JS로 시뮬레이션
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_SENSORS_H
#define ZYL_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

/* ─── 센서 타입 ─── */
typedef enum {
    ZYL_SENSOR_ACCELEROMETER,    /* 가속도계 (m/s^2) */
    ZYL_SENSOR_GYROSCOPE,        /* 자이로스코프 (rad/s) */
    ZYL_SENSOR_PROXIMITY,        /* 근접 센서 (near/far + cm) */
    ZYL_SENSOR_LIGHT,            /* 조도 센서 (lux) */
    ZYL_SENSOR_MAGNETOMETER,     /* 자기장/나침반 (heading deg) */
} ZylSensorType;

/* ─── 센서 이벤트 ─── */
typedef struct {
    ZylSensorType type;
    union {
        struct { float x, y, z; } accel;     /* m/s^2 */
        struct { float x, y, z; } gyro;      /* rad/s */
        struct { bool near; float distance_cm; } proximity;
        struct { float lux; } light;
        struct { float heading_deg; } compass;
    } data;
    uint64_t timestamp_ns;       /* CLOCK_MONOTONIC 나노초 */
} ZylSensorEvent;

/* ─── 콜백 ─── */
typedef void (*zyl_sensor_callback_fn)(const ZylSensorEvent *event,
                                        void *user_data);

/* ─── 서비스 인터페이스 ─── */
typedef struct ZylSensorService ZylSensorService;

/* 서비스 생성/해제 */
ZylSensorService *zyl_sensor_create(void);
void              zyl_sensor_destroy(ZylSensorService *svc);

/* 리스너 등록: 지정 타입의 센서를 rate_hz 주기로 폴링하여 콜백 호출 */
int  zyl_sensor_register_listener(ZylSensorService *svc,
                                   ZylSensorType type,
                                   float rate_hz,
                                   zyl_sensor_callback_fn cb,
                                   void *user_data);

/* 리스너 해제 */
void zyl_sensor_unregister_listener(ZylSensorService *svc,
                                     ZylSensorType type,
                                     zyl_sensor_callback_fn cb);

/* 최신 값 조회 (폴링 없이) */
int  zyl_sensor_get_latest(ZylSensorService *svc,
                            ZylSensorType type,
                            ZylSensorEvent *out);

/* D-Bus 상수 */
#define ZYL_SENSOR_DBUS_NAME "org.zylos.SensorService"
#define ZYL_SENSOR_DBUS_PATH "/org/zylos/SensorService"

#endif /* ZYL_SENSORS_H */
