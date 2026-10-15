/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Platform Layer - Emulator
 *
 * 역할: ZylOS HAL 에뮬레이터 — 순수 인메모리 가상 하드웨어 구현
 * 수행범위: 가상 Wi-Fi/BT/디스플레이/배터리/저장공간, 오디오는 볼륨 상태만.
 *          없는 하드웨어(부트로더, 통화 경로, 진동 모터)는 없다고 답한다.
 *          실제 하드웨어, D-Bus, sysfs, gio 완전 불필요.
 *          Linux과 macOS 모두 빌드 가능.
 * 의존방향: hal.h (Domain Interface)만 의존. 외부 라이브러리 없음.
 * SOLID: DIP — hal.h 의 포트만 구현 (모듈 접근자).
 *        SRP — 모듈별 독립된 상태 구조체와 함수군.
 *        ISP — 각 HAL 인터페이스를 독립 구현 블록으로 분리.
 * ────────────────────────────────────────────────────────── */

/* nanosleep, pthread 등 POSIX 함수 노출 */
#define _POSIX_C_SOURCE 200809L

#include "hal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

/* ──────────────────────────────────────────────────────────
   내부 유틸리티
   ────────────────────────────────────────────────────────── */

/* base_dbm ± 5dBm 무작위 지터 */
static int dbm_jitter(int base_dbm) {
    return base_dbm + (rand() % 11) - 5;
}


/* ══════════════════════════════════════════════════════════
   WiFi 에뮬레이터
   ══════════════════════════════════════════════════════════ */

#define EMU_WIFI_AP_COUNT 5

typedef struct {
    const char *ssid;
    const char *bssid;
    int         base_dbm;
    const char *security;
    int         freq_mhz;
} EmuAp;

static const EmuAp k_wifi_aps[EMU_WIFI_AP_COUNT] = {
    { "ZylOS-Home",      "AA:BB:CC:DD:EE:01", -45, "WPA2", 2437 },
    { "CoffeeShop-Free", "AA:BB:CC:DD:EE:02", -65, "Open", 2412 },
    { "Office-5G",       "AA:BB:CC:DD:EE:03", -35, "WPA3", 5180 },
    { "Neighbor-Net",    "AA:BB:CC:DD:EE:04", -75, "WPA2", 2462 },
    { "IoT-Network",     "AA:BB:CC:DD:EE:05", -80, "WPA2", 2452 },
};

static bool g_wifi_enabled      = true;
static int  g_wifi_connected_idx = -1;       /* k_wifi_aps 인덱스, -1=미연결 */

static bool emu_wifi_present(void) { return true; }
static bool emu_wifi_get_enabled(void) { return g_wifi_enabled; }
static bool emu_wifi_set_enabled(bool on) {
    g_wifi_enabled = on;
    if (!on) g_wifi_connected_idx = -1;
    return true;
}
static bool emu_wifi_scan(void) { return g_wifi_enabled; }

static int emu_wifi_list_networks(ZylWifiNetwork *out, int max) {
    if (!g_wifi_enabled) return 0;
    int n = 0;
    for (int i = 0; i < EMU_WIFI_AP_COUNT && n < max; i++, n++) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", k_wifi_aps[i].ssid);
        snprintf(out[n].bssid, sizeof(out[n].bssid), "%s", k_wifi_aps[i].bssid);
        snprintf(out[n].security, sizeof(out[n].security), "%s", k_wifi_aps[i].security);
        out[n].signal_dbm = dbm_jitter(k_wifi_aps[i].base_dbm);
        out[n].connected  = g_wifi_connected_idx == i;
    }
    return n;
}

static bool emu_wifi_connect(const char *ssid, const char *passphrase) {
    if (!ssid || !g_wifi_enabled) return false;
    for (int i = 0; i < EMU_WIFI_AP_COUNT; i++) {
        if (strcmp(k_wifi_aps[i].ssid, ssid) != 0) continue;
        bool open = strcmp(k_wifi_aps[i].security, "Open") == 0;
        if (!open && (!passphrase || strlen(passphrase) < 8)) return false;
        g_wifi_connected_idx = i;
        return true;
    }
    return false;
}

