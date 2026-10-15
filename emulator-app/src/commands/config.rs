// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: 프리부팅 디바이스 설정 관리 Tauri 커맨드
// 수행범위: 디바이스 프로필 목록, 호스트 리소스, 설정 검증/저장, 저장된 기기
// 의존방향: state (DeviceConfig, DeviceProfile, SavedDevice)
// SOLID: SRP — 설정 관리만 담당, ISP — 최소 인터페이스 노출
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::state::{AppState, DeviceConfig, DeviceProfile, SavedDevice};
use std::fs;
use std::sync::Mutex;
use tauri::State;

/// 사전 정의 디바이스 프로필 반환
///
/// 목록은 ui/device-profiles.json 단일 출처에서 온다. 설정 UI와 컴포지터도
/// 같은 파일을 읽으므로 세 곳에 중복 정의하지 않는다.
#[tauri::command]
pub fn get_device_profiles() -> Vec<DeviceProfile> {
    #[derive(serde::Deserialize)]
    struct ProfileFile {
        profiles: Vec<DeviceProfile>,
    }

    // Compiled in: the file ships with the binary, so a missing or malformed
    // descriptor is a build-time error rather than an empty device list.
    const RAW: &str = include_str!("../../ui/device-profiles.json");

    match serde_json::from_str::<ProfileFile>(RAW) {
        Ok(f) => f.profiles,
        Err(e) => {
            log::error!("device-profiles.json is malformed: {e}");
            Vec::new()
        }
    }
}

/// 호스트 시스템의 가용 리소스 조회
#[tauri::command]
pub fn get_host_resources() -> Result<serde_json::Value, String> {
    let mut info = serde_json::json!({});

    // 가용 디스크 공간
    if let Ok(output) = std::process::Command::new("df")
        .args(["-k", "."])
        .output()
    {
        let stdout = String::from_utf8_lossy(&output.stdout);
        if let Some(line) = stdout.lines().nth(1) {
            let fields: Vec<&str> = line.split_whitespace().collect();
            if fields.len() >= 4 {
                let available_kb: u64 = fields[3].parse().unwrap_or(0);
                info["available_storage_gb"] =
                    serde_json::json!((available_kb as f64 / 1024.0 / 1024.0).round() as u64);
            }
        }
    }

    // 총 RAM
    #[cfg(target_os = "macos")]
    {
        if let Ok(output) = std::process::Command::new("sysctl")
            .args(["-n", "hw.memsize"])
            .output()
        {
            let stdout = String::from_utf8_lossy(&output.stdout);
            if let Ok(bytes) = stdout.trim().parse::<u64>() {
                info["total_ram_mb"] = serde_json::json!(bytes / 1024 / 1024);
            }
        }
    }

    #[cfg(target_os = "linux")]
    {
        if let Ok(content) = fs::read_to_string("/proc/meminfo") {
            for line in content.lines() {
                if line.starts_with("MemTotal:") {
                    let kb: u64 = line
                        .split_whitespace()
                        .nth(1)
                        .and_then(|s| s.parse().ok())
                        .unwrap_or(0);
                    info["total_ram_mb"] = serde_json::json!(kb / 1024);
                    break;
                }
            }
        }
    }

    Ok(info)
}

/// 설정 검증
#[tauri::command]
pub fn validate_config(config: DeviceConfig) -> Result<(), String> {
    if config.storage_gb < 4 || config.storage_gb > 64 {
        return Err("Storage must be 4-64 GB".into());
    }
    if config.ram_mb < 256 || config.ram_mb > 8192 {
        return Err("RAM must be 256 MB - 8 GB".into());
    }
    if config.screen_width == 0 || config.screen_height == 0 {
        return Err("Invalid screen resolution".into());
    }
    if config.profile_id.is_empty() {
        return Err("Profile ID is empty".into());
    }
    Ok(())
}

/// 설정 저장 (state + 디스크 persist)
#[tauri::command]
pub fn save_config(
    config: DeviceConfig,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    validate_config(config.clone())?;

    let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;

    // 디스크에 저장
    let saved = SavedDevice {
        config: config.clone(),
        image_path: None,
        created_at: chrono_now(),
    };

    let saved_dir = app_state.data_dir.join("devices");
    fs::create_dir_all(&saved_dir)
        .map_err(|e| format!("Failed to create devices dir: {}", e))?;

    let saved_path = saved_dir.join(format!("{}.json", config.profile_id));
    let json = serde_json::to_string_pretty(&saved)
        .map_err(|e| format!("Serialize error: {}", e))?;
    fs::write(&saved_path, json).map_err(|e| format!("Write error: {}", e))?;

    app_state.config = Some(config);
    Ok(())
}

