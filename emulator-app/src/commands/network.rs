// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 호스트 WiFi/Bluetooth 상태 조회
// 수행범위: macOS airport/system_profiler, Linux nmcli/bluetoothctl
// 의존방향: std::process::Command
// SOLID: OCP — cfg 분기로 플랫폼 확장
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use serde::Serialize;
use std::process::Command;

#[derive(Debug, Serialize)]
pub struct WifiNetwork {
    pub ssid: String,
    pub signal: i32,
    pub security: String,
    pub connected: bool,
}

#[derive(Debug, Serialize)]
pub struct BluetoothDevice {
    pub name: String,
    pub address: String,
    pub device_type: String,
    pub paired: bool,
    pub connected: bool,
}

/// WiFi 네트워크 목록 조회 (non-blocking)
#[tauri::command]
pub async fn get_wifi_networks() -> Result<Vec<WifiNetwork>, String> {
    let (tx, rx) = tokio::sync::oneshot::channel();
    std::thread::spawn(move || {
        #[cfg(target_os = "macos")]
        let result = get_wifi_macos();
        #[cfg(target_os = "linux")]
        let result = get_wifi_linux();
        let _ = tx.send(result);
    });
    rx.await.map_err(|_| "WiFi scan cancelled".to_string())?
}

/// Bluetooth 디바이스 목록 조회 (non-blocking)
#[tauri::command]
pub async fn get_bluetooth_devices() -> Result<Vec<BluetoothDevice>, String> {
    let (tx, rx) = tokio::sync::oneshot::channel();
    std::thread::spawn(move || {
        #[cfg(target_os = "macos")]
        let result = get_bt_macos();
        #[cfg(target_os = "linux")]
        let result = get_bt_linux();
        let _ = tx.send(result);
    });
    rx.await.map_err(|_| "Bluetooth scan cancelled".to_string())?
}

// ── macOS WiFi ──

