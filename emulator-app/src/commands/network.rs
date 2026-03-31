// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 호스트 WiFi/Bluetooth 상태 조회, HTTP fetch 프록시
// 수행범위: macOS airport/system_profiler, Linux nmcli/bluetoothctl,
//           도메인 화이트리스트 기반 HTTP 프록시 (Rust 레벨 강제)
// 의존방향: std::process::Command
// SOLID: OCP — cfg 분기로 플랫폼 확장 / SRP — 도메인 검증 모듈 분리
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

// ════════════════════════════════════════════
// Domain whitelist — enforced at Rust command boundary.
//
// Rationale: sandbox.js can be bypassed by any caller that invokes the
// Tauri `http_fetch` command directly (e.g. via __TAURI__.invoke).
// Enforcing the whitelist here closes that bypass path.
//
// Only explicitly allowed hostnames are permitted. IP addresses (including
// loopback and RFC 1918 ranges), `localhost`, and file:// / data:// URIs
// are rejected. This prevents SSRF against the host machine.
// ════════════════════════════════════════════

/// Hostname suffix allowlist. A request is permitted iff its parsed hostname
/// equals one of these entries OR ends with ".<entry>".
const ALLOWED_DOMAINS: &[&str] = &[
    // Weather services used by Zyl OS weather app
    "wttr.in",
    "api.open-meteo.com",
    // IP geolocation used by location service
    "ipinfo.io",
    // Zyl update / app-store endpoints
    "updates.zylos.dev",
    "store.zylos.dev",
    // OpenStreetMap tile/nominatim (maps app)
    "tile.openstreetmap.org",
    "nominatim.openstreetmap.org",
];

/// Validate a URL against the domain whitelist and block unsafe targets.
/// Returns Ok(hostname) or Err(human-readable reason).
fn validate_fetch_url(url: &str) -> Result<String, String> {
    // Scheme check
    if !url.starts_with("https://") && !url.starts_with("http://") {
        return Err("Invalid URL: must start with http:// or https://".into());
    }

    // Length limit
    if url.len() > 2048 {
        return Err("URL too long (max 2048 characters)".into());
    }

    // Extract hostname from URL (simple parser — no external crate required)
    // Format: scheme://[userinfo@]host[:port]/path?query#fragment
    let after_scheme = url
        .find("://")
        .map(|i| &url[i + 3..])
        .ok_or_else(|| "Malformed URL: missing '://'".to_string())?;

    // Drop path/query/fragment
    let host_part = after_scheme
        .split('/')
        .next()
        .unwrap_or(after_scheme);

    // Drop userinfo (user:pass@host)
    let host_part = host_part
        .split('@')
        .last()
        .unwrap_or(host_part);

    // Drop port
    let hostname = if host_part.contains(':') && !host_part.starts_with('[') {
        // IPv4 with port or plain host:port
        host_part.split(':').next().unwrap_or(host_part)
    } else if host_part.starts_with('[') {
        // IPv6 literal — always blocked (see below)
        host_part
    } else {
        host_part
    };

    let hostname = hostname.to_lowercase();

    // Block loopback and localhost
    if hostname == "localhost"
        || hostname == "127.0.0.1"
        || hostname == "::1"
        || hostname.starts_with("127.")
    {
        return Err(format!("Blocked: loopback address '{}' is not permitted", hostname));
    }

    // Block IPv6 literals (prevent bypass via [::1] etc.)
    if hostname.starts_with('[') {
        return Err(format!("Blocked: IPv6 literal address '{}' is not permitted", hostname));
    }

    // Block bare IPv4 addresses (prevents SSRF against RFC1918/link-local/cloud metadata)
    if hostname.chars().all(|c| c.is_ascii_digit() || c == '.') {
        return Err(format!(
            "Blocked: IP address '{}' is not permitted. Use a hostname.",
            hostname
        ));
    }

    // Domain allowlist check
    let permitted = ALLOWED_DOMAINS.iter().any(|&allowed| {
        hostname == allowed || hostname.ends_with(&format!(".{}", allowed))
    });

    if !permitted {
        return Err(format!(
            "Domain '{}' is not on the http_fetch allowlist. \
             Contact the OS service team to add it.",
            hostname
        ));
    }

    Ok(hostname)
}

