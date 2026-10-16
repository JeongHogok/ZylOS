/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: Hardware Abstraction Layer 포트 — 서비스가 하드웨어에 닿는 유일한 길
 * 수행범위: 부트로더, 오디오(통화 경로 포함), 진동, Wi-Fi, 블루투스, 디스플레이,
 *           배터리, 저장공간, 센서, 하드웨어 키, 카메라.
 *           각 모듈은 그 하드웨어가 이 보드에 있는지도 답한다 —
 *           없으면 기능은 남고 비활성으로 보인다
 * 의존방향: stdbool.h, stddef.h, stdint.h
 * SOLID: DIP — 서비스는 이 인터페이스에만 의존. ISP — 모듈별 접근자
 *
 * 구현은 보드 계약(board/<id>/meson.build 의 board_hal_backend)이 고른다:
 *   linux    — 커널 드라이버 + PipeWire/NetworkManager/BlueZ/sysfs (hal_<모듈>.c)
 *   emulator — VM·개발 호스트용 가상 하드웨어 (hal_emulator.c)
 * ────────────────────────────────────────────────────────── */

#ifndef ZYL_HAL_H
#define ZYL_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════
   Wi-Fi HAL — 무선 연결과 핫스팟
   실기기: NetworkManager (nmcli). NM 이 wpa_supplicant 를 관리하므로 직접 다루지 않는다
   에뮬레이터: 가상 AP, 핫스팟 없음
   ═══════════════════════════════════════════════════════ */

typedef struct {
    char    ssid[64];
    char    security[32];     /* "WPA2", "WPA2 WPA3", "Open" */
    char    bssid[24];
    int     signal_dbm;       /* -100 ~ -30 */
    bool    connected;
} ZylWifiNetwork;

typedef struct {
    bool    active;
    char    ssid[64];
    char    passphrase[64];
} ZylHotspotState;

typedef struct {
    bool  (*present)(void);                        /* a Wi-Fi device exists */
    bool  (*get_enabled)(void);
    bool  (*set_enabled)(bool on);
    bool  (*scan)(void);
    /* One entry per network (strongest access point), hidden ones left out.
       Returns the count, -1 when the list cannot be read */
    int   (*list_networks)(ZylWifiNetwork *out, int max);
    bool  (*connect)(const char *ssid, const char *passphrase);
    bool  (*disconnect)(void);
    /* Hotspot: the Wi-Fi device can be an access point */
    bool  (*hotspot_supported)(void);
    /* Shares the uplink over IPv4 and IPv6 */
    bool  (*hotspot_start)(const char *ssid, const char *passphrase);
    bool  (*hotspot_stop)(void);
    bool  (*hotspot_get)(ZylHotspotState *out);
} ZylWifiHal;

/* ═══════════════════════════════════════════════════════
   Bluetooth HAL
   실기기: BlueZ (system bus, org.bluez Adapter1/Device1)
   에뮬레이터: 가상 기기
   ═══════════════════════════════════════════════════════ */

typedef struct {
    char    name[64];         /* the address when the device sends none */
    char    address[18];
    char    type[32];         /* BlueZ Icon: "audio-headphones", "phone", ... */
    bool    paired;
    bool    connected;
    int     rssi;             /* dBm, 0 when unknown */
} ZylBtDevice;

typedef struct {
    bool  (*present)(void);                        /* an adapter exists */
    bool  (*get_enabled)(void);                    /* powered */
    bool  (*set_enabled)(bool on);
    bool  (*start_scan)(void);
    bool  (*stop_scan)(void);
    /* Known and discovered devices; returns the count, -1 when unreadable */
    int   (*list_devices)(ZylBtDevice *out, int max);
    bool  (*pair)(const char *address);
    bool  (*connect)(const char *address);
    bool  (*disconnect)(const char *address);
    bool  (*remove)(const char *address);
} ZylBtHal;

/* ═══════════════════════════════════════════════════════
   Display HAL — 출력(DRM)과 백라이트
   실기기: /sys/class/drm (커넥터·모드), /sys/class/backlight
   에뮬레이터: 가상 출력 하나, 백라이트 없음 (VM)
   ═══════════════════════════════════════════════════════ */

#define ZYL_HAL_MAX_MODES 32

typedef struct {
    int     width, height, refresh_hz;
} ZylDisplayModeInfo;