#[cfg(target_os = "macos")]
fn get_wifi_macos() -> Result<Vec<WifiNetwork>, String> {
    let mut networks = Vec::new();

    // 현재 연결된 네트워크
    let current_ssid = Command::new("networksetup")
        .args(["-getairportnetwork", "en0"])
        .output()
        .ok()
        .and_then(|o| {
            let s = String::from_utf8_lossy(&o.stdout).to_string();
            s.split(": ").nth(1).map(|v| v.trim().to_string())
        })
        .unwrap_or_default();

    // 저장된 네트워크 목록 (networksetup)
    let output = Command::new("networksetup")
        .args(["-listpreferredwirelessnetworks", "en0"])
        .output()
        .map_err(|e| format!("networksetup failed: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout);
    for line in stdout.lines().skip(1) {
        let ssid = line.trim().to_string();
        if ssid.is_empty() {
            continue;
        }
        let is_connected = ssid == current_ssid;
        networks.push(WifiNetwork {
            connected: is_connected,
            ssid,
            signal: if is_connected { -30 } else { -65 },
            security: "WPA2".into(),
        });
    }

    // 연결된 네트워크가 목록에 없으면 맨 앞에 추가
    if !current_ssid.is_empty() && !networks.iter().any(|n| n.ssid == current_ssid) {
        networks.insert(0, WifiNetwork {
            ssid: current_ssid,
            signal: -30,
            security: "WPA2".into(),
            connected: true,
        });
    }

    // 연결된 것 먼저, 나머지 신호 강도순
    networks.sort_by(|a, b| b.connected.cmp(&a.connected).then(b.signal.cmp(&a.signal)));
    Ok(networks)
}

// ── Linux WiFi ──

#[cfg(target_os = "linux")]
fn get_wifi_linux() -> Result<Vec<WifiNetwork>, String> {
    let output = Command::new("nmcli")
        .args(["-t", "-f", "SSID,SIGNAL,SECURITY,ACTIVE", "dev", "wifi", "list"])
        .output()
        .map_err(|e| format!("nmcli failed: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut networks = Vec::new();

    for line in stdout.lines() {
        let fields: Vec<&str> = line.split(':').collect();
        if fields.len() < 4 {
            continue;
        }
        let ssid = fields[0].to_string();
        if ssid.is_empty() {
            continue;
        }
        let signal: i32 = fields[1].parse().unwrap_or(0);
        let security = fields[2].to_string();
        let connected = fields[3] == "yes";

        networks.push(WifiNetwork {
            ssid,
            signal: -(100 - signal), // nmcli는 0-100%, RSSI로 변환
            security: if security.is_empty() { "Open".into() } else { security },
            connected,
        });
    }

    networks.sort_by(|a, b| b.signal.cmp(&a.signal));
    Ok(networks)
}

// ── macOS Bluetooth ──

#[cfg(target_os = "macos")]
fn get_bt_macos() -> Result<Vec<BluetoothDevice>, String> {
    let output = Command::new("system_profiler")
        .args(["SPBluetoothDataType", "-json"])
        .output()
        .map_err(|e| format!("system_profiler failed: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout);
    let json: serde_json::Value = serde_json::from_str(&stdout)
        .map_err(|e| format!("JSON parse error: {}", e))?;

    let mut devices = Vec::new();

    // SPBluetoothDataType[0].device_connected / device_not_connected
    if let Some(bt_data) = json.get("SPBluetoothDataType").and_then(|v| v.get(0)) {
        for key in &["device_connected", "device_not_connected"] {
            if let Some(dev_list) = bt_data.get(key).and_then(|v| v.as_array()) {
                let is_connected = *key == "device_connected";
                for dev_obj in dev_list {
                    if let Some(obj) = dev_obj.as_object() {
                        for (name, info) in obj {
                            let address = info
                                .get("device_address")
                                .and_then(|v| v.as_str())
                                .unwrap_or("")
                                .to_string();
                            let minor_type = info
                                .get("device_minorType")
                                .and_then(|v| v.as_str())
                                .unwrap_or("unknown")
                                .to_string();

                            devices.push(BluetoothDevice {
                                name: name.clone(),
                                address,
                                device_type: minor_type,
                                paired: true,
                                connected: is_connected,
                            });
                        }
                    }
                }
            }
        }
    }

    Ok(devices)
}

// ── Linux Bluetooth ──

#[cfg(target_os = "linux")]
fn get_bt_linux() -> Result<Vec<BluetoothDevice>, String> {
    // paired devices
    let output = Command::new("bluetoothctl")
        .args(["devices", "Paired"])
        .output()
        .map_err(|e| format!("bluetoothctl failed: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut devices = Vec::new();

    for line in stdout.lines() {
        // "Device XX:XX:XX:XX:XX:XX DeviceName"
        let parts: Vec<&str> = line.splitn(3, ' ').collect();
        if parts.len() < 3 {
            continue;
        }
        let address = parts[1].to_string();
        let name = parts[2].to_string();

        // 연결 상태 확인
        let info_output = Command::new("bluetoothctl")
            .args(["info", &address])
            .output()
            .ok();

        let connected = info_output
            .as_ref()
            .map(|o| String::from_utf8_lossy(&o.stdout).contains("Connected: yes"))
            .unwrap_or(false);

        devices.push(BluetoothDevice {
            name,
            address,
            device_type: "unknown".into(),
            paired: true,
            connected,
        });
    }

    Ok(devices)
}

// ════════════════════════════════════════════
// HTTP Proxy — network.fetch service backend
// ════════════════════════════════════════════

/// Fetch a URL via curl (non-blocking).
/// Domain whitelist is enforced in the OS service layer (sandbox.js).
/// This command is the backend for apps that need network access (weather, etc.).
///
/// Runs curl in a background thread via std::thread::spawn so the Tauri main
/// thread (and therefore the entire WebView) is never blocked by slow networks.
/// Uses `async` + `tokio::sync::oneshot` pattern to return a Future that resolves
/// when the background thread completes, keeping the Tauri event loop free.
#[tauri::command]
pub async fn http_fetch(url: String) -> Result<String, String> {
    // Basic URL validation
    if !url.starts_with("https://") && !url.starts_with("http://") {
        return Err("Invalid URL: must start with http:// or https://".into());
    }

    // URL length limit (prevent abuse)
    if url.len() > 2048 {
        return Err("URL too long (max 2048 characters)".into());
    }

    // Spawn blocking curl in a background OS thread
    let (tx, rx) = tokio::sync::oneshot::channel();

    std::thread::spawn(move || {
        let result = Command::new("curl")
            .args([
                "-s",          // silent
                "-m", "8",     // max time 8 seconds (reduced from 10)
                "--connect-timeout", "5", // connection timeout 5 seconds
                "-L",          // follow redirects
                "--max-redirs", "3", // max 3 redirects
                "--max-filesize", "1048576", // max 1MB response
                &url,
            ])
            .output();

        let send_result = match result {
            Ok(output) => {
                if !output.status.success() {
                    Err(format!("HTTP request failed with status: {}", output.status))
                } else {
                    String::from_utf8(output.stdout)
                        .map_err(|e| format!("Response encoding error: {}", e))
                }
            }
            Err(e) => Err(format!("curl failed: {}", e)),
        };

        let _ = tx.send(send_result);
    });

    // Await the background thread result without blocking the main thread
    rx.await.map_err(|_| "Network request cancelled".to_string())?
}