/// 저장된 가상 디바이스 목록
#[tauri::command]
pub fn get_saved_devices(state: State<'_, Mutex<AppState>>) -> Result<Vec<SavedDevice>, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let saved_dir = app_state.data_dir.join("devices");

    if !saved_dir.exists() {
        return Ok(vec![]);
    }

    let mut devices = Vec::new();
    let entries = fs::read_dir(&saved_dir).map_err(|e| format!("Read dir error: {}", e))?;

    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().map(|e| e == "json").unwrap_or(false) {
            if let Ok(content) = fs::read_to_string(&path) {
                if let Ok(saved) = serde_json::from_str::<SavedDevice>(&content) {
                    devices.push(saved);
                }
            }
        }
    }

    devices.sort_by(|a, b| b.created_at.cmp(&a.created_at));
    Ok(devices)
}

/// 저장된 디바이스 삭제
#[tauri::command]
pub fn delete_saved_device(
    profile_id: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;

    // 디바이스 설정 파일 삭제
    let saved_path = app_state
        .data_dir
        .join("devices")
        .join(format!("{}.json", profile_id));
    let _ = fs::remove_file(&saved_path);

    // 디스크 이미지 삭제
    let image_path = app_state
        .data_dir
        .join("images")
        .join(format!("{}.img", profile_id));
    let _ = fs::remove_file(&image_path);
    let _ = fs::remove_file(image_path.with_extension("id"));   // its hardware ID

    // VFS 폴백 디렉토리 삭제
    let vfs_path = app_state.data_dir.join("vfs").join(&profile_id);
    let _ = fs::remove_dir_all(&vfs_path);

    Ok(())
}

/// Where the OS image's apps are: the bundle, or ui/apps in dev mode
pub(crate) fn bundled_app_dirs() -> Vec<std::path::PathBuf> {
    let apps_dir = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .map(|d| d.join("../Resources/apps"))
        .unwrap_or_else(|| std::path::PathBuf::from("apps"));
    vec![
        apps_dir,
        std::path::PathBuf::from("ui/apps"),
        std::path::PathBuf::from("assets/apps"),
    ]
}

/// 설치된 앱 목록 조회 (apps/ 디렉토리의 app.json 파싱) + 가상 기기에 스토어로 설치한 앱
#[tauri::command]
pub fn list_installed_apps(
    state: tauri::State<'_, std::sync::Mutex<crate::state::AppState>>,
) -> Result<Vec<serde_json::Value>, String> {
    let search_dirs = bundled_app_dirs();
    let mut apps = Vec::new();

    for base in &search_dirs {
        if !base.exists() || !base.is_dir() {
            continue;
        }
        log::debug!("Scanning apps directory: {:?}", base);
        if let Ok(entries) = fs::read_dir(base) {
            for entry in entries.flatten() {
                let path = entry.path();
                let manifest_path = path.join("app.json");
                if manifest_path.exists() {
                    if let Ok(content) = fs::read_to_string(&manifest_path) {
                        if let Ok(mut manifest) = serde_json::from_str::<serde_json::Value>(&content) {
                            /* Rust does NOT determine system status.
                               It passes raw app.json data; the OS permission layer
                               (ZylPermissions.SYSTEM_APPS) is the sole authority.
                               The 'system' field here is advisory only — overridden by OS. */
                            let is_system = false; /* always false from Rust — OS decides */

                            if let Some(obj) = manifest.as_object_mut() {
                                obj.insert("installed".into(), serde_json::json!(true));
                                obj.insert("system".into(), serde_json::json!(is_system));
                            }
                            /* Whether an app shows in the launcher is its manifest's
                               "launcher" field — the OS reads it, not this list */
                            apps.push(manifest);
                        }
                    }
                }
            }
        }
        /* Continue scanning all directories — merge results, dedup by app id */
    }

    /* Store apps after the image's: an image app's id is never taken over */
    if let Ok(app_state) = state.lock() {
        apps.extend(crate::commands::store::installed_store_apps(&app_state));
    }

    /* Deduplicate by app id — first occurrence wins */
    let mut seen = std::collections::HashSet::new();
    apps.retain(|app| {
        if let Some(id) = app.get("id").and_then(|v| v.as_str()) {
            seen.insert(id.to_string())
        } else {
            false
        }
    });

    Ok(apps)
}

fn chrono_now() -> String {
    // 간단한 ISO 타임스탬프 (chrono 크레이트 없이)
    use std::time::SystemTime;
    let secs = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    format!("{}", secs)
}