typedef struct {
    char    name[32];                 /* "HDMI-A-1", "DSI-1" */
    bool    connected;
    int     mode_count;
    ZylDisplayModeInfo modes[ZYL_HAL_MAX_MODES];   /* preferred first */
} ZylDisplayOutput;

typedef struct {
    int   (*list_outputs)(ZylDisplayOutput *out, int max);
    bool  (*backlight_present)(void);
    int   (*get_brightness)(void);                 /* 0 ~ 100, -1 without a backlight */
    bool  (*set_brightness)(int percent);
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
   Battery HAL — 배터리와 외부 전원
   실기기: /sys/class/power_supply (Battery / Mains·USB)
   에뮬레이터: 배터리 없음, 외부 전원 (VM)
   ═══════════════════════════════════════════════════════ */

typedef struct {
    bool    present;          /* a battery is fitted; a dev board often has none */
    int     level_percent;    /* 0 ~ 100, -1 unknown */
    bool    charging;
    bool    plugged;          /* running on mains or USB power */
} ZylBatteryState;

typedef struct {
    bool  (*get)(ZylBatteryState *out);
} ZylBatteryHal;

/* ═══════════════════════════════════════════════════════
   Storage HAL — 사용자 데이터 파티션 용량
   실기기·에뮬레이터: statvfs
   ═══════════════════════════════════════════════════════ */

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t available_bytes;
} ZylStorageState;

typedef struct {
    bool  (*get)(ZylStorageState *out);
} ZylStorageHal;

/* ═══════════════════════════════════════════════════════
   Torch HAL — 카메라 플래시 LED 를 손전등으로
   실기기: /sys/class/leds 의 이름에 flash 또는 torch 가 든 LED
   에뮬레이터: 없음
   ═══════════════════════════════════════════════════════ */

typedef struct {
    bool  (*present)(void);
    bool  (*get)(void);
    bool  (*set)(bool on);
} ZylTorchHal;

/* ═══════════════════════════════════════════════════════
   CPU HAL — cpufreq 거버너, 코어 온·오프라인
   실기기: /sys/devices/system/cpu
   에뮬레이터: 조절할 것 없음
   ═══════════════════════════════════════════════════════ */

typedef struct {
    int   (*core_count)(void);
    bool  (*set_core_online)(int core, bool online);   /* cpu0 always stays */
    bool  (*set_governor)(const char *governor);
} ZylCpuHal;

/* ═══════════════════════════════════════════════════════
   Board HAL — 이 기기가 무엇인지
   실기기: 디바이스 트리(model, compatible) 또는 DMI, /proc/meminfo, uname
   에뮬레이터: 가상 기기, 메모리·커널은 VM 실측
   ═══════════════════════════════════════════════════════ */

typedef struct {
    char     model[96];       /* "Banana Pi BPI-F3" */
    char     soc[96];         /* device tree: the SoC's compatible ("spacemit,k1-x"); x86: CPU model */
    uint64_t ram_bytes;
    char     kernel[96];      /* "6.6.36" */
    /* This device's own ID, which development profiles list (device tree
       serial-number — the bootloader sets it from the chip's factory ID).
       "" when the board gives none: no development build can run on it */
    char     device_id[72];
} ZylBoardInfo;

typedef struct {
    bool  (*get)(ZylBoardInfo *out);
} ZylBoardHal;

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
   Sensors HAL — 가속도·자이로·근접·조도·자기장
   실기기: Linux IIO (/sys/bus/iio/devices/iio:deviceN 의 in_<채널>_raw × scale).
           종류마다 그 채널을 가진 장치를 따로 찾는다 — 가속도·자이로가 한 칩이어도 둘 다
   에뮬레이터: 센서 없음 — present() false, 값을 지어내지 않는다
   ═══════════════════════════════════════════════════════ */

typedef enum {
    ZYL_HAL_SENSOR_ACCEL,         /* v[0..2] = x, y, z in m/s² */
    ZYL_HAL_SENSOR_GYRO,          /* v[0..2] = x, y, z in rad/s */
    ZYL_HAL_SENSOR_PROXIMITY,     /* v[0] = the driver's scaled distance */
    ZYL_HAL_SENSOR_LIGHT,         /* v[0] = lux */
    ZYL_HAL_SENSOR_MAGNETIC,      /* v[0..2] = x, y, z in gauss */
    ZYL_HAL_SENSOR_COUNT
} ZylSensorKind;