static bool emu_wifi_disconnect(void) { g_wifi_connected_idx = -1; return true; }

/* 가상 무선 장치는 AP 모드를 갖지 않는다 */
static bool emu_wifi_hotspot_supported(void) { return false; }
static bool emu_wifi_hotspot_start(const char *s, const char *p) { (void)s; (void)p; return false; }
static bool emu_wifi_hotspot_stop(void) { return false; }
static bool emu_wifi_hotspot_get(ZylHotspotState *out) { memset(out, 0, sizeof(*out)); return true; }

static const ZylWifiHal g_wifi_hal = {
    .present           = emu_wifi_present,
    .get_enabled       = emu_wifi_get_enabled,
    .set_enabled       = emu_wifi_set_enabled,
    .scan              = emu_wifi_scan,
    .list_networks     = emu_wifi_list_networks,
    .connect           = emu_wifi_connect,
    .disconnect        = emu_wifi_disconnect,
    .hotspot_supported = emu_wifi_hotspot_supported,
    .hotspot_start     = emu_wifi_hotspot_start,
    .hotspot_stop      = emu_wifi_hotspot_stop,
    .hotspot_get       = emu_wifi_hotspot_get,
};


/* ══════════════════════════════════════════════════════════
   Bluetooth 에뮬레이터
   ══════════════════════════════════════════════════════════ */

#define EMU_BT_DEV_COUNT 3

typedef struct {
    char name[64];
    char address[18];
    char type[16];
    bool paired;
    bool connected;
    int  battery_percent; /* -1 = 미지원 */
} EmuBtDevice;

/* 사전 구성된 가상 디바이스 목록 */
static EmuBtDevice g_bt_devices[EMU_BT_DEV_COUNT] = {
    { "ZylOS Earbuds", "11:22:33:44:55:01", "audio", true,  true,  82 },
    { "BT Keyboard",   "11:22:33:44:55:02", "input", true,  false, 55 },
    { "Smart Speaker", "11:22:33:44:55:03", "audio", false, false, -1 },
};

static bool g_bt_enabled = true;

static int bt_find_device(const char *address) {
    if (!address) return -1;
    for (int i = 0; i < EMU_BT_DEV_COUNT; i++)
        if (strcmp(g_bt_devices[i].address, address) == 0) return i;
    return -1;
}

static bool emu_bt_present(void) { return true; }
static bool emu_bt_get_enabled(void) { return g_bt_enabled; }
static bool emu_bt_set_enabled(bool on) {
    g_bt_enabled = on;
    if (!on) for (int i = 0; i < EMU_BT_DEV_COUNT; i++) g_bt_devices[i].connected = false;
    return true;
}
static bool emu_bt_start_scan(void) { return g_bt_enabled; }
static bool emu_bt_stop_scan(void) { return true; }

static int emu_bt_list_devices(ZylBtDevice *out, int max) {
    if (!g_bt_enabled) return 0;
    int n = 0;
    for (int i = 0; i < EMU_BT_DEV_COUNT && n < max; i++, n++) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].name, sizeof(out[n].name), "%s", g_bt_devices[i].name);
        snprintf(out[n].address, sizeof(out[n].address), "%s", g_bt_devices[i].address);
        snprintf(out[n].type, sizeof(out[n].type), "%s", g_bt_devices[i].type);
        out[n].paired    = g_bt_devices[i].paired;
        out[n].connected = g_bt_devices[i].connected;
    }
    return n;
}

