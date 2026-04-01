// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Emulated Services
//
// 역할: 실기기 D-Bus 시스템 서비스를 HAL 수준으로 에뮬레이션
// 수행범위: notification, power/battery, location, user, credential,
//           wifi, bluetooth, audio, display, storage, camera,
//           sensors, telephony, nfc, alarm
// 의존방향: state (AppState, mount_point), tauri AppHandle
// SOLID: SRP — 에뮬레이션 서비스만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::state::AppState;
use serde::{Deserialize, Serialize};
use std::fs;
use std::sync::Mutex;
use tauri::{Emitter, State};

// ════════════════════════════════════════════
// 1. Notification Service
// ════════════════════════════════════════════

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Notification {
    pub id: u64,
    pub app_id: String,
    pub title: String,
    pub body: String,
    pub icon: String,
    pub priority: u32,
    pub timestamp: u64,
}

use std::sync::atomic::{AtomicU64, Ordering};

static NOTIF_COUNTER: AtomicU64 = AtomicU64::new(0);
static NOTIF_STORE: std::sync::LazyLock<Mutex<Vec<Notification>>> =
    std::sync::LazyLock::new(|| Mutex::new(Vec::new()));

#[tauri::command]
pub fn notif_post(app_id: String, title: String, body: String, icon: String, priority: u32) -> u64 {
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    let id = NOTIF_COUNTER.fetch_add(1, Ordering::SeqCst) + 1;
    let notif = Notification {
        id, app_id, title, body, icon, priority, timestamp: ts,
    };
    if let Ok(mut store) = NOTIF_STORE.lock() {
        store.push(notif);
    }
    id
}

#[tauri::command]
pub fn notif_cancel(id: u64) {
    if let Ok(mut store) = NOTIF_STORE.lock() {
        store.retain(|n| n.id != id);
    }
}

#[tauri::command]
pub fn notif_get_active() -> Vec<Notification> {
    NOTIF_STORE.lock().map(|s| s.clone()).unwrap_or_default()
}

#[tauri::command]
pub fn notif_clear_all() {
    if let Ok(mut store) = NOTIF_STORE.lock() {
        store.clear();
    }
}

// ════════════════════════════════════════════
// 2. Power / Brightness Service (legacy)
// ════════════════════════════════════════════

static BRIGHTNESS: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(80);

#[tauri::command]
pub fn power_get_state() -> serde_json::Value {
    let battery = crate::platform::get_battery_info().unwrap_or_else(|_| {
        serde_json::json!({"level": 100, "charging": true})
    });
    let brightness = BRIGHTNESS.load(std::sync::atomic::Ordering::Relaxed);

    serde_json::json!({
        "state": "ACTIVE",
        "brightness": brightness,
        "battery": battery,
        "screenOn": true,
    })
}

#[tauri::command]
pub fn power_set_brightness(percent: u32) -> serde_json::Value {
    let clamped = percent.min(100);
    BRIGHTNESS.store(clamped, std::sync::atomic::Ordering::Relaxed);
    serde_json::json!({ "brightness": clamped })
}

// ════════════════════════════════════════════
// 3. Location Service
// ════════════════════════════════════════════

#[tauri::command]
pub async fn location_get_last_known() -> serde_json::Value {
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64;

    let (tx, rx) = tokio::sync::oneshot::channel();

    std::thread::spawn(move || {
        let result = std::process::Command::new("curl")
            .args(["-s", "-m", "3", "--connect-timeout", "2", "https://ipinfo.io/json"])
            .output();

        let location = match result {
            Ok(output) if output.status.success() => {
                String::from_utf8(output.stdout)
                    .ok()
                    .and_then(|text| serde_json::from_str::<serde_json::Value>(&text).ok())
                    .and_then(|data| {
                        let loc_str = data["loc"].as_str()?;
                        let parts: Vec<&str> = loc_str.split(',').collect();
                        if parts.len() != 2 { return None; }
                        let lat = parts[0].parse::<f64>().ok()?;
                        let lon = parts[1].parse::<f64>().ok()?;
                        Some(serde_json::json!({
                            "latitude": lat,
                            "longitude": lon,
                            "altitude": 0.0,
                            "accuracy": 5000.0,
                            "speed": 0.0,
                            "bearing": 0.0,
                            "timestamp": ts,
                            "provider": "ip-geolocation",
                            "city": data["city"].as_str().unwrap_or(""),
                            "region": data["region"].as_str().unwrap_or(""),
                            "country": data["country"].as_str().unwrap_or("")
                        }))
                    })
            }
            _ => None,
        };

        let _ = tx.send(location);
    });

    match rx.await {
        Ok(Some(loc)) => loc,
        _ => serde_json::json!({
            "latitude": 0.0,
            "longitude": 0.0,
            "altitude": 0.0,
            "accuracy": -1.0,
            "speed": 0.0,
            "bearing": 0.0,
            "timestamp": ts,
            "provider": "unavailable",
            "city": "",
            "region": "",
            "country": ""
        }),
    }
}

// ════════════════════════════════════════════
// 4. User Service
// ════════════════════════════════════════════

#[tauri::command]
pub fn user_get_current(state: State<'_, Mutex<AppState>>) -> serde_json::Value {
    let mut user_name = "user".to_string();
    if let Ok(app_state) = state.lock() {
        let base = app_state.mount_point.as_ref().unwrap_or(&app_state.data_dir);
        let settings_path = base.join("settings.json");
        if let Ok(content) = fs::read_to_string(&settings_path) {
            if let Ok(data) = serde_json::from_str::<serde_json::Value>(&content) {
                if let Some(name) = data["user"]["name"].as_str() {
                    if !name.is_empty() {
                        user_name = name.to_string();
                    }
                }
            }
        }
    }
    serde_json::json!({
        "uid": 1000,
        "name": user_name,
        "type": "OWNER",
        "avatar": "",
        "dataDir": "/data/users/1000"
    })
}

#[tauri::command]
pub fn user_list(state: State<'_, Mutex<AppState>>) -> Vec<serde_json::Value> {
    let current = user_get_current(state);
    vec![serde_json::json!({
        "uid": current["uid"],
        "name": current["name"],
        "type": current["type"],
        "isActive": true
    })]
}

