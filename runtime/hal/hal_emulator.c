/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Platform Layer - Emulator
 *
 * 역할: ZylOS HAL 에뮬레이터 — 순수 인메모리 가상 하드웨어 구현
 * 수행범위: WiFi/BT/Display/Audio/Battery/Storage 6개 모듈 에뮬레이션.
 *          실제 하드웨어, D-Bus, sysfs, gio 완전 불필요.
 *          Linux과 macOS 모두 빌드 가능.
 * 의존방향: hal.h (Domain Interface)만 의존. 외부 라이브러리 없음.
 * SOLID: DIP — ZylHalRegistry 추상 인터페이스만 구현.
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

/* -100 ~ 0 dBm → 0 ~ 100% 선형 변환 */
static int dbm_to_percent(int dbm) {
    if (dbm >= -50)  return 100;
    if (dbm <= -100) return 0;
    return 2 * (dbm + 100);
}

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

static bool g_wifi_enabled        = true;
static bool g_wifi_connected       = false;
static int  g_wifi_connected_idx   = -1;       /* k_wifi_aps 인덱스, -1=미연결 */
static char g_wifi_ip[46]          = "";
static char g_wifi_mac[18]         = "00:11:22:33:44:55";
static char g_wifi_ssid[64]        = "";

static int wifi_init(void) {
    srand((unsigned)time(NULL));
    return 0;
}

static void wifi_shutdown(void) {
    g_wifi_connected     = false;
    g_wifi_connected_idx = -1;
    g_wifi_ssid[0]       = '\0';
    g_wifi_ip[0]         = '\0';
}

static int wifi_set_enabled(bool enabled) {
    g_wifi_enabled = enabled;
    if (!enabled) wifi_shutdown();
    return 0;
}

static int wifi_get_state(ZylWifiState *out) {
    if (!out) return -1;
    out->enabled       = g_wifi_enabled;
    out->connected     = g_wifi_connected;
    out->link_speed_mbps = g_wifi_connected ? 150 : 0;
    strncpy(out->current_ssid, g_wifi_ssid,   sizeof(out->current_ssid)   - 1);
    strncpy(out->ip_address,   g_wifi_ip,     sizeof(out->ip_address)     - 1);
    strncpy(out->mac_address,  g_wifi_mac,    sizeof(out->mac_address)    - 1);
    out->current_ssid[sizeof(out->current_ssid)   - 1] = '\0';
    out->ip_address  [sizeof(out->ip_address)     - 1] = '\0';
    out->mac_address [sizeof(out->mac_address)    - 1] = '\0';
    return 0;
}

static int wifi_scan(ZylWifiNetwork **out_list, int *out_count) {
    if (!out_list || !out_count) return -1;
    ZylWifiNetwork *list = malloc(sizeof(ZylWifiNetwork) * EMU_WIFI_AP_COUNT);
    if (!list) return -1;
    for (int i = 0; i < EMU_WIFI_AP_COUNT; i++) {
        strncpy(list[i].ssid,     k_wifi_aps[i].ssid,     sizeof(list[i].ssid)     - 1);
        strncpy(list[i].bssid,    k_wifi_aps[i].bssid,    sizeof(list[i].bssid)    - 1);
        strncpy(list[i].security, k_wifi_aps[i].security, sizeof(list[i].security) - 1);
        list[i].ssid    [sizeof(list[i].ssid)     - 1] = '\0';
        list[i].bssid   [sizeof(list[i].bssid)    - 1] = '\0';
        list[i].security[sizeof(list[i].security) - 1] = '\0';
        list[i].signal_dbm     = dbm_jitter(k_wifi_aps[i].base_dbm);
        list[i].signal_percent = dbm_to_percent(list[i].signal_dbm);
        list[i].frequency_mhz  = k_wifi_aps[i].freq_mhz;
        list[i].connected      = (g_wifi_connected && g_wifi_connected_idx == i);
    }
    *out_list  = list;
    *out_count = EMU_WIFI_AP_COUNT;
    return 0;
}

static int wifi_connect(const char *ssid, const char *password) {
    if (!ssid || !g_wifi_enabled) return -1;
    for (int i = 0; i < EMU_WIFI_AP_COUNT; i++) {
        if (strcmp(k_wifi_aps[i].ssid, ssid) == 0) {
            bool is_open = (strcmp(k_wifi_aps[i].security, "Open") == 0);
            if (!is_open && (!password || password[0] == '\0')) return -1;
            g_wifi_connected     = true;
            g_wifi_connected_idx = i;
            strncpy(g_wifi_ssid, ssid,          sizeof(g_wifi_ssid) - 1);
            strncpy(g_wifi_ip,   "192.168.1.100", sizeof(g_wifi_ip)  - 1);
            g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
            g_wifi_ip  [sizeof(g_wifi_ip)   - 1] = '\0';
            return 0;
        }
    }
    return -1; /* SSID 없음 */
}

