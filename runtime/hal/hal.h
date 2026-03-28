/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: Hardware Abstraction Layer 인터페이스 정의
 * 수행범위: WiFi, BT, 디스플레이, 사운드, 배터리, 센서 추상화
 * 의존방향: stdbool.h, stdint.h
 * SOLID: DIP — 하드웨어 구현이 아닌 추상 인터페이스에 의존.
 *        ISP — 각 하드웨어 모듈별 독립 인터페이스.
 *
 * 이 인터페이스는 두 가지 구현을 가진다:
 *   1. 실기기: Linux 커널 드라이버 + wpa_supplicant/BlueZ/PipeWire
 *   2. 에뮬레이터: 브라우저 Web API (navigator.connection 등)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_HAL_H
#define ZYL_HAL_H

#include <stdbool.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════
   WiFi HAL
   실기기: wpa_supplicant D-Bus API → nl80211
   에뮬레이터: navigator.connection + NetworkInformation API
   ═══════════════════════════════════════════════════════ */

typedef struct {
    char    ssid[64];
    char    bssid[18];        /* MAC address */
    int     signal_dbm;       /* -100 ~ 0 */
    int     signal_percent;   /* 0 ~ 100 */
    char    security[16];     /* "Open", "WPA2", "WPA3", "WEP" */
    bool    connected;
    int     frequency_mhz;    /* 2412, 5180, etc. */
} ZylWifiNetwork;

typedef struct {
    bool    enabled;
    bool    connected;
    char    current_ssid[64];
    char    ip_address[46];   /* IPv4 or IPv6 */
    char    mac_address[18];
    int     link_speed_mbps;
} ZylWifiState;

typedef struct {
    /* 드라이버 구현이 채워야 하는 함수 포인터 */
    int   (*init)(void);
    void  (*shutdown)(void);

    int   (*set_enabled)(bool enabled);
    int   (*get_state)(ZylWifiState *out);

    /* 스캔: 결과 배열과 개수를 반환. 호출자가 해제. */
    int   (*scan)(ZylWifiNetwork **out_list, int *out_count);

    int   (*connect)(const char *ssid, const char *password);
    int   (*disconnect)(void);

    /* 현재 연결의 실시간 신호 강도 */
    int   (*get_signal_strength)(int *out_percent);
} ZylWifiHal;

/* ═══════════════════════════════════════════════════════
   Bluetooth HAL
   실기기: BlueZ D-Bus API
   에뮬레이터: Web Bluetooth API (navigator.bluetooth)
   ═══════════════════════════════════════════════════════ */

typedef struct {
    char    name[64];
    char    address[18];      /* MAC */
    char    type[16];         /* "audio", "input", "phone", "computer" */
    bool    paired;
    bool    connected;
    int     battery_percent;  /* -1 if unknown */
} ZylBtDevice;

typedef struct {
    bool    enabled;
    char    device_name[64];  /* 이 기기의 BT 이름 */
    bool    discoverable;
} ZylBtState;

typedef struct {
    int   (*init)(void);
    void  (*shutdown)(void);

    int   (*set_enabled)(bool enabled);
    int   (*get_state)(ZylBtState *out);

    int   (*scan)(ZylBtDevice **out_list, int *out_count);

    int   (*pair)(const char *address);
    int   (*unpair)(const char *address);
    int   (*connect_device)(const char *address);
    int   (*disconnect_device)(const char *address);

    int   (*get_paired_devices)(ZylBtDevice **out_list, int *out_count);
} ZylBtHal;

/* ═══════════════════════════════════════════════════════
   Display HAL
   실기기: sysfs backlight + DRM/KMS
   에뮬레이터: CSS filter brightness
   ═══════════════════════════════════════════════════════ */

typedef struct {
    int     brightness_percent; /* 0 ~ 100 */
    bool    auto_brightness;
    bool    dark_mode;
    char    font_size[16];      /* "small", "medium", "large" */
    int     screen_timeout_sec; /* 자동 잠금 시간 */
} ZylDisplayState;

typedef struct {
    int   (*init)(void);
    void  (*shutdown)(void);

    int   (*set_brightness)(int percent);
    int   (*get_brightness)(int *out_percent);

    int   (*set_auto_brightness)(bool enabled);
    int   (*set_dark_mode)(bool enabled);
    int   (*set_font_size)(const char *size);
    int   (*set_screen_timeout)(int seconds);

    int   (*get_state)(ZylDisplayState *out);
} ZylDisplayHal;

/* ═══════════════════════════════════════════════════════
   Audio HAL
   실기기: PipeWire / ALSA
   에뮬레이터: Web Audio API
   ═══════════════════════════════════════════════════════ */

typedef struct {
    int     media_volume;       /* 0 ~ 100 */
    int     notification_volume;
    int     alarm_volume;
    int     call_volume;
    bool    vibration;
    bool    silent_mode;
} ZylAudioState;

typedef struct {
    int   (*init)(void);
    void  (*shutdown)(void);

    int   (*set_volume)(const char *stream, int percent);
    int   (*get_volume)(const char *stream, int *out_percent);

    int   (*set_vibration)(bool enabled);
    int   (*set_silent_mode)(bool enabled);

    int   (*get_state)(ZylAudioState *out);
} ZylAudioHal;

/* ═══════════════════════════════════════════════════════
   Battery HAL
   실기기: sysfs power_supply
   에뮬레이터: navigator.getBattery()
   ═══════════════════════════════════════════════════════ */

typedef struct {
    int     level_percent;    /* 0 ~ 100 */
    bool    charging;
    bool    usb_connected;
    char    health[16];       /* "Good", "Overheat", "Dead" */
    int     temperature_c10;  /* 온도 × 10 (예: 285 = 28.5°C) */
    int     voltage_mv;
} ZylBatteryState;

typedef struct {
    int   (*init)(void);
    void  (*shutdown)(void);

    int   (*get_state)(ZylBatteryState *out);

    /* 콜백: 배터리 상태 변경 시 호출 */
    int   (*set_change_callback)(void (*cb)(const ZylBatteryState *state, void *data),
                                 void *user_data);
} ZylBatteryHal;

/* ═══════════════════════════════════════════════════════
   Storage HAL
   실기기: statvfs()
   에뮬레이터: navigator.storage.estimate()
   ═══════════════════════════════════════════════════════ */

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t available_bytes;
} ZylStorageState;

typedef struct {
    int   (*init)(void);
    void  (*shutdown)(void);

    int   (*get_state)(ZylStorageState *out);
} ZylStorageHal;

/* ═══════════════════════════════════════════════════════
   HAL Registry — 전체 HAL 모듈을 하나로 묶는 구조체
   ═══════════════════════════════════════════════════════ */

typedef struct {
    ZylWifiHal      *wifi;
    ZylBtHal        *bluetooth;
    ZylDisplayHal   *display;
    ZylAudioHal     *audio;
    ZylBatteryHal   *battery;
    ZylStorageHal   *storage;
} ZylHalRegistry;

/*
 * HAL 초기화 — 플랫폼에 따라 구현 선택
 *   실기기:    zyl_hal_create_linux()
 *   에뮬레이터: zyl_hal_create_emulator()
 */
ZylHalRegistry *zyl_hal_create_linux(void);
ZylHalRegistry *zyl_hal_create_emulator(void);
void            zyl_hal_destroy(ZylHalRegistry *hal);

#endif /* ZYL_HAL_H */