// ════════════════════════════════════════════
// 5. Credential Service (AES-256-GCM)
// ════════════════════════════════════════════

use aes_gcm::{Aes256Gcm, KeyInit, aead::Aead};
use aes_gcm::aead::generic_array::GenericArray;
use rand::RngCore;

fn derive_key(passphrase: &[u8], salt: &[u8]) -> [u8; 32] {
    let mut key = [0u8; 32];
    pbkdf2::pbkdf2_hmac::<sha2::Sha256>(passphrase, salt, 100_000, &mut key);
    key
}

fn get_master_passphrase(app_state: &crate::state::AppState) -> Vec<u8> {
    let base = app_state.mount_point.as_ref()
        .unwrap_or(&app_state.data_dir);

    let salt_path = base.join(".emu_keysalt");
    let profile_salt: Vec<u8> = if salt_path.exists() {
        std::fs::read(&salt_path).unwrap_or_else(|_| b"fallback-salt-v1".to_vec())
    } else {
        use rand::RngCore;
        let mut s = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut s);
        if std::fs::write(&salt_path, &s).is_ok() {
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let _ = std::fs::set_permissions(
                    &salt_path,
                    std::fs::Permissions::from_mode(0o600),
                );
            }
        }
        s.to_vec()
    };

    let machine_secret = read_machine_id();

    let mut passphrase = b"zyl-emu-v2:".to_vec();
    passphrase.extend_from_slice(machine_secret.as_bytes());
    passphrase.push(b':');
    passphrase.extend(profile_salt.iter().flat_map(|b| {
        let h = format!("{:02x}", b);
        h.into_bytes()
    }));
    passphrase
}

fn read_machine_id() -> String {
    #[cfg(target_os = "linux")]
    {
        if let Ok(id) = std::fs::read_to_string("/etc/machine-id") {
            let trimmed = id.trim().to_string();
            if !trimmed.is_empty() {
                return trimmed;
            }
        }
    }

    #[cfg(target_os = "macos")]
    {
        use std::process::Command;
        if let Ok(output) = Command::new("ioreg")
            .args(["-rd1", "-c", "IOPlatformExpertDevice"])
            .output()
        {
            let text = String::from_utf8_lossy(&output.stdout);
            for line in text.lines() {
                if line.contains("IOPlatformUUID") {
                    if let Some(uuid) = line.split('"').nth(3) {
                        if !uuid.is_empty() {
                            return uuid.to_string();
                        }
                    }
                }
            }
        }
    }

    "zyl-emu-machine-unknown".to_string()
}

#[tauri::command]
pub fn credential_store(
    service: String,
    account: String,
    secret: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let base = app_state.mount_point.as_ref()
        .unwrap_or(&app_state.data_dir);
    let cred_dir = base.join(".credentials");
    let _ = fs::create_dir_all(&cred_dir);

    let passphrase = get_master_passphrase(&app_state);

    let mut salt = [0u8; 16];
    let mut nonce_bytes = [0u8; 12];
    rand::thread_rng().fill_bytes(&mut salt);
    rand::thread_rng().fill_bytes(&mut nonce_bytes);

    let key = derive_key(&passphrase, &salt);
    let cipher = Aes256Gcm::new(GenericArray::from_slice(&key));
    let nonce = GenericArray::from_slice(&nonce_bytes);

    let ciphertext = cipher.encrypt(nonce, secret.as_bytes())
        .map_err(|e| format!("Encryption failed: {}", e))?;

    let mut output = Vec::with_capacity(16 + 12 + ciphertext.len());
    output.extend_from_slice(&salt);
    output.extend_from_slice(&nonce_bytes);
    output.extend_from_slice(&ciphertext);

    let key_name = format!("{}_{}", service, account);
    let path = cred_dir.join(format!("{}.enc", key_name));

    fs::write(&path, &output).map_err(|e| format!("Write error: {}", e))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = fs::set_permissions(&path, fs::Permissions::from_mode(0o600));
    }

    Ok(())
}

#[tauri::command]
pub fn credential_lookup(
    service: String,
    account: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<String, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let base = app_state.mount_point.as_ref()
        .unwrap_or(&app_state.data_dir);

    let passphrase = get_master_passphrase(&app_state);

    let key_name = format!("{}_{}", service, account);
    let path = base.join(".credentials").join(format!("{}.enc", key_name));
    let data = fs::read(&path).map_err(|_| "Credential not found".to_string())?;

    if data.len() < 16 + 12 + 16 {
        return Err("Credential file too short".into());
    }

    let salt = &data[..16];
    let nonce_bytes = &data[16..28];
    let ciphertext = &data[28..];

    let key = derive_key(&passphrase, salt);
    let cipher = Aes256Gcm::new(GenericArray::from_slice(&key));
    let nonce = GenericArray::from_slice(nonce_bytes);

    let plaintext = cipher.decrypt(nonce, ciphertext)
        .map_err(|_| "Decryption failed — wrong key or tampered data".to_string())?;

    String::from_utf8(plaintext)
        .map_err(|e| format!("UTF-8 decode error: {}", e))
}

#[tauri::command]
pub fn credential_delete(
    service: String,
    account: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let base = app_state.mount_point.as_ref()
        .unwrap_or(&app_state.data_dir);

    let key_name = format!("{}_{}", service, account);
    let path = base.join(".credentials").join(format!("{}.enc", key_name));

    if path.exists() {
        if let Ok(metadata) = fs::metadata(&path) {
            let len = metadata.len() as usize;
            if len > 0 {
                let mut noise = vec![0u8; len];
                rand::thread_rng().fill_bytes(&mut noise);
                let _ = fs::write(&path, &noise);
            }
        }
    }

    let _ = fs::remove_file(&path);
    Ok(())
}

// ════════════════════════════════════════════
// 6. WiFi Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone, Serialize, Deserialize)]
struct WifiNetwork {
    ssid: String,
    signal: i32,       // dBm, e.g. -45
    security: String,  // "WPA2", "WPA3", "Open"
    frequency: u32,    // MHz, e.g. 2412, 5180
    connected: bool,
    bssid: String,
}

