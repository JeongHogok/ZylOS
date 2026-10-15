// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 앱별 비공개 데이터 파일 (가상 기기의 /data/appdata/<앱 id>/ 대응)
// 수행범위: <마운트>/.system/appdata/<앱 id>/appdata.json 통째로 읽기·쓰기.
//           .system 은 파일 서비스가 열지 않는 경로라 다른 앱이 읽을 수 없다.
//           누가 부르는지·키·한도는 OS(apps/system/services/appdata.js)의 몫
// 의존방향: state (mount_point 또는 data_dir), settings::atomic_write
// SOLID: SRP — 앱 컨테이너 파일 입출력만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::commands::settings::atomic_write;
use crate::state::AppState;
use std::fs;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::State;

/// An app id names a directory: letters, digits, . _ - only, no "." or ".."
pub(crate) fn container(app_state: &AppState, app_id: &str) -> Result<PathBuf, String> {
    let ok = !app_id.is_empty()
        && app_id.len() <= 128
        && app_id != "."
        && !app_id.contains("..")
        && app_id.chars().all(|c| c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-');
    if !ok {
        return Err(format!("Invalid app id: {}", app_id));
    }
    let root = app_state.mount_point.clone().unwrap_or_else(|| app_state.data_dir.clone());
    Ok(root.join(".system").join("appdata").join(app_id))
}

/// The app's data as stored, or "" when it has none
#[tauri::command]
pub fn appdata_load(app_id: String, state: State<'_, Mutex<AppState>>) -> Result<String, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let path = container(&app_state, &app_id)?.join("appdata.json");
    Ok(fs::read_to_string(path).unwrap_or_default())
}

#[tauri::command]
pub fn appdata_save(app_id: String, data: String, state: State<'_, Mutex<AppState>>) -> Result<bool, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let dir = container(&app_state, &app_id)?;
    fs::create_dir_all(&dir).map_err(|e| format!("Create error: {}", e))?;
    atomic_write(&dir.join("appdata.json"), &data)?;
    Ok(true)
}