static bool emu_bt_pair(const char *address) {
    int i = bt_find_device(address);
    if (i < 0 || !g_bt_enabled) return false;
    g_bt_devices[i].paired = true;
    return true;
}
static bool emu_bt_connect(const char *address) {
    int i = bt_find_device(address);
    if (i < 0 || !g_bt_enabled || !g_bt_devices[i].paired) return false;
    g_bt_devices[i].connected = true;
    return true;
}
static bool emu_bt_disconnect(const char *address) {
    int i = bt_find_device(address);
    if (i < 0) return false;
    g_bt_devices[i].connected = false;
    return true;
}
static bool emu_bt_remove(const char *address) {
    int i = bt_find_device(address);
    if (i < 0) return false;
    g_bt_devices[i].paired = g_bt_devices[i].connected = false;
    return true;
}

static const ZylBtHal g_bt_hal = {
    .present      = emu_bt_present,
    .get_enabled  = emu_bt_get_enabled,
    .set_enabled  = emu_bt_set_enabled,
    .start_scan   = emu_bt_start_scan,
    .stop_scan    = emu_bt_stop_scan,
    .list_devices = emu_bt_list_devices,
    .pair         = emu_bt_pair,
    .connect      = emu_bt_connect,
    .disconnect   = emu_bt_disconnect,
    .remove       = emu_bt_remove,
};


/* ══════════════════════════════════════════════════════════
   Display 에뮬레이터
   ══════════════════════════════════════════════════════════ */

static int  g_disp_brightness    = 80;
static bool g_disp_auto_bright   = false;
static bool g_disp_dark_mode     = false;
static char g_disp_font_size[16] = "medium";
static int  g_disp_timeout_sec   = 30;

static int  display_init(void) { return 0; }
static void display_shutdown(void) {}

static int display_set_brightness(int percent) {
    if (percent < 0 || percent > 100) return -1;
    g_disp_brightness = percent;
    return 0;
}

static int display_get_brightness(int *out_percent) {
    if (!out_percent) return -1;
    *out_percent = g_disp_brightness;
    return 0;
}

static int display_set_auto_brightness(bool enabled) {
    g_disp_auto_bright = enabled;
    return 0;
}

static int display_set_dark_mode(bool enabled) {
    g_disp_dark_mode = enabled;
    return 0;
}

static int display_set_font_size(const char *size) {
    if (!size) return -1;
    strncpy(g_disp_font_size, size, sizeof(g_disp_font_size) - 1);
    g_disp_font_size[sizeof(g_disp_font_size) - 1] = '\0';
    return 0;
}

static int display_set_screen_timeout(int seconds) {
    if (seconds < 0) return -1;
    g_disp_timeout_sec = seconds;
    return 0;
}

static int display_get_state(ZylDisplayState *out) {
    if (!out) return -1;
    out->brightness_percent = g_disp_brightness;
    out->auto_brightness    = g_disp_auto_bright;
    out->dark_mode          = g_disp_dark_mode;
    out->screen_timeout_sec = g_disp_timeout_sec;
    strncpy(out->font_size, g_disp_font_size, sizeof(out->font_size) - 1);
    out->font_size[sizeof(out->font_size) - 1] = '\0';
    return 0;
}

static ZylDisplayHal g_display_hal = {
    .init                = display_init,
    .shutdown            = display_shutdown,
    .set_brightness      = display_set_brightness,
    .get_brightness      = display_get_brightness,
    .set_auto_brightness = display_set_auto_brightness,
    .set_dark_mode       = display_set_dark_mode,
    .set_font_size       = display_set_font_size,
    .set_screen_timeout  = display_set_screen_timeout,
    .get_state           = display_get_state,
};


/* ══════════════════════════════════════════════════════════
   Audio 에뮬레이터 — 출력 볼륨·음소거 상태만. 소리를 낼 장치도, 이어피스와
   스피커를 나눌 통화 경로도 없다
   ══════════════════════════════════════════════════════════ */

static int  g_audio_volume = 70;
static bool g_audio_muted  = false;
static bool g_audio_mic_muted = false;