#[derive(Debug, Clone)]
struct WifiState {
    enabled: bool,
    connected: bool,
    current_ssid: Option<String>,
    ip: String,
    mac: String,
    speed: u32, // Mbps
}

impl Default for WifiState {
    fn default() -> Self {
        Self {
            enabled: true,
            connected: true,
            current_ssid: Some("ZylOS_Virtual_AP".to_string()),
            ip: "192.168.1.42".to_string(),
            mac: "02:00:00:AA:BB:CC".to_string(),
            speed: 300,
        }
    }
}

static WIFI_STATE: std::sync::LazyLock<Mutex<WifiState>> =
    std::sync::LazyLock::new(|| Mutex::new(WifiState::default()));

/// Five virtual access points with realistic RF parameters
fn virtual_aps() -> Vec<WifiNetwork> {
    vec![
        WifiNetwork {
            ssid: "ZylOS_Virtual_AP".to_string(),
            signal: -42,
            security: "WPA3".to_string(),
            frequency: 5180,
            connected: false, // filled in dynamically
            bssid: "02:00:00:AA:BB:CC".to_string(),
        },
        WifiNetwork {
            ssid: "HomeNetwork_5G".to_string(),
            signal: -58,
            security: "WPA2".to_string(),
            frequency: 5220,
            connected: false,
            bssid: "12:34:56:78:9A:BC".to_string(),
        },
        WifiNetwork {
            ssid: "OfficeWiFi".to_string(),
            signal: -67,
            security: "WPA2-Enterprise".to_string(),
            frequency: 2412,
            connected: false,
            bssid: "AA:BB:CC:DD:EE:FF".to_string(),
        },
        WifiNetwork {
            ssid: "CoffeeShop_Guest".to_string(),
            signal: -74,
            security: "Open".to_string(),
            frequency: 2437,
            connected: false,
            bssid: "DE:AD:BE:EF:CA:FE".to_string(),
        },
        WifiNetwork {
            ssid: "Neighbor_2.4G".to_string(),
            signal: -81,
            security: "WPA2".to_string(),
            frequency: 2462,
            connected: false,
            bssid: "F0:0D:CA:FE:BA:BE".to_string(),
        },
    ]
}

#[tauri::command]
pub fn get_wifi_networks() -> Vec<serde_json::Value> {
    let state = WIFI_STATE.lock().unwrap_or_else(|e| e.into_inner());
    let current_ssid = state.current_ssid.clone();
    let connected = state.connected && state.enabled;

    virtual_aps()
        .into_iter()
        .map(|mut ap| {
            ap.connected = connected && current_ssid.as_deref() == Some(&ap.ssid);
            serde_json::json!({
                "ssid": ap.ssid,
                "signal": ap.signal,
                "security": ap.security,
                "frequency": ap.frequency,
                "connected": ap.connected,
                "bssid": ap.bssid,
            })
        })
        .collect()
}

#[tauri::command]
pub fn wifi_connect(ssid: String, password: String) -> serde_json::Value {
    let aps = virtual_aps();
    let ap = aps.iter().find(|a| a.ssid == ssid);

    match ap {
        None => serde_json::json!({ "success": false, "error": "Network not found" }),
        Some(ap) => {
            // Validate: open networks need no password; WPA* need ≥8 chars
            if ap.security != "Open" && password.len() < 8 {
                return serde_json::json!({
                    "success": false,
                    "error": "Authentication failed: invalid password"
                });
            }
            let mut state = WIFI_STATE.lock().unwrap_or_else(|e| e.into_inner());
            state.connected = true;
            state.current_ssid = Some(ssid.clone());
            // Simulate DHCP lease
            state.ip = "192.168.1.42".to_string();
            state.speed = if ap.frequency >= 5000 { 300 } else { 150 };
            serde_json::json!({ "success": true, "ssid": ssid, "ip": state.ip })
        }
    }
}

#[tauri::command]
pub fn wifi_disconnect() -> serde_json::Value {
    let mut state = WIFI_STATE.lock().unwrap_or_else(|e| e.into_inner());
    let prev = state.current_ssid.take();
    state.connected = false;
    serde_json::json!({ "success": true, "disconnectedFrom": prev })
}

#[tauri::command]
pub fn wifi_get_state() -> serde_json::Value {
    let state = WIFI_STATE.lock().unwrap_or_else(|e| e.into_inner());
    serde_json::json!({
        "enabled": state.enabled,
        "connected": state.connected && state.enabled,
        "currentSsid": state.current_ssid,
        "ip": if state.connected { &state.ip } else { "" },
        "mac": state.mac,
        "speed": if state.connected { state.speed } else { 0 },
    })
}

// ════════════════════════════════════════════
// 7. Bluetooth Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone, Serialize, Deserialize)]
struct BtDevice {
    name: String,
    address: String,
    device_type: String,
    paired: bool,
    connected: bool,
    rssi: i32,
}

#[derive(Debug, Clone)]
struct BluetoothState {
    enabled: bool,
    device_name: String,
    discoverable: bool,
    devices: Vec<BtDevice>,
}

impl Default for BluetoothState {
    fn default() -> Self {
        Self {
            enabled: true,
            device_name: "ZylOS Device".to_string(),
            discoverable: false,
            devices: vec![
                BtDevice {
                    name: "AirPods Pro".to_string(),
                    address: "11:22:33:44:55:66".to_string(),
                    device_type: "Headphone".to_string(),
                    paired: true,
                    connected: true,
                    rssi: -52,
                },
                BtDevice {
                    name: "Magic Keyboard".to_string(),
                    address: "AA:BB:CC:11:22:33".to_string(),
                    device_type: "Keyboard".to_string(),
                    paired: true,
                    connected: false,
                    rssi: -68,
                },
                BtDevice {
                    name: "Mi Band 7".to_string(),
                    address: "FA:CE:B0:0C:12:34".to_string(),
                    device_type: "Wearable".to_string(),
                    paired: false,
                    connected: false,
                    rssi: -75,
                },
            ],
        }
    }
}

static BT_STATE: std::sync::LazyLock<Mutex<BluetoothState>> =
    std::sync::LazyLock::new(|| Mutex::new(BluetoothState::default()));

