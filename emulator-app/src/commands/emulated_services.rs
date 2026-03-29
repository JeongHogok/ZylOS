// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Emulated Services
//
// 역할: 실기기 D-Bus 시스템 서비스를 에뮬레이션
// 수행범위: notification, power, location, user, credential
// 의존방향: state (AppState, mount_point)
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
// 2. Power Service
// ════════════════════════════════════════════

#[tauri::command]
pub fn power_get_state() -> serde_json::Value {
    let battery = crate::platform::get_battery_info().unwrap_or_else(|_| {
        serde_json::json!({"level": 100, "charging": true})
    });

    serde_json::json!({
        "state": "ACTIVE",
        "brightness": 80,
        "battery": battery,
        "screenOn": true,
    })
}

#[tauri::command]
pub fn power_set_brightness(percent: u32) -> serde_json::Value {
    serde_json::json!({ "brightness": percent.min(100) })
}

// ════════════════════════════════════════════
// 3. Location Service
// ════════════════════════════════════════════

#[tauri::command]
pub fn location_get_last_known() -> serde_json::Value {
    // 가상 위치 (서울 시청)
    serde_json::json!({
        "latitude": 37.5665,
        "longitude": 126.9780,
        "altitude": 38.0,
        "accuracy": 100.0,
        "speed": 0.0,
        "bearing": 0.0,
        "timestamp": std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as u64,
        "provider": "emulator"
    })
}

// ════════════════════════════════════════════
// 4. User Service
// ════════════════════════════════════════════

#[tauri::command]
pub fn user_get_current() -> serde_json::Value {
    serde_json::json!({
        "uid": 1000,
        "name": "user",
        "type": "OWNER",
        "avatar": "",
        "dataDir": "/data/users/1000"
    })
}

#[tauri::command]
pub fn user_list() -> Vec<serde_json::Value> {
    vec![serde_json::json!({
        "uid": 1000,
        "name": "user",
        "type": "OWNER",
        "isActive": true
    })]
}

// ════════════════════════════════════════════
// 5. Credential Service
// ════════════════════════════════════════════

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

    let key = format!("{}_{}", service, account);
    let path = cred_dir.join(format!("{}.enc", key));
    // 프로토타입: 평문 저장 (실기기에서는 AES-256-GCM)
    fs::write(&path, &secret).map_err(|e| format!("Write error: {}", e))
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

    let key = format!("{}_{}", service, account);
    let path = base.join(".credentials").join(format!("{}.enc", key));
    fs::read_to_string(&path).map_err(|_| "Credential not found".into())
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

    let key = format!("{}_{}", service, account);
    let path = base.join(".credentials").join(format!("{}.enc", key));
    let _ = fs::remove_file(&path);
    Ok(())
}