/// Fetch a URL via curl (non-blocking).
///
/// The domain whitelist is enforced at the Rust command boundary. Callers
/// that invoke this command directly (bypassing sandbox.js) still receive the
/// same domain restrictions. IP literals and loopback are blocked to prevent
/// SSRF against the host.
///
/// Runs curl in a background thread via std::thread::spawn so the Tauri main
/// thread (and therefore the entire WebView) is never blocked by slow networks.
#[tauri::command]
pub async fn http_fetch(url: String) -> Result<String, String> {
    // Validate URL before spawning any threads
    validate_fetch_url(&url)?;

    let (tx, rx) = tokio::sync::oneshot::channel();

    std::thread::spawn(move || {
        let result = Command::new("curl")
            .args([
                "-s",          // silent
                "-m", "8",     // max time 8 seconds
                "--connect-timeout", "5",
                "-L",          // follow redirects
                "--max-redirs", "3",
                "--max-filesize", "1048576", // max 1MB response
            ])
            .arg(&url)
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

    rx.await.map_err(|_| "Network request cancelled".to_string())?
}

// ────────────────────────────────────────────────────────────────────────────
// Unit tests
// ────────────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_allowed_domain_exact() {
        assert!(validate_fetch_url("https://wttr.in/Seoul").is_ok());
    }

    #[test]
    fn test_allowed_domain_subdomain() {
        assert!(validate_fetch_url("https://tile.openstreetmap.org/path").is_ok());
    }

    #[test]
    fn test_allowed_ipinfo() {
        assert!(validate_fetch_url("https://ipinfo.io/json").is_ok());
    }

    #[test]
    fn test_blocked_unlisted_domain() {
        let err = validate_fetch_url("https://evil.com/payload");
        assert!(err.is_err(), "unlisted domain must be blocked");
        let msg = err.unwrap_err();
        assert!(msg.contains("allowlist"), "should mention allowlist");
    }

    #[test]
    fn test_blocked_localhost() {
        let err = validate_fetch_url("http://localhost/admin");
        assert!(err.is_err());
        let msg = err.unwrap_err();
        assert!(msg.contains("loopback"), "should mention loopback");
    }

    #[test]
    fn test_blocked_loopback_ip() {
        let err = validate_fetch_url("http://127.0.0.1:8080/secret");
        assert!(err.is_err());
    }

    #[test]
    fn test_blocked_ipv4_literal() {
        let err = validate_fetch_url("https://192.168.1.1/router");
        assert!(err.is_err());
        let msg = err.unwrap_err();
        assert!(msg.contains("IP address"), "should mention IP address");
    }

    #[test]
    fn test_blocked_ipv6_literal() {
        let err = validate_fetch_url("http://[::1]:80/");
        assert!(err.is_err());
    }

    #[test]
    fn test_blocked_non_http_scheme() {
        let err = validate_fetch_url("file:///etc/passwd");
        assert!(err.is_err());
    }

    #[test]
    fn test_blocked_url_too_long() {
        let long_url = format!("https://wttr.in/{}", "a".repeat(2048));
        let err = validate_fetch_url(&long_url);
        assert!(err.is_err());
    }

    #[test]
    fn test_subdomain_of_unlisted_domain_blocked() {
        // sub.evil.com should NOT be allowed just because evil.com contains wttr.in as substring
        let err = validate_fetch_url("https://not-wttr.in/data");
        assert!(err.is_err(), "subdomain of unlisted domain must be blocked");
    }

    #[test]
    fn test_cloud_metadata_blocked() {
        // AWS EC2 metadata endpoint — must be blocked
        let err = validate_fetch_url("http://169.254.169.254/latest/meta-data/");
        assert!(err.is_err());
    }
}