#[tauri::command]
pub fn get_bluetooth_devices() -> Vec<serde_json::Value> {
    let state = BT_STATE.lock().unwrap_or_else(|e| e.into_inner());
    if !state.enabled {
        return vec![];
    }
    state.devices.iter().map(|d| serde_json::json!({
        "name": d.name,
        "address": d.address,
        "deviceType": d.device_type,
        "paired": d.paired,
        "connected": d.connected,
        "rssi": d.rssi,
    })).collect()
}

#[tauri::command]
pub fn bt_set_enabled(enabled: bool) -> serde_json::Value {
    let mut state = BT_STATE.lock().unwrap_or_else(|e| e.into_inner());
    state.enabled = enabled;
    if !enabled {
        // Disconnect all on disable
        for d in state.devices.iter_mut() {
            d.connected = false;
        }
    }
    serde_json::json!({ "enabled": state.enabled })
}

#[tauri::command]
pub fn bt_pair(address: String) -> serde_json::Value {
    let mut state = BT_STATE.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(d) = state.devices.iter_mut().find(|d| d.address == address) {
        d.paired = true;
        serde_json::json!({ "success": true, "address": address, "paired": true })
    } else {
        serde_json::json!({ "success": false, "error": "Device not found" })
    }
}

#[tauri::command]
pub fn bt_unpair(address: String) -> serde_json::Value {
    let mut state = BT_STATE.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(d) = state.devices.iter_mut().find(|d| d.address == address) {
        d.paired = false;
        d.connected = false;
        serde_json::json!({ "success": true, "address": address, "paired": false })
    } else {
        serde_json::json!({ "success": false, "error": "Device not found" })
    }
}

#[tauri::command]
pub fn bt_connect(address: String) -> serde_json::Value {
    let mut state = BT_STATE.lock().unwrap_or_else(|e| e.into_inner());
    if !state.enabled {
        return serde_json::json!({ "success": false, "error": "Bluetooth disabled" });
    }
    if let Some(d) = state.devices.iter_mut().find(|d| d.address == address) {
        if !d.paired {
            return serde_json::json!({ "success": false, "error": "Device not paired" });
        }
        d.connected = true;
        serde_json::json!({ "success": true, "address": address, "connected": true })
    } else {
        serde_json::json!({ "success": false, "error": "Device not found" })
    }
}

#[tauri::command]
pub fn bt_disconnect(address: String) -> serde_json::Value {
    let mut state = BT_STATE.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(d) = state.devices.iter_mut().find(|d| d.address == address) {
        d.connected = false;
        serde_json::json!({ "success": true, "address": address, "connected": false })
    } else {
        serde_json::json!({ "success": false, "error": "Device not found" })
    }
}

#[tauri::command]
pub fn bt_get_state() -> serde_json::Value {
    let state = BT_STATE.lock().unwrap_or_else(|e| e.into_inner());
    let connected_devices: Vec<&str> = state.devices.iter()
        .filter(|d| d.connected)
        .map(|d| d.name.as_str())
        .collect();
    serde_json::json!({
        "enabled": state.enabled,
        "deviceName": state.device_name,
        "discoverable": state.discoverable,
        "connectedDevices": connected_devices,
    })
}

// ════════════════════════════════════════════
// 8. Audio Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone)]
struct AudioState {
    media_volume: u8,
    notif_volume: u8,
    alarm_volume: u8,
    ringtone_volume: u8,
    system_volume: u8,
    vibration: bool,
    silent_mode: bool,
}

impl Default for AudioState {
    fn default() -> Self {
        Self {
            media_volume: 70,
            notif_volume: 80,
            alarm_volume: 90,
            ringtone_volume: 80,
            system_volume: 50,
            vibration: true,
            silent_mode: false,
        }
    }
}

static AUDIO_STATE: std::sync::LazyLock<Mutex<AudioState>> =
    std::sync::LazyLock::new(|| Mutex::new(AudioState::default()));

#[tauri::command]
pub fn audio_get_state() -> serde_json::Value {
    let s = AUDIO_STATE.lock().unwrap_or_else(|e| e.into_inner());
    serde_json::json!({
        "mediaVolume": s.media_volume,
        "notifVolume": s.notif_volume,
        "alarmVolume": s.alarm_volume,
        "ringtoneVolume": s.ringtone_volume,
        "systemVolume": s.system_volume,
        "vibration": s.vibration,
        "silentMode": s.silent_mode,
    })
}

#[tauri::command]
pub fn audio_set_volume(stream: String, percent: u8) -> serde_json::Value {
    let clamped = percent.min(100);
    let mut s = AUDIO_STATE.lock().unwrap_or_else(|e| e.into_inner());
    match stream.as_str() {
        "media"     => s.media_volume = clamped,
        "notif"     => s.notif_volume = clamped,
        "alarm"     => s.alarm_volume = clamped,
        "ringtone"  => s.ringtone_volume = clamped,
        "system"    => s.system_volume = clamped,
        _           => return serde_json::json!({ "success": false, "error": "Unknown stream" }),
    }
    serde_json::json!({ "success": true, "stream": stream, "volume": clamped })
}

#[tauri::command]
pub fn audio_set_vibration(enabled: bool) -> serde_json::Value {
    let mut s = AUDIO_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.vibration = enabled;
    serde_json::json!({ "vibration": s.vibration })
}

#[tauri::command]
pub fn audio_set_silent(enabled: bool) -> serde_json::Value {
    let mut s = AUDIO_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.silent_mode = enabled;
    serde_json::json!({ "silentMode": s.silent_mode })
}

// ════════════════════════════════════════════
// 9. Display Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone)]
struct DisplayState {
    brightness: u8,    // 0-100
    dark_mode: bool,
    font_size: String, // "small" | "normal" | "large" | "xlarge"
    screen_timeout: u32, // seconds
}

impl Default for DisplayState {
    fn default() -> Self {
        Self {
            brightness: 80,
            dark_mode: false,
            font_size: "normal".to_string(),
            screen_timeout: 60,
        }
    }
}

static DISPLAY_STATE: std::sync::LazyLock<Mutex<DisplayState>> =
    std::sync::LazyLock::new(|| Mutex::new(DisplayState::default()));

