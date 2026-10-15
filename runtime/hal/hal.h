/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: Hardware Abstraction Layer 포트 — 서비스가 하드웨어에 닿는 유일한 길
 * 수행범위: 부트로더, 오디오(통화 경로 포함), 진동, Wi-Fi, 블루투스, 디스플레이,
 *           배터리, 저장공간. 각 모듈은 그 하드웨어가 이 보드에 있는지도 답한다 —
 *           없으면 기능은 남고 비활성으로 보인다
 * 의존방향: stdbool.h, stdint.h
 * SOLID: DIP — 서비스는 이 인터페이스에만 의존. ISP — 모듈별 접근자
 *
 * 구현은 보드 계약(board/<id>/meson.build 의 board_hal_backend)이 고른다:
 *   linux    — 커널 드라이버 + PipeWire/NetworkManager/BlueZ/sysfs (hal_<모듈>.c)
 *   emulator — VM·개발 호스트용 가상 하드웨어 (hal_emulator.c)
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
   Audio HAL — 출력·마이크와 통화 경로
   실기기: PipeWire (wpctl, pactl, pw-play)
   에뮬레이터: 메모리 상태, 통화 경로 없음
   ═══════════════════════════════════════════════════════ */

typedef struct {
    int   (*get_output_volume)(void);              /* 0 ~ 100, -1 unknown */
    bool  (*set_output_volume)(int percent);
    bool  (*set_output_mute)(bool mute);
    bool  (*set_input_mute)(bool mute);            /* the microphone: a call's uplink */
    /* A short sine tone, not waiting for it to finish */
    bool  (*play_tone)(int freq_hz, int duration_ms, int volume_percent);
    /* Calls: true when the earpiece and the loudspeaker are separate outputs
       the OS can switch between. Without both there is nothing to switch */
    bool  (*speakerphone_supported)(void);
    bool  (*set_speakerphone)(bool on);
} ZylAudioHal;

/* ═══════════════════════════════════════════════════════
   Vibrator HAL
   실기기: 입력 장치의 force-feedback(FF_RUMBLE) — pwm/gpio-vibrator 등
   에뮬레이터: 없음
   ═══════════════════════════════════════════════════════ */

typedef struct {
    bool  (*present)(void);
    /* on, off, on, ... in milliseconds; returns at once */
    bool  (*vibrate)(const int *pattern_ms, int count);
} ZylVibratorHal;

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
   Boot HAL — 부트로더 환경과 A/B 슬롯 배치
   실기기: U-Boot 환경 (fw_printenv/fw_setenv), 파티션은 board/bpi-f3/README.md
   에뮬레이터: 없음 — present() false, 시스템 업데이트·잠금 해제 비활성
   ═══════════════════════════════════════════════════════ */

typedef struct {
    /* A/B boot with a bootloader environment the OS can write */
    bool        (*present)(void);
    /* Caller frees with free(); NULL when unset or there is no environment */
    char       *(*env_get)(const char *name);
    /* value NULL deletes the variable */
    bool        (*env_set)(const char *name, const char *value);
    /* Block devices of slot 'a' or 'b', and where its signed FIT lives */
    const char *(*slot_rootfs)(char slot);
    const char *(*slot_verity)(char slot);
    const char *(*boot_dir)(void);
} ZylBootHal;

/* ═══════════════════════════════════════════════════════
   모듈 접근자 — 보드 계약의 board_hal_backend 가 linux / emulator 구현을
   고른다(runtime/hal/meson.build). 서비스는 쓰는 모듈의 접근자만 부른다.
   ═══════════════════════════════════════════════════════ */

const ZylBootHal     *zyl_hal_boot(void);
const ZylAudioHal    *zyl_hal_audio(void);
const ZylVibratorHal *zyl_hal_vibrator(void);
ZylWifiHal           *zyl_hal_wifi(void);
ZylBtHal             *zyl_hal_bt(void);
ZylDisplayHal        *zyl_hal_display(void);
ZylBatteryHal        *zyl_hal_battery(void);
ZylStorageHal        *zyl_hal_storage(void);

#endif /* ZYL_HAL_H */
