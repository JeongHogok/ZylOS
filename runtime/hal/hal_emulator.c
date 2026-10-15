/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Platform Layer - Emulator
 *
 * 역할: ZylOS HAL 에뮬레이터 — VM·개발 호스트용 가상 하드웨어
 * 수행범위: 가상 Wi-Fi/BT/출력, 오디오는 볼륨 상태만, 저장공간은 호스트 실제 용량.
 *          VM 에 없는 하드웨어(부트로더, 통화 경로, 진동 모터, 백라이트, 배터리,
 *          손전등, cpufreq)는 없다고 답한다 — 지어내지 않는다.
 *          실제 하드웨어, D-Bus, sysfs, gio 완전 불필요.
 *          Linux과 macOS 모두 빌드 가능.
 * 의존방향: hal.h (Domain Interface)만 의존. 외부 라이브러리 없음.
 * SOLID: DIP — hal.h 의 포트만 구현 (모듈 접근자).
 *        SRP — 모듈별 독립된 상태 구조체와 함수군.
 *        ISP — 각 HAL 인터페이스를 독립 구현 블록으로 분리.
 * ────────────────────────────────────────────────────────── */

#define _DEFAULT_SOURCE   /* sysconf(_SC_PHYS_PAGES) */

#include "hal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

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
   Display 에뮬레이터 — 가상 출력 하나. VM 에는 백라이트가 없다
   ══════════════════════════════════════════════════════════ */

static int emu_display_list_outputs(ZylDisplayOutput *out, int max) {
    if (max < 1) return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "Virtual-1");
    out->connected = true;
    out->mode_count = 1;
    out->modes[0] = (ZylDisplayModeInfo){ 1080, 2400, 60 };
    return 1;
}
static bool emu_display_backlight_present(void) { return false; }
static int  emu_display_get_brightness(void) { return -1; }
static bool emu_display_set_brightness(int percent) { (void)percent; return false; }

static const ZylDisplayHal g_display_hal = {
    .list_outputs      = emu_display_list_outputs,
    .backlight_present = emu_display_backlight_present,
    .get_brightness    = emu_display_get_brightness,
    .set_brightness    = emu_display_set_brightness,
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
   전원 — VM 에는 배터리가 없고 외부 전원으로 돈다. 손전등도, 조절할 cpufreq 도
   없다. 저장공간은 호스트 파일시스템의 실제 용량
   ══════════════════════════════════════════════════════════ */

static bool emu_battery_get(ZylBatteryState *out) {
    memset(out, 0, sizeof(*out));
    out->level_percent = -1;
    out->plugged = true;
    return true;
}
static const ZylBatteryHal g_battery_hal = { .get = emu_battery_get };

static bool emu_storage_get(ZylStorageState *out) {
    memset(out, 0, sizeof(*out));
    struct statvfs sv;
    if (statvfs("/", &sv) != 0) return false;
    out->total_bytes     = (uint64_t)sv.f_blocks * sv.f_frsize;
    out->available_bytes = (uint64_t)sv.f_bavail * sv.f_frsize;
    out->used_bytes      = out->total_bytes - (uint64_t)sv.f_bfree * sv.f_frsize;
    return true;
}
static const ZylStorageHal g_storage_hal = { .get = emu_storage_get };

static bool emu_torch_present(void) { return false; }
static bool emu_torch_get(void) { return false; }
static bool emu_torch_set(bool on) { (void)on; return false; }
static const ZylTorchHal g_torch_hal = { .present = emu_torch_present, .get = emu_torch_get, .set = emu_torch_set };

static int  emu_cpu_core_count(void) { return 1; }
static bool emu_cpu_set_core_online(int core, bool online) { (void)core; (void)online; return false; }
static bool emu_cpu_set_governor(const char *g) { (void)g; return false; }
/* 가상 기기 — 메모리와 커널은 이 VM 의 실제 값 */
static bool emu_board_get(ZylBoardInfo *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->model, sizeof(out->model), "Zyl OS Virtual Device");
    snprintf(out->soc, sizeof(out->soc), "virtual");
    long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0) out->ram_bytes = (uint64_t)pages * (uint64_t)page;
    struct utsname u;
    if (uname(&u) == 0) snprintf(out->kernel, sizeof(out->kernel), "%s", u.release);
    return true;
}
static const ZylBoardHal g_board_hal = { .get = emu_board_get };

static const ZylCpuHal g_cpu_hal = {
    .core_count = emu_cpu_core_count, .set_core_online = emu_cpu_set_core_online, .set_governor = emu_cpu_set_governor,
};


/* ══════════════════════════════════════════════════════════
   모듈 접근자
   ══════════════════════════════════════════════════════════ */

const ZylAudioHal    *zyl_hal_audio(void)    { return &g_audio_hal; }
const ZylVibratorHal *zyl_hal_vibrator(void) { return &g_vibrator_hal; }
const ZylWifiHal     *zyl_hal_wifi(void)     { return &g_wifi_hal; }
const ZylBtHal       *zyl_hal_bt(void)       { return &g_bt_hal; }
const ZylDisplayHal  *zyl_hal_display(void)  { return &g_display_hal; }
const ZylBatteryHal  *zyl_hal_battery(void)  { return &g_battery_hal; }
const ZylStorageHal  *zyl_hal_storage(void)  { return &g_storage_hal; }
const ZylTorchHal    *zyl_hal_torch(void)    { return &g_torch_hal; }
const ZylCpuHal      *zyl_hal_cpu(void)      { return &g_cpu_hal; }
const ZylBoardHal    *zyl_hal_board(void)    { return &g_board_hal; }

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