#[tauri::command]
pub fn display_get_state() -> serde_json::Value {
    let s = DISPLAY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    serde_json::json!({
        "brightness": s.brightness,
        "darkMode": s.dark_mode,
        "fontSize": s.font_size,
        "screenTimeout": s.screen_timeout,
    })
}

#[tauri::command]
pub fn display_set_brightness(percent: u8) -> serde_json::Value {
    let clamped = percent.min(100);
    let mut s = DISPLAY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.brightness = clamped;
    // Keep legacy atomic in sync
    BRIGHTNESS.store(clamped as u32, std::sync::atomic::Ordering::Relaxed);
    serde_json::json!({ "brightness": clamped })
}

#[tauri::command]
pub fn display_set_dark_mode(enabled: bool) -> serde_json::Value {
    let mut s = DISPLAY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.dark_mode = enabled;
    serde_json::json!({ "darkMode": s.dark_mode })
}

#[tauri::command]
pub fn display_set_font_size(size: String) -> serde_json::Value {
    const VALID: &[&str] = &["small", "normal", "large", "xlarge"];
    if !VALID.contains(&size.as_str()) {
        return serde_json::json!({ "success": false, "error": "Invalid font size" });
    }
    let mut s = DISPLAY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.font_size = size.clone();
    serde_json::json!({ "fontSize": size })
}

#[tauri::command]
pub fn display_set_screen_timeout(seconds: u32) -> serde_json::Value {
    let mut s = DISPLAY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.screen_timeout = seconds;
    serde_json::json!({ "screenTimeout": seconds })
}

// ════════════════════════════════════════════
// 10. Battery Service (Emulated HAL, time-based simulation)
// ════════════════════════════════════════════
//
// Simulation model:
//   - Charging:    +1% every 30 s
//   - Discharging: -1% every 60 s
// Level is computed lazily on each get_battery_state call based on
// elapsed wall-clock time since the last call, so no background thread
// is needed.

#[derive(Debug, Clone)]
struct BatteryState {
    level: u8,           // 0-100
    charging: bool,
    health: String,      // "Good" | "Overheat" | "Dead"
    temperature: f32,    // °C
    voltage: f32,        // V
    last_tick_secs: u64, // unix timestamp of last simulation tick
    last_level: u8,      // level at last call (for change detection)
}

impl Default for BatteryState {
    fn default() -> Self {
        let now = unix_now_secs();
        Self {
            level: 78,
            charging: true,
            health: "Good".to_string(),
            temperature: 32.5,
            voltage: 3.95,
            last_tick_secs: now,
            last_level: 78,
        }
    }
}

fn unix_now_secs() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

static BATTERY_STATE: std::sync::LazyLock<Mutex<BatteryState>> =
    std::sync::LazyLock::new(|| Mutex::new(BatteryState::default()));

#[tauri::command]
pub fn get_battery_state(app_handle: tauri::AppHandle) -> serde_json::Value {
    let mut s = BATTERY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    let now = unix_now_secs();
    let elapsed = now.saturating_sub(s.last_tick_secs);

    // Simulate level change based on elapsed time
    if s.charging {
        // +1% per 30 s, cap at 100%
        let ticks = (elapsed / 30).min(100) as u8;
        s.level = s.level.saturating_add(ticks).min(100);
        if s.level == 100 {
            s.charging = false; // full → switch to discharging
        }
    } else {
        // -1% per 60 s, floor at 0%
        let ticks = (elapsed / 60).min(100) as u8;
        s.level = s.level.saturating_sub(ticks);
        if s.level == 0 {
            s.charging = true; // emulator auto-charges on empty
        }
    }

    // Simulate temperature drift (slightly higher while charging)
    s.temperature = 32.5 + if s.charging { 1.8 } else { 0.0 }
        + (now % 10) as f32 * 0.05;
    // Voltage correlates roughly with SOC
    s.voltage = 3.6 + (s.level as f32 / 100.0) * 0.6;

    s.last_tick_secs = now;

    // Emit event if level changed (frontend can subscribe)
    if s.level != s.last_level {
        let _ = app_handle.emit("battery-level-change", serde_json::json!({
            "level": s.level,
            "charging": s.charging,
        }));
        s.last_level = s.level;
    }

    serde_json::json!({
        "level": s.level,
        "charging": s.charging,
        "health": s.health,
        "temperature": s.temperature,
        "voltage": s.voltage,
    })
}

// ════════════════════════════════════════════
// 11. Storage Service (Emulated 64 GB device)
// ════════════════════════════════════════════

const STORAGE_TOTAL: u64 = 64 * 1024 * 1024 * 1024;   // 64 GiB
const STORAGE_USED: u64  = 22 * 1024 * 1024 * 1024;   // 22 GiB used (realistic)

#[tauri::command]
pub fn get_storage_state() -> serde_json::Value {
    let available = STORAGE_TOTAL - STORAGE_USED;
    serde_json::json!({
        "total":     STORAGE_TOTAL,
        "used":      STORAGE_USED,
        "available": available,
        "percent":   (STORAGE_USED as f64 / STORAGE_TOTAL as f64 * 100.0).round() as u32,
    })
}

// ════════════════════════════════════════════
// 12. Camera Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone)]
struct CameraState {
    preview_active: bool,
    active_camera_id: Option<String>,
}

static CAMERA_STATE: std::sync::LazyLock<Mutex<CameraState>> =
    std::sync::LazyLock::new(|| Mutex::new(CameraState {
        preview_active: false,
        active_camera_id: None,
    }));

