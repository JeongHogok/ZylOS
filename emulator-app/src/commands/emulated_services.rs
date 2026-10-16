// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Emulated Services
//
// 역할: 실기기 D-Bus 시스템 서비스를 HAL 수준으로 에뮬레이션
// 수행범위: power/battery, location, credential(호스트 키에 묶인 보안 저장소),
//           wifi, bluetooth
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
use tauri::State;

// ════════════════════════════════════════════
// 2. Power / Brightness Service (legacy)
// ════════════════════════════════════════════

static BRIGHTNESS: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(80);

#[tauri::command]
pub fn power_get_state() -> serde_json::Value {
    // The host's battery is the virtual device's; a host without one (a
    // desktop) means a device without one — never an invented level
    let battery = match crate::platform::get_battery_info() {
        Ok(mut b) => {
            b["present"] = serde_json::json!(true);
            b
        }
        Err(_) => serde_json::json!({ "present": false }),
    };
    let brightness = BRIGHTNESS.load(std::sync::atomic::Ordering::Relaxed);

    serde_json::json!({
        "state": "ACTIVE",
        "brightness": brightness,
        "battery": battery,
        "screenOn": true,
    })
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

/// A service or account name as the device accepts it (runtime/services/credential
/// is_safe_name): non-empty, at most 255 bytes, no path separator, no "..".
fn safe_name(name: &str) -> bool {
    !name.is_empty() && name.len() <= 255 && !name.contains(['/', '\\', '\0']) && !name.contains("..")
}

/// `.credentials/<service>/<account>.enc` — the device's layout. A directory per
/// service keeps every pair apart (the old `<service>_<account>` file name made
/// "a_b"+"c" and "a"+"b_c" the same file) and the names cannot leave the store.
fn credential_path(base: &std::path::Path, service: &str, account: &str) -> Result<std::path::PathBuf, String> {
    if !safe_name(service) || !safe_name(account) {
        return Err("Invalid service or account name".into());
    }
    Ok(base.join(".credentials").join(service).join(format!("{}.enc", account)))
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
    let path = credential_path(base, &service, &account)?;
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).map_err(|e| format!("Write error: {}", e))?;
    }

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

    let path = credential_path(base, &service, &account)?;
    // Stored before the per-service layout: move it over on first read
    let legacy = base.join(".credentials").join(format!("{}_{}.enc", service, account));
    if !path.exists() && legacy.is_file() {
        if let Some(dir) = path.parent() {
            let _ = fs::create_dir_all(dir);
        }
        let _ = fs::rename(&legacy, &path);
    }
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

    let path = credential_path(base, &service, &account)?;

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
    // A radio that is off hears no access points
    if !state.enabled {
        return vec![];
    }
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

/// The virtual radio's switch (like bt_set_enabled)
#[tauri::command]
pub fn wifi_set_enabled(enabled: bool) -> serde_json::Value {
    let mut state = WIFI_STATE.lock().unwrap_or_else(|e| e.into_inner());
    state.enabled = enabled;
    if !enabled {
        state.connected = false;
        state.current_ssid = None;
    }
    serde_json::json!({ "enabled": state.enabled })
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

#[cfg(test)]
mod credential_tests {
    use super::*;
    use std::path::Path;

    #[test]
    fn names_stay_in_the_store() {
        let base = Path::new("/data");
        for bad in ["", "..", "../x", "a/b", "a\\b", "x..y", "a\0b"] {
            assert!(credential_path(base, bad, "acct").is_err(), "service {:?}", bad);
            assert!(credential_path(base, "svc", bad).is_err(), "account {:?}", bad);
        }
        assert!(credential_path(base, &"s".repeat(256), "a").is_err());
        assert_eq!(credential_path(base, "wifi", "home.net").unwrap(),
                   Path::new("/data/.credentials/wifi/home.net.enc"));
    }

    #[test]
    fn pairs_do_not_collide() {
        let base = Path::new("/data");
        assert_ne!(credential_path(base, "a_b", "c").unwrap(), credential_path(base, "a", "b_c").unwrap());
    }
}