typedef struct {
    double  v[3];                 /* scaled; axes a kind does not have are 0 */
} ZylSensorSample;

typedef struct {
    /* Looks for this kind of sensor; read() reads the one it found */
    bool  (*present)(ZylSensorKind kind);
    /* One sample; false when it cannot be read (no made-up 0) */
    bool  (*read)(ZylSensorKind kind, ZylSensorSample *out);
} ZylSensorsHal;

/* ═══════════════════════════════════════════════════════
   Input HAL — 하드웨어 키 (전원, 볼륨, 뒤로, 홈, 메뉴)
   실기기: evdev (/dev/input/eventN). 휴대폰은 전원 키·볼륨 키·터치가 각각 다른 장치라
           이 키 중 하나라도 가진 장치를 모두 연다. 터치·센서만 있는 장치는 열지 않는다
   에뮬레이터: 키 장치 없음 — open() -1
   ═══════════════════════════════════════════════════════ */

typedef enum {
    ZYL_HAL_KEY_POWER,
    ZYL_HAL_KEY_VOLUME_UP,
    ZYL_HAL_KEY_VOLUME_DOWN,
    ZYL_HAL_KEY_BACK,
    ZYL_HAL_KEY_HOME,
    ZYL_HAL_KEY_MENU,
} ZylInputKey;

typedef struct {
    /* Opens every device with one of the keys. Returns one fd, readable while
       any of them has input (for the main loop), -1 when there is none */
    int   (*open)(void);
    /* The next key press waiting, without blocking. Releases, autorepeat and
       other events are passed over; a device that went away is dropped */
    bool  (*read_key)(ZylInputKey *key);
    void  (*close)(void);
} ZylInputHal;

/* ═══════════════════════════════════════════════════════
   Camera HAL — 사진 한 장
   실기기: V4L2 캡처 장치 (후면 /dev/video0, 전면 /dev/video1), YUYV 만.
           ISP 를 거치는 SoC 는 보드가 자기 구현을 고른다
           (board/<id>/meson.build 의 board_hal_camera, 기본은 V4L2 — hal_camera.c)
   에뮬레이터: 카메라 없음 — present() false
   ═══════════════════════════════════════════════════════ */

typedef enum {
    ZYL_HAL_LENS_BACK,
    ZYL_HAL_LENS_FRONT,
} ZylLensFacing;

typedef enum {
    ZYL_HAL_PIXEL_YUYV,           /* packed 4:2:2, width × 2 bytes per row, no padding */
} ZylPixelFormat;

typedef struct {
    ZylPixelFormat format;
    uint32_t       width, height;
    void          *data;
    size_t         size;          /* width × height × 2 for YUYV */
} ZylCameraImage;

typedef struct {
    bool  (*present)(ZylLensFacing lens);
    /* One still at the size this camera settles on; release it with free_image */
    bool  (*capture)(ZylLensFacing lens, ZylCameraImage *out);
    void  (*free_image)(ZylCameraImage *image);
} ZylCameraHal;

/* ═══════════════════════════════════════════════════════
   모듈 접근자 — 보드 계약의 board_hal_backend 가 linux / emulator 구현을
   고른다(runtime/hal/meson.build). 서비스는 쓰는 모듈의 접근자만 부른다.
   ═══════════════════════════════════════════════════════ */

const ZylBootHal     *zyl_hal_boot(void);
const ZylAudioHal    *zyl_hal_audio(void);
const ZylVibratorHal *zyl_hal_vibrator(void);
const ZylWifiHal     *zyl_hal_wifi(void);
const ZylBtHal       *zyl_hal_bt(void);
const ZylDisplayHal  *zyl_hal_display(void);
const ZylBatteryHal  *zyl_hal_battery(void);
const ZylStorageHal  *zyl_hal_storage(void);
const ZylTorchHal    *zyl_hal_torch(void);
const ZylCpuHal      *zyl_hal_cpu(void);
const ZylBoardHal    *zyl_hal_board(void);
const ZylSensorsHal  *zyl_hal_sensors(void);
const ZylInputHal    *zyl_hal_input(void);
const ZylCameraHal   *zyl_hal_camera(void);

#endif /* ZYL_HAL_H */
