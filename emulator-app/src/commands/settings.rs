// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: 에뮬레이터 설정 영속화 (JSON 파일 기반)
// 수행범위: 가상 디바이스의 settings.json 읽기/쓰기
// 의존방향: state (mount_point 또는 data_dir)
// SOLID: SRP — 설정 영속화만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::state::AppState;
use std::fs;
use std::sync::Mutex;
use tauri::State;

/// 설정 파일 경로 결정 (마운트 포인트 또는 data_dir 폴백)
fn settings_path(app_state: &AppState) -> std::path::PathBuf {
    if let Some(mp) = &app_state.mount_point {
        mp.join("settings.json")
    } else {
        app_state.data_dir.join("settings.json")
    }
}

/// 전체 설정 로드
#[tauri::command]
pub fn load_settings(
    state: State<'_, Mutex<AppState>>,
) -> Result<serde_json::Value, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let path = settings_path(&app_state);

    if !path.exists() {
        return Ok(default_settings());
    }

    let content = fs::read_to_string(&path)
        .map_err(|e| format!("Read settings error: {}", e))?;

    serde_json::from_str(&content)
        .map_err(|e| format!("Parse settings error: {}", e))
}

/// 개별 설정 저장
#[tauri::command]
pub fn save_settings(
    category: String,
    key: String,
    value: serde_json::Value,
    state: State<'_, Mutex<AppState>>,
) -> Result<serde_json::Value, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let path = settings_path(&app_state);

    // 기존 설정 로드
    let mut settings = if path.exists() {
        let content = fs::read_to_string(&path).unwrap_or_else(|_| "{}".into());
        serde_json::from_str::<serde_json::Value>(&content).unwrap_or_else(|_| default_settings())
    } else {
        default_settings()
    };

    // 카테고리 하위에 키-값 설정
    if let Some(obj) = settings.as_object_mut() {
        let cat = obj
            .entry(&category)
            .or_insert_with(|| serde_json::json!({}));
        if let Some(cat_obj) = cat.as_object_mut() {
            cat_obj.insert(key.clone(), value.clone());
        }
    }

    // 디스크에 저장
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let json = serde_json::to_string_pretty(&settings)
        .map_err(|e| format!("Serialize error: {}", e))?;
    fs::write(&path, json).map_err(|e| format!("Write error: {}", e))?;

    log::info!("Settings saved: {}.{}", category, key);

    // 업데이트된 카테고리 반환
    Ok(settings
        .get(&category)
        .cloned()
        .unwrap_or(serde_json::json!({})))
}

/// 기본 설정
fn default_settings() -> serde_json::Value {
    serde_json::json!({
        "wifi": { "enabled": true },
        "bluetooth": { "enabled": true },
        "display": {
            "brightness": 80,
            "darkMode": true,
            "autoBrightness": true,
            "fontSize": "medium"
        },
        "sound": {
            "mediaVolume": 70,
            "notifVolume": 80,
            "alarmVolume": 90,
            "systemVolume": 50,
            "ringtoneVolume": 80,
            "vibration": true,
            "silentMode": false
        },
        "security": {
            "lockType": "swipe",
            "pin": "",
            "fingerprint": false
        },
        "wallpaper": {
            "current": "default",
            "options": ["default", "blue", "purple", "dark", "sunset"]
        }
    })
}