static int  emu_audio_get_output_volume(void) { return g_audio_volume; }
static bool emu_audio_set_output_volume(int percent) {
    g_audio_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    return true;
}
static bool emu_audio_set_output_mute(bool mute) { g_audio_muted = mute; return true; }
static bool emu_audio_set_input_mute(bool mute) { g_audio_mic_muted = mute; return true; }
static bool emu_audio_play_tone(int f, int ms, int vol) { (void)f; (void)ms; (void)vol; return false; }
static bool emu_audio_speakerphone_supported(void) { return false; }
static bool emu_audio_set_speakerphone(bool on) { (void)on; return false; }

static const ZylAudioHal g_audio_hal = {
    .get_output_volume      = emu_audio_get_output_volume,
    .set_output_volume      = emu_audio_set_output_volume,
    .set_output_mute        = emu_audio_set_output_mute,
    .set_input_mute         = emu_audio_set_input_mute,
    .play_tone              = emu_audio_play_tone,
    .speakerphone_supported = emu_audio_speakerphone_supported,
    .set_speakerphone       = emu_audio_set_speakerphone,
};

/* 진동 모터 없음 */
static bool emu_vib_present(void) { return false; }
static bool emu_vib_vibrate(const int *p, int n) { (void)p; (void)n; return false; }
static const ZylVibratorHal g_vibrator_hal = { .present = emu_vib_present, .vibrate = emu_vib_vibrate };


/* ══════════════════════════════════════════════════════════
   Battery 에뮬레이터
   백그라운드 스레드로 충전/방전 시뮬레이션
   ══════════════════════════════════════════════════════════ */

static int             g_bat_level    = 73;
static bool            g_bat_charging = true;
static bool            g_bat_usb      = true;
static int             g_bat_temp     = 285;  /* ×10, 28.5°C */
static int             g_bat_voltage  = 4150; /* mV */
static char            g_bat_health[16] = "Good";

static void          (*g_bat_cb)(const ZylBatteryState *, void *) = NULL;
static void           *g_bat_cb_data   = NULL;

static volatile bool   g_bat_running   = false;
static pthread_t        g_bat_thread;
static pthread_mutex_t  g_bat_mutex    = PTHREAD_MUTEX_INITIALIZER;

static void bat_fire_callback_locked(void) {
    /* 호출 전에 mutex 보유 중이어야 함 */
    if (!g_bat_cb) return;
    ZylBatteryState s;
    s.level_percent   = g_bat_level;
    s.charging        = g_bat_charging;
    s.usb_connected   = g_bat_usb;
    s.temperature_c10 = g_bat_temp;
    s.voltage_mv      = g_bat_voltage;
    strncpy(s.health, g_bat_health, sizeof(s.health) - 1);
    s.health[sizeof(s.health) - 1] = '\0';
    g_bat_cb(&s, g_bat_cb_data);
}

static void *battery_sim_thread(void *arg) {
    (void)arg;
    time_t last_tick = time(NULL);
    while (1) {
        /* 1초 간격으로 폴링 */
        struct timespec ts;
        ts.tv_sec  = 1;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);

        pthread_mutex_lock(&g_bat_mutex);
        if (!g_bat_running) {
            pthread_mutex_unlock(&g_bat_mutex);
            break;
        }
        time_t now     = time(NULL);
        double elapsed = difftime(now, last_tick);
        bool   changed = false;

        if (g_bat_charging && elapsed >= 30.0) {
            /* 충전: 30초마다 +1% (100% 상한) */
            if (g_bat_level < 100) { g_bat_level++; changed = true; }
            last_tick = now;
        } else if (!g_bat_charging && elapsed >= 60.0) {
            /* 방전: 60초마다 -1% (0% 하한) */
            if (g_bat_level > 0)   { g_bat_level--; changed = true; }
            last_tick = now;
        }
        if (changed) bat_fire_callback_locked();
        pthread_mutex_unlock(&g_bat_mutex);
    }
    return NULL;
}

static int battery_init(void) {
    g_bat_running = true;
    int rc = pthread_create(&g_bat_thread, NULL, battery_sim_thread, NULL);
    return (rc == 0) ? 0 : -1;
}