static int wifi_disconnect(void) {
    g_wifi_connected     = false;
    g_wifi_connected_idx = -1;
    g_wifi_ssid[0]       = '\0';
    g_wifi_ip[0]         = '\0';
    return 0;
}

static int wifi_get_signal_strength(int *out_percent) {
    if (!out_percent) return -1;
    if (!g_wifi_connected || g_wifi_connected_idx < 0) {
        *out_percent = 0;
        return -1;
    }
    int dbm = dbm_jitter(k_wifi_aps[g_wifi_connected_idx].base_dbm);
    *out_percent = dbm_to_percent(dbm);
    return 0;
}

static ZylWifiHal g_wifi_hal = {
    .init               = wifi_init,
    .shutdown           = wifi_shutdown,
    .set_enabled        = wifi_set_enabled,
    .get_state          = wifi_get_state,
    .scan               = wifi_scan,
    .connect            = wifi_connect,
    .disconnect         = wifi_disconnect,
    .get_signal_strength = wifi_get_signal_strength,
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

static bool g_bt_enabled      = true;
static bool g_bt_discoverable = false;

static int bt_init(void) { return 0; }

static void bt_shutdown(void) {
    for (int i = 0; i < EMU_BT_DEV_COUNT; i++)
        g_bt_devices[i].connected = false;
}

static int bt_set_enabled(bool enabled) {
    g_bt_enabled = enabled;
    if (!enabled) {
        g_bt_discoverable = false;
        bt_shutdown();
    }
    return 0;
}

static int bt_get_state(ZylBtState *out) {
    if (!out) return -1;
    out->enabled      = g_bt_enabled;
    out->discoverable = g_bt_discoverable;
    strncpy(out->device_name, "ZylOS Device", sizeof(out->device_name) - 1);
    out->device_name[sizeof(out->device_name) - 1] = '\0';
    return 0;
}

static int bt_scan(ZylBtDevice **out_list, int *out_count) {
    if (!out_list || !out_count) return -1;
    ZylBtDevice *list = malloc(sizeof(ZylBtDevice) * EMU_BT_DEV_COUNT);
    if (!list) return -1;
    for (int i = 0; i < EMU_BT_DEV_COUNT; i++) {
        strncpy(list[i].name,    g_bt_devices[i].name,    sizeof(list[i].name)    - 1);
        strncpy(list[i].address, g_bt_devices[i].address, sizeof(list[i].address) - 1);
        strncpy(list[i].type,    g_bt_devices[i].type,    sizeof(list[i].type)    - 1);
        list[i].name   [sizeof(list[i].name)    - 1] = '\0';
        list[i].address[sizeof(list[i].address) - 1] = '\0';
        list[i].type   [sizeof(list[i].type)    - 1] = '\0';
        list[i].paired          = g_bt_devices[i].paired;
        list[i].connected       = g_bt_devices[i].connected;
        list[i].battery_percent = g_bt_devices[i].battery_percent;
    }
    *out_list  = list;
    *out_count = EMU_BT_DEV_COUNT;
    return 0;
}

static int bt_find_device(const char *address) {
    if (!address) return -1;
    for (int i = 0; i < EMU_BT_DEV_COUNT; i++) {
        if (strcmp(g_bt_devices[i].address, address) == 0) return i;
    }
    return -1;
}

static int bt_pair(const char *address) {
    int idx = bt_find_device(address);
    if (idx < 0) return -1;
    g_bt_devices[idx].paired = true;
    return 0;
}

static int bt_unpair(const char *address) {
    int idx = bt_find_device(address);
    if (idx < 0) return -1;
    g_bt_devices[idx].paired    = false;
    g_bt_devices[idx].connected = false;
    return 0;
}

static int bt_connect_device(const char *address) {
    int idx = bt_find_device(address);
    if (idx < 0 || !g_bt_devices[idx].paired) return -1;
    g_bt_devices[idx].connected = true;
    return 0;
}

static int bt_disconnect_device(const char *address) {
    int idx = bt_find_device(address);
    if (idx < 0) return -1;
    g_bt_devices[idx].connected = false;
    return 0;
}

static int bt_get_paired_devices(ZylBtDevice **out_list, int *out_count) {
    if (!out_list || !out_count) return -1;
    int count = 0;
    for (int i = 0; i < EMU_BT_DEV_COUNT; i++)
        if (g_bt_devices[i].paired) count++;

    ZylBtDevice *list = malloc(sizeof(ZylBtDevice) * (count > 0 ? count : 1));
    if (!list) return -1;
    int j = 0;
    for (int i = 0; i < EMU_BT_DEV_COUNT; i++) {
        if (!g_bt_devices[i].paired) continue;
        strncpy(list[j].name,    g_bt_devices[i].name,    sizeof(list[j].name)    - 1);
        strncpy(list[j].address, g_bt_devices[i].address, sizeof(list[j].address) - 1);
        strncpy(list[j].type,    g_bt_devices[i].type,    sizeof(list[j].type)    - 1);
        list[j].name   [sizeof(list[j].name)    - 1] = '\0';
        list[j].address[sizeof(list[j].address) - 1] = '\0';
        list[j].type   [sizeof(list[j].type)    - 1] = '\0';
        list[j].paired          = g_bt_devices[i].paired;
        list[j].connected       = g_bt_devices[i].connected;
        list[j].battery_percent = g_bt_devices[i].battery_percent;
        j++;
    }
    *out_list  = list;
    *out_count = count;
    return 0;
}

static ZylBtHal g_bt_hal = {
    .init               = bt_init,
    .shutdown           = bt_shutdown,
    .set_enabled        = bt_set_enabled,
    .get_state          = bt_get_state,
    .scan               = bt_scan,
    .pair               = bt_pair,
    .unpair             = bt_unpair,
    .connect_device     = bt_connect_device,
    .disconnect_device  = bt_disconnect_device,
    .get_paired_devices = bt_get_paired_devices,
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
   Audio 에뮬레이터
   ══════════════════════════════════════════════════════════ */

static int  g_audio_media        = 70;
static int  g_audio_notification = 80;
static int  g_audio_alarm        = 100;
static int  g_audio_call         = 70;
static bool g_audio_vibration    = true;
static bool g_audio_silent       = false;

static int  audio_init(void) { return 0; }
static void audio_shutdown(void) {}

static int audio_set_volume(const char *stream, int percent) {
    if (!stream || percent < 0 || percent > 100) return -1;
    if (strcmp(stream, "media")        == 0) { g_audio_media        = percent; return 0; }
    if (strcmp(stream, "notification") == 0) { g_audio_notification = percent; return 0; }
    if (strcmp(stream, "alarm")        == 0) { g_audio_alarm        = percent; return 0; }
    if (strcmp(stream, "call")         == 0) { g_audio_call         = percent; return 0; }
    return -1;
}

static int audio_get_volume(const char *stream, int *out_percent) {
    if (!stream || !out_percent) return -1;
    if (strcmp(stream, "media")        == 0) { *out_percent = g_audio_media;        return 0; }
    if (strcmp(stream, "notification") == 0) { *out_percent = g_audio_notification; return 0; }
    if (strcmp(stream, "alarm")        == 0) { *out_percent = g_audio_alarm;        return 0; }
    if (strcmp(stream, "call")         == 0) { *out_percent = g_audio_call;         return 0; }
    return -1;
}

static int audio_set_vibration(bool enabled) {
    g_audio_vibration = enabled;
    return 0;
}

static int audio_set_silent_mode(bool enabled) {
    g_audio_silent = enabled;
    return 0;
}

static int audio_get_state(ZylAudioState *out) {
    if (!out) return -1;
    out->media_volume        = g_audio_media;
    out->notification_volume = g_audio_notification;
    out->alarm_volume        = g_audio_alarm;
    out->call_volume         = g_audio_call;
    out->vibration           = g_audio_vibration;
    out->silent_mode         = g_audio_silent;
    return 0;
}

static ZylAudioHal g_audio_hal = {
    .init            = audio_init,
    .shutdown        = audio_shutdown,
    .set_volume      = audio_set_volume,
    .get_volume      = audio_get_volume,
    .set_vibration   = audio_set_vibration,
    .set_silent_mode = audio_set_silent_mode,
    .get_state       = audio_get_state,
};


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
   HAL Registry 생성 / 소멸
   ══════════════════════════════════════════════════════════ */

ZylHalRegistry *zyl_hal_create_emulator(void) {
    ZylHalRegistry *hal = malloc(sizeof(ZylHalRegistry));
    if (!hal) return NULL;

    hal->wifi      = &g_wifi_hal;
    hal->bluetooth = &g_bt_hal;
    hal->display   = &g_display_hal;
    hal->audio     = &g_audio_hal;
    hal->battery   = &g_battery_hal;
    hal->storage   = &g_storage_hal;

    /* 순서: 의존성 없으므로 임의 순서 가능 */
    hal->wifi->init();
    hal->bluetooth->init();
    hal->display->init();
    hal->audio->init();
    hal->battery->init();
    hal->storage->init();

    return hal;
}

void zyl_hal_destroy(ZylHalRegistry *hal) {
    if (!hal) return;
    hal->storage->shutdown();
    hal->battery->shutdown();   /* 스레드 join 포함 */
    hal->audio->shutdown();
    hal->display->shutdown();
    hal->bluetooth->shutdown();
    hal->wifi->shutdown();
    free(hal);
}