/// Generate a minimal valid PNG (solid color, 64×64) using only std + CRC32.
/// No external crate required beyond the `base64` already in Cargo.toml.
fn generate_solid_png(width: u32, height: u32, r: u8, g: u8, b: u8) -> Vec<u8> {
    // Build raw scanlines: filter=0 per row, then RGB pixels
    let row_bytes = 1 + (width as usize) * 3;
    let mut raw = vec![0u8; (height as usize) * row_bytes];
    for row in 0..height as usize {
        raw[row * row_bytes] = 0; // filter byte: None
        for col in 0..width as usize {
            let off = row * row_bytes + 1 + col * 3;
            raw[off]     = r;
            raw[off + 1] = g;
            raw[off + 2] = b;
        }
    }

    let compressed = zlib_store_blocks(&raw);

    let mut ihdr = Vec::with_capacity(13);
    ihdr.extend_from_slice(&width.to_be_bytes());
    ihdr.extend_from_slice(&height.to_be_bytes());
    ihdr.push(8); // bit depth
    ihdr.push(2); // color type: RGB
    ihdr.push(0); ihdr.push(0); ihdr.push(0); // compress/filter/interlace

    let mut png = Vec::new();
    png.extend_from_slice(b"\x89PNG\r\n\x1a\n"); // PNG signature
    png_chunk(&mut png, b"IHDR", &ihdr);
    png_chunk(&mut png, b"IDAT", &compressed);
    png_chunk(&mut png, b"IEND", b"");
    png
}

fn png_chunk(out: &mut Vec<u8>, name: &[u8; 4], data: &[u8]) {
    out.extend_from_slice(&(data.len() as u32).to_be_bytes());
    out.extend_from_slice(name);
    out.extend_from_slice(data);
    let crc = png_crc32(name, data);
    out.extend_from_slice(&crc.to_be_bytes());
}

fn png_crc32(name: &[u8], data: &[u8]) -> u32 {
    static TABLE: std::sync::OnceLock<[u32; 256]> = std::sync::OnceLock::new();
    let t = TABLE.get_or_init(|| {
        let mut table = [0u32; 256];
        for n in 0..256usize {
            let mut c = n as u32;
            for _ in 0..8 {
                if c & 1 != 0 { c = 0xEDB8_8320 ^ (c >> 1); } else { c >>= 1; }
            }
            table[n] = c;
        }
        table
    });
    let mut crc: u32 = 0xFFFF_FFFF;
    for &byte in name.iter().chain(data.iter()) {
        crc = t[((crc ^ byte as u32) & 0xFF) as usize] ^ (crc >> 8);
    }
    crc ^ 0xFFFF_FFFF
}

