// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: 프리부팅 디바이스 설정 관리 Tauri 커맨드
// 수행범위: 디바이스 프로필 목록, 리소스 옵션, 설정 검증/저장
// 의존방향: state (DeviceConfig, DeviceProfile, SavedDevice)
// SOLID: SRP — 설정 관리만 담당, ISP — 최소 인터페이스 노출
// ──────────────────────────────────────────────────────────

use crate::state::{AppState, DeviceConfig, DeviceProfile, NavMode, SavedDevice};
use std::fs;
use std::sync::Mutex;
use tauri::State;

/// 사전 정의 디바이스 프로필 반환
#[tauri::command]
pub fn get_device_profiles() -> Vec<DeviceProfile> {
    vec![
        DeviceProfile {
            id: "zyl-f3-gesture".into(),
            name: "BPI-F3 Gesture".into(),
            description: "iOS 스타일 제스처 네비게이션".into(),
            soc: "SpacemiT K1 (8x X60)".into(),
            ram_options: vec![512, 1024, 2048, 4096],
            screen_width: 1080,
            screen_height: 2400,
            screen_label: "1080×2400".into(),
            nav_mode: NavMode::Gesture,
            has_notch: true,
            frame_width: 393,
            frame_height: 852,
            frame_radius: 52,
        },
        DeviceProfile {
            id: "zyl-f3-softkeys".into(),
            name: "BPI-F3 Lite".into(),
            description: "3버튼 소프트키 네비게이션".into(),
            soc: "SpacemiT K1 (8x X60)".into(),
            ram_options: vec![512, 1024, 2048],
            screen_width: 1080,
            screen_height: 2340,
            screen_label: "1080×2340".into(),
            nav_mode: NavMode::Softkeys,
            has_notch: false,
            frame_width: 393,
            frame_height: 852,
            frame_radius: 44,
        },
        DeviceProfile {
            id: "zyl-f3-hardware".into(),
            name: "BPI-F3 Classic".into(),
            description: "하드웨어 버튼 네비게이션".into(),
            soc: "SpacemiT K1 (8x X60)".into(),
            ram_options: vec![512, 1024],
            screen_width: 720,
            screen_height: 1280,
            screen_label: "720×1280".into(),
            nav_mode: NavMode::Hardware,
            has_notch: false,
            frame_width: 380,
            frame_height: 820,
            frame_radius: 36,
        },
    ]
}

/// 저장공간 옵션 반환
#[tauri::command]
pub fn get_storage_options() -> Vec<serde_json::Value> {
    vec![
        serde_json::json!({"value": 4, "label": "4 GB", "description": "최소 (앱 몇 개)"}),
        serde_json::json!({"value": 8, "label": "8 GB", "description": "표준"}),
        serde_json::json!({"value": 16, "label": "16 GB", "description": "권장"}),
        serde_json::json!({"value": 32, "label": "32 GB", "description": "대용량"}),
    ]
}

/// RAM 옵션 반환
#[tauri::command]
pub fn get_ram_options() -> Vec<serde_json::Value> {
    vec![
        serde_json::json!({"value": 512, "label": "512 MB", "description": "최소 (테스트용)"}),
        serde_json::json!({"value": 1024, "label": "1 GB", "description": "기본"}),
        serde_json::json!({"value": 2048, "label": "2 GB", "description": "권장"}),
        serde_json::json!({"value": 4096, "label": "4 GB", "description": "고성능"}),
    ]
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
        return Err("저장공간은 4~64GB 범위여야 합니다".into());
    }
    if config.ram_mb < 256 || config.ram_mb > 8192 {
        return Err("RAM은 256MB~8GB 범위여야 합니다".into());
    }
    if config.screen_width == 0 || config.screen_height == 0 {
        return Err("화면 해상도가 유효하지 않습니다".into());
    }
    if config.profile_id.is_empty() {
        return Err("프로필 ID가 비어있습니다".into());
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

    // VFS 폴백 디렉토리 삭제
    let vfs_path = app_state.data_dir.join("vfs").join(&profile_id);
    let _ = fs::remove_dir_all(&vfs_path);

    Ok(())
}

/// 설치된 앱 목록 조회 (apps/ 디렉토리의 app.json 파싱)
#[tauri::command]
pub fn list_installed_apps() -> Result<Vec<serde_json::Value>, String> {
    // 번들된 앱 디렉토리에서 app.json 스캔
    let apps_dir = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .map(|d| d.join("../Resources/apps"))
        .unwrap_or_else(|| std::path::PathBuf::from("apps"));

    // dev 모드 폴백: ui/apps/ 디렉토리
    let search_dirs = vec![
        apps_dir.clone(),
        std::path::PathBuf::from("ui/apps"),
        std::path::PathBuf::from("assets/apps"),
    ];

    let mut apps = Vec::new();

    for base in &search_dirs {
        if !base.exists() {
            continue;
        }
        if let Ok(entries) = fs::read_dir(base) {
            for entry in entries.flatten() {
                let path = entry.path();
                let manifest_path = path.join("app.json");
                if manifest_path.exists() {
                    if let Ok(content) = fs::read_to_string(&manifest_path) {
                        if let Ok(mut manifest) = serde_json::from_str::<serde_json::Value>(&content) {
                            // 시스템 전용 앱은 홈 그리드에서 제외
                            let app_role = manifest.get("role").and_then(|v| v.as_str()).unwrap_or("");
                            let app_id = manifest.get("id").and_then(|v| v.as_str()).unwrap_or("");
                            let is_system_only = app_role == "system"
                                || app_id == "com.zylos.lockscreen"
                                || app_id == "com.zylos.statusbar"
                                || app_id == "com.zylos.oobe";

                            if let Some(obj) = manifest.as_object_mut() {
                                obj.insert("installed".into(), serde_json::json!(true));
                                obj.insert("system".into(), serde_json::json!(is_system_only));
                            }
                            if !is_system_only {
                                apps.push(manifest);
                            }
                        }
                    }
                }
            }
        }
        if !apps.is_empty() {
            break; // 첫 번째 유효한 디렉토리에서 발견하면 중단
        }
    }

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