static void battery_shutdown(void) {
    pthread_mutex_lock(&g_bat_mutex);
    g_bat_running = false;
    pthread_mutex_unlock(&g_bat_mutex);
    pthread_join(g_bat_thread, NULL);
}

static int battery_get_state(ZylBatteryState *out) {
    if (!out) return -1;
    pthread_mutex_lock(&g_bat_mutex);
    out->level_percent   = g_bat_level;
    out->charging        = g_bat_charging;
    out->usb_connected   = g_bat_usb;
    out->temperature_c10 = g_bat_temp;
    out->voltage_mv      = g_bat_voltage;
    strncpy(out->health, g_bat_health, sizeof(out->health) - 1);
    out->health[sizeof(out->health) - 1] = '\0';
    pthread_mutex_unlock(&g_bat_mutex);
    return 0;
}

static int battery_set_change_callback(
    void (*cb)(const ZylBatteryState *state, void *data), void *user_data)
{
    pthread_mutex_lock(&g_bat_mutex);
    g_bat_cb      = cb;
    g_bat_cb_data = user_data;
    pthread_mutex_unlock(&g_bat_mutex);
    return 0;
}

static ZylBatteryHal g_battery_hal = {
    .init                = battery_init,
    .shutdown            = battery_shutdown,
    .get_state           = battery_get_state,
    .set_change_callback = battery_set_change_callback,
};


/* ══════════════════════════════════════════════════════════
   Storage 에뮬레이터
   ══════════════════════════════════════════════════════════ */

#define EMU_GB_BYTES ((uint64_t)1024 * 1024 * 1024)

static int  storage_init(void) { return 0; }
static void storage_shutdown(void) {}

static int storage_get_state(ZylStorageState *out) {
    if (!out) return -1;
    out->total_bytes     = (uint64_t)64 * EMU_GB_BYTES;
    out->used_bytes      = (uint64_t)23 * EMU_GB_BYTES;
    out->available_bytes = out->total_bytes - out->used_bytes;
    return 0;
}

static ZylStorageHal g_storage_hal = {
    .init      = storage_init,
    .shutdown  = storage_shutdown,
    .get_state = storage_get_state,
};


/* ══════════════════════════════════════════════════════════
   모듈 접근자 — 상태를 쓰는 모듈은 처음 쓸 때 init
   ══════════════════════════════════════════════════════════ */

const ZylAudioHal    *zyl_hal_audio(void) { return &g_audio_hal; }
const ZylVibratorHal *zyl_hal_vibrator(void) { return &g_vibrator_hal; }
const ZylWifiHal *zyl_hal_wifi(void) { return &g_wifi_hal; }
const ZylBtHal *zyl_hal_bt(void) { return &g_bt_hal; }
ZylDisplayHal *zyl_hal_display(void) { return &g_display_hal; }
ZylBatteryHal *zyl_hal_battery(void) { return &g_battery_hal; }
ZylStorageHal *zyl_hal_storage(void) { return &g_storage_hal; }

/* ──────────────────────────────────────────────────────────
   Boot — 가상 기기에는 A/B 부트로더가 없다: 업데이트·잠금 해제 비활성
   ────────────────────────────────────────────────────────── */

static bool        emu_boot_present(void) { return false; }
static char       *emu_boot_env_get(const char *name) { (void)name; return NULL; }
static bool        emu_boot_env_set(const char *name, const char *value) { (void)name; (void)value; return false; }
static const char *emu_boot_slot(char slot) { (void)slot; return ""; }
static const char *emu_boot_dir(void) { return ""; }

static const ZylBootHal g_boot_hal = {
    .present     = emu_boot_present,
    .env_get     = emu_boot_env_get,
    .env_set     = emu_boot_env_set,
    .slot_rootfs = emu_boot_slot,
    .slot_verity = emu_boot_slot,
    .boot_dir    = emu_boot_dir,
};

const ZylBootHal *zyl_hal_boot(void) { return &g_boot_hal; }