fn adler32(data: &[u8]) -> u32 {
    let (mut s1, mut s2) = (1u32, 0u32);
    for &b in data {
        s1 = (s1 + b as u32) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    (s2 << 16) | s1
}

/// Wrap raw bytes in a zlib stream using deflate stored (no-compression) blocks.
fn zlib_store_blocks(data: &[u8]) -> Vec<u8> {
    // CMF=0x78 (deflate, window 32K), FLG chosen so (CMF*256+FLG) % 31 == 0
    // 0x78*256 = 30720; 30720 % 31 = 30; need to add 1 → FLG = 1
    let mut out = vec![0x78u8, 0x01];

    const BLOCK_MAX: usize = 65535;
    let mut offset = 0;
    while offset <= data.len() {
        let end = (offset + BLOCK_MAX).min(data.len());
        let is_last = end == data.len();
        let block = &data[offset..end];
        let len = block.len() as u16;
        let nlen = !len;
        out.push(if is_last { 0x01 } else { 0x00 }); // BFINAL, BTYPE=00
        out.extend_from_slice(&len.to_le_bytes());
        out.extend_from_slice(&nlen.to_le_bytes());
        out.extend_from_slice(block);
        if is_last { break; }
        offset = end;
    }

    // Adler-32 checksum of original data
    out.extend_from_slice(&adler32(data).to_be_bytes());
    out
}

use base64::Engine as _;

#[tauri::command]
pub fn camera_get_capabilities() -> serde_json::Value {
    serde_json::json!({
        "cameras": [
            {
                "id": "back",
                "label": "Main Camera",
                "facing": "back",
                "resolutions": ["4032x3024", "3840x2160", "1920x1080", "1280x720"],
                "maxFps": 60,
                "hasFlash": true,
                "hasOIS": true,
                "aperture": "f/1.8",
                "focalLength": 26,
            },
            {
                "id": "front",
                "label": "Selfie Camera",
                "facing": "front",
                "resolutions": ["3264x2448", "1920x1080", "1280x720"],
                "maxFps": 30,
                "hasFlash": false,
                "hasOIS": false,
                "aperture": "f/2.2",
                "focalLength": 23,
            }
        ]
    })
}

#[tauri::command]
pub fn camera_capture(camera_id: String) -> serde_json::Value {
    // Generate a small 64×64 colored placeholder PNG
    let (r, g, b) = match camera_id.as_str() {
        "front" => (245u8, 158u8, 11u8),  // amber — selfie warmth
        _       => (14u8,  165u8, 233u8), // sky-blue — main camera
    };
    let png_bytes = generate_solid_png(64, 64, r, g, b);
    let b64 = base64::engine::general_purpose::STANDARD.encode(&png_bytes);
    let data_uri = format!("data:image/png;base64,{}", b64);

    serde_json::json!({
        "success": true,
        "cameraId": camera_id,
        "imageData": data_uri,
        "width": 64,
        "height": 64,
        "format": "png",
    })
}

#[tauri::command]
pub fn camera_start_preview(camera_id: String) -> serde_json::Value {
    let mut s = CAMERA_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.preview_active = true;
    s.active_camera_id = Some(camera_id.clone());
    serde_json::json!({ "success": true, "cameraId": camera_id, "previewActive": true })
}

#[tauri::command]
pub fn camera_stop_preview() -> serde_json::Value {
    let mut s = CAMERA_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.preview_active = false;
    s.active_camera_id = None;
    serde_json::json!({ "success": true, "previewActive": false })
}

// ════════════════════════════════════════════
// 13. Sensors Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone)]
struct SensorSession {
    sensor_type: String,
    rate_hz: f32,
    started_at: std::time::Instant,
}

#[derive(Debug, Clone, Default)]
struct SensorsState {
    active: Vec<SensorSession>,
}

static SENSORS_STATE: std::sync::LazyLock<Mutex<SensorsState>> =
    std::sync::LazyLock::new(|| Mutex::new(SensorsState::default()));

#[tauri::command]
pub fn sensors_get_available() -> serde_json::Value {
    serde_json::json!([
        { "type": "accelerometer",   "name": "3-Axis Accelerometer", "vendor": "Zyl HAL", "maxRange": 19.6, "resolution": 0.0012 },
        { "type": "gyroscope",       "name": "3-Axis Gyroscope",     "vendor": "Zyl HAL", "maxRange": 34.9, "resolution": 0.0011 },
        { "type": "proximity",       "name": "Proximity Sensor",     "vendor": "Zyl HAL", "maxRange": 5.0,  "resolution": 1.0   },
        { "type": "ambient_light",   "name": "Ambient Light Sensor", "vendor": "Zyl HAL", "maxRange": 10000.0, "resolution": 0.1 },
        { "type": "magnetometer",    "name": "3-Axis Magnetometer",  "vendor": "Zyl HAL", "maxRange": 4900.0, "resolution": 0.3  },
    ])
}

#[tauri::command]
pub fn sensors_start(sensor_type: String, rate_hz: f32) -> serde_json::Value {
    let mut s = SENSORS_STATE.lock().unwrap_or_else(|e| e.into_inner());
    // Remove any existing session for this sensor
    s.active.retain(|sess| sess.sensor_type != sensor_type);
    s.active.push(SensorSession {
        sensor_type: sensor_type.clone(),
        rate_hz,
        started_at: std::time::Instant::now(),
    });
    serde_json::json!({ "success": true, "sensorType": sensor_type, "rateHz": rate_hz })
}

#[tauri::command]
pub fn sensors_stop(sensor_type: String) -> serde_json::Value {
    let mut s = SENSORS_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.active.retain(|sess| sess.sensor_type != sensor_type);
    serde_json::json!({ "success": true, "sensorType": sensor_type })
}

#[tauri::command]
pub fn sensors_get_reading(sensor_type: String) -> serde_json::Value {
    // Use elapsed time to produce deterministic but time-varying simulated values
    let t = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs_f64();

    let ts = (t * 1000.0) as u64;
    let noise = |amp: f64| -> f64 {
        // Pseudo-noise based on time hash — no rand needed
        let h = ((t * 137.0 + amp * 31.0).sin() * 43758.5453) % 1.0;
        h * amp * 2.0 - amp
    };

    let values: serde_json::Value = match sensor_type.as_str() {
        "accelerometer" => {
            // Simulates device lying flat — gravity on Z axis
            let ax = noise(0.05);
            let ay = noise(0.05);
            let az = -9.80665 + noise(0.08);
            serde_json::json!([ax, ay, az])
        }
        "gyroscope" => {
            // Simulates mild hand tremor
            let wx = (t * 2.1).sin() * 0.003 + noise(0.001);
            let wy = (t * 1.7).cos() * 0.003 + noise(0.001);
            let wz = (t * 0.8).sin() * 0.002 + noise(0.001);
            serde_json::json!([wx, wy, wz])
        }
        "proximity" => {
            // Object far (~5 cm) most of the time, near (~0 cm) occasionally
            let near = (t * 0.1).sin() > 0.8;
            let dist: f64 = if near { noise(0.1).abs() } else { 4.5 + noise(0.3) };
            let binary = if dist < 0.5 { 1 } else { 0 };
            serde_json::json!([binary, dist.max(0.0)])
        }
        "ambient_light" => {
            // Indoor room: ~300 lux, with slow drift
            let lux = 300.0 + (t * 0.2).sin() * 50.0 + noise(5.0);
            serde_json::json!([lux.max(0.0)])
        }
        "magnetometer" => {
            // Earth's magnetic field — Seoul approximate values
            let mx = 22.0 + (t * 0.1).sin() * 2.0 + noise(0.5);
            let my = -5.0 + (t * 0.15).cos() * 1.5 + noise(0.5);
            let mz = -42.0 + (t * 0.08).sin() * 1.5 + noise(0.5);
            serde_json::json!([mx, my, mz])
        }
        _ => serde_json::json!([])
    };

    serde_json::json!({
        "sensorType": sensor_type,
        "values": values,
        "timestamp": ts,
        "accuracy": 3, // HIGH
    })
}

// ════════════════════════════════════════════
// 14. Telephony Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone, Serialize, Deserialize)]
struct SmsMessage {
    id: u64,
    to: String,
    from: String,
    text: String,
    timestamp: u64,
    direction: String, // "outgoing" | "incoming"
}

#[derive(Debug, Clone)]
struct TelephonyState {
    call_state: String,  // "IDLE" | "DIALING" | "ACTIVE" | "HELD"
    call_number: String,
    call_start_secs: u64,
    sms_list: Vec<SmsMessage>,
    sms_id_counter: u64,
}

impl Default for TelephonyState {
    fn default() -> Self {
        Self {
            call_state: "IDLE".to_string(),
            call_number: String::new(),
            call_start_secs: 0,
            sms_list: Vec::new(),
            sms_id_counter: 0,
        }
    }
}

static TELEPHONY_STATE: std::sync::LazyLock<Mutex<TelephonyState>> =
    std::sync::LazyLock::new(|| Mutex::new(TelephonyState::default()));

#[tauri::command]
pub fn telephony_get_state() -> serde_json::Value {
    serde_json::json!({
        "simPresent": true,
        "operator": "Zyl Mobile",
        "networkType": "5G",
        "signal": 4,       // bars 0-5
        "imei": "355619090223697",
        "phoneNumber": "+82-10-0000-0001",
        "iccid": "8982010000000000001F",
        "roaming": false,
    })
}

#[tauri::command]
pub fn telephony_dial(number: String) -> serde_json::Value {
    let mut s = TELEPHONY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    if s.call_state != "IDLE" {
        return serde_json::json!({ "success": false, "error": "Line busy" });
    }
    s.call_state = "DIALING".to_string();
    s.call_number = number.clone();
    s.call_start_secs = unix_now_secs();
    serde_json::json!({
        "success": true,
        "state": "DIALING",
        "number": number,
    })
}

#[tauri::command]
pub fn telephony_hangup() -> serde_json::Value {
    let mut s = TELEPHONY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    let duration = if s.call_start_secs > 0 {
        unix_now_secs().saturating_sub(s.call_start_secs)
    } else { 0 };
    let number = s.call_number.clone();
    let prev_state = s.call_state.clone();
    s.call_state = "IDLE".to_string();
    s.call_number = String::new();
    s.call_start_secs = 0;
    serde_json::json!({
        "success": true,
        "previousState": prev_state,
        "number": number,
        "durationSecs": duration,
    })
}

#[tauri::command]
pub fn telephony_send_sms(to: String, text: String) -> serde_json::Value {
    let mut s = TELEPHONY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.sms_id_counter += 1;
    let msg = SmsMessage {
        id: s.sms_id_counter,
        to: to.clone(),
        from: "+82-10-0000-0001".to_string(),
        text: text.clone(),
        timestamp: unix_now_secs(),
        direction: "outgoing".to_string(),
    };
    s.sms_list.push(msg);
    serde_json::json!({
        "success": true,
        "id": s.sms_id_counter,
        "to": to,
    })
}

#[tauri::command]
pub fn telephony_get_sms_list() -> Vec<serde_json::Value> {
    let s = TELEPHONY_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.sms_list.iter().map(|m| serde_json::json!({
        "id": m.id,
        "to": m.to,
        "from": m.from,
        "text": m.text,
        "timestamp": m.timestamp,
        "direction": m.direction,
    })).collect()
}

// ════════════════════════════════════════════
// 15. NFC Service (Emulated HAL)
// ════════════════════════════════════════════

#[derive(Debug, Clone)]
struct NfcState {
    enabled: bool,
    scanning: bool,
    last_tag: Option<serde_json::Value>,
}

impl Default for NfcState {
    fn default() -> Self {
        Self {
            enabled: true,
            scanning: false,
            last_tag: None,
        }
    }
}

static NFC_STATE: std::sync::LazyLock<Mutex<NfcState>> =
    std::sync::LazyLock::new(|| Mutex::new(NfcState::default()));

#[tauri::command]
pub fn nfc_get_state() -> serde_json::Value {
    let s = NFC_STATE.lock().unwrap_or_else(|e| e.into_inner());
    serde_json::json!({
        "enabled": s.enabled,
        "scanning": s.scanning,
        "hasLastTag": s.last_tag.is_some(),
    })
}

#[tauri::command]
pub fn nfc_start_scan() -> serde_json::Value {
    let mut s = NFC_STATE.lock().unwrap_or_else(|e| e.into_inner());
    if !s.enabled {
        return serde_json::json!({ "success": false, "error": "NFC disabled" });
    }
    s.scanning = true;
    // Simulate a tag appearing immediately in the emulator
    s.last_tag = Some(serde_json::json!({
        "uid": "04:A3:B2:C1:D0:E5:F6",
        "technology": "NfcA",
        "type": "NDEF",
        "records": [
            {
                "tnf": 1,
                "type": "T",
                "payload": "enZylOS Virtual Tag",
                "mimeType": "text/plain",
            }
        ],
        "atqa": "0x0004",
        "sak": 32,
        "maxTransceiveLength": 253,
    }));
    serde_json::json!({ "success": true, "scanning": true })
}

#[tauri::command]
pub fn nfc_stop_scan() -> serde_json::Value {
    let mut s = NFC_STATE.lock().unwrap_or_else(|e| e.into_inner());
    s.scanning = false;
    serde_json::json!({ "success": true, "scanning": false })
}

#[tauri::command]
pub fn nfc_get_last_tag() -> serde_json::Value {
    let s = NFC_STATE.lock().unwrap_or_else(|e| e.into_inner());
    match &s.last_tag {
        Some(tag) => tag.clone(),
        None => serde_json::json!(null),
    }
}

// ════════════════════════════════════════════
// 16. Alarm Service (Emulated HAL, file-persisted)
// ════════════════════════════════════════════

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Alarm {
    pub id: String,
    pub hour: u8,
    pub minute: u8,
    pub repeat_days: Vec<u8>, // 0=Sun, 1=Mon, … 6=Sat
    pub enabled: bool,
    pub label: String,
}

static ALARM_STORE: std::sync::LazyLock<Mutex<Vec<Alarm>>> =
    std::sync::LazyLock::new(|| {
        // Load from disk on first access
        let alarms = load_alarms_from_disk().unwrap_or_default();
        Mutex::new(alarms)
    });

fn alarms_file_path() -> std::path::PathBuf {
    dirs::data_local_dir()
        .unwrap_or_else(std::env::temp_dir)
        .join("zyl-emulator")
        .join("alarms.json")
}

fn load_alarms_from_disk() -> Option<Vec<Alarm>> {
    let path = alarms_file_path();
    let content = fs::read_to_string(path).ok()?;
    serde_json::from_str(&content).ok()
}

fn persist_alarms(alarms: &[Alarm]) {
    let path = alarms_file_path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    if let Ok(json) = serde_json::to_string_pretty(alarms) {
        let _ = fs::write(path, json);
    }
}

#[tauri::command]
pub fn alarm_set(
    id: String,
    hour: u8,
    minute: u8,
    repeat_days: Vec<u8>,
    enabled: bool,
    label: Option<String>,
) -> serde_json::Value {
    if hour > 23 || minute > 59 {
        return serde_json::json!({ "success": false, "error": "Invalid time" });
    }
    let alarm = Alarm {
        id: id.clone(),
        hour,
        minute,
        repeat_days,
        enabled,
        label: label.unwrap_or_default(),
    };
    let mut store = ALARM_STORE.lock().unwrap_or_else(|e| e.into_inner());
    store.retain(|a| a.id != id); // upsert
    store.push(alarm);
    persist_alarms(&store);
    serde_json::json!({ "success": true, "id": id })
}

#[tauri::command]
pub fn alarm_cancel(id: String) -> serde_json::Value {
    let mut store = ALARM_STORE.lock().unwrap_or_else(|e| e.into_inner());
    let before = store.len();
    store.retain(|a| a.id != id);
    let removed = store.len() < before;
    persist_alarms(&store);
    serde_json::json!({ "success": removed, "id": id })
}

#[tauri::command]
pub fn alarm_get_all() -> Vec<serde_json::Value> {
    let store = ALARM_STORE.lock().unwrap_or_else(|e| e.into_inner());
    store.iter().map(|a| serde_json::json!({
        "id": a.id,
        "hour": a.hour,
        "minute": a.minute,
        "repeatDays": a.repeat_days,
        "enabled": a.enabled,
        "label": a.label,
    })).collect()
}
