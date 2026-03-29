// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: 디바이스 부팅/셧다운 오케스트레이션 Tauri 커맨드
// 수행범위: 리소스 예약 → 부팅 상태 전환 → BootInfo 반환
// 의존방향: commands/resource, state
// SOLID: SRP — 부팅 라이프사이클만 담당
// ──────────────────────────────────────────────────────────

use crate::commands::resource;
use crate::state::{AppState, BootInfo, DeviceConfig};
use std::sync::Mutex;
use tauri::State;

/// 디바이스 부팅: 설정 저장 → 리소스 예약 → 부팅 정보 반환
#[tauri::command]
pub async fn boot_device(
    config: DeviceConfig,
    state: State<'_, Mutex<AppState>>,
) -> Result<BootInfo, String> {
    // 0. 이중 부팅 방지
    {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        if app_state.booted {
            return Err("Device already booted. Call shutdown_device first.".into());
        }
    }

    // 1. 설정 저장
    {
        let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.config = Some(config.clone());
    }

    // 2. 리소스 예약
    let resource_info = resource::reserve_resources(state.clone()).await?;

    // 3. 부팅 상태 전환
    {
        let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.booted = true;
    }

    log::info!(
        "Device booted: {} ({}MB RAM, {}GB storage)",
        config.profile_id,
        config.ram_mb,
        config.storage_gb
    );

    Ok(BootInfo {
        profile_id: config.profile_id,
        mount_point: resource_info.mount_point,
        ram_limit_mb: config.ram_mb,
        nav_mode: config.nav_mode.to_string(),
        screen_width: config.screen_width,
        screen_height: config.screen_height,
    })
}

/// 디바이스 셧다운: 리소스 해제 → 상태 초기화
#[tauri::command]
pub async fn shutdown_device(state: State<'_, Mutex<AppState>>) -> Result<(), String> {
    // 1. 리소스 해제
    resource::release_resources(state.clone()).await?;

    // 2. 상태 초기화
    {
        let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.booted = false;
    }

    log::info!("Device shut down");
    Ok(())
}

/// 현재 부팅 상태 조회
#[tauri::command]
pub fn get_boot_status(state: State<'_, Mutex<AppState>>) -> Result<serde_json::Value, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;

    Ok(serde_json::json!({
        "booted": app_state.booted,
        "resource_reserved": app_state.resource_reserved,
        "has_config": app_state.config.is_some(),
        "mount_point": app_state.mount_point.as_ref().map(|p| p.to_string_lossy().to_string()),
    }))
}
