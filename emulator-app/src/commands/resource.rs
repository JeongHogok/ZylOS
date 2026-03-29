// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: 리소스 예약/해제 오케스트레이션 Tauri 커맨드
// 수행범위: 디스크 이미지 생성+마운트, 메모리 제한 적용/해제
// 의존방향: resource/disk_image, resource/memory_limit, state
// SOLID: SRP — 리소스 라이프사이클만 담당
// ──────────────────────────────────────────────────────────

use crate::resource::{disk_image, memory_limit};
use crate::state::{AppState, ResourceInfo};
use std::sync::Mutex;
use tauri::State;

/// 리소스 예약 (디스크 이미지 + 메모리 제한)
#[tauri::command]
pub async fn reserve_resources(
    state: State<'_, Mutex<AppState>>,
) -> Result<ResourceInfo, String> {
    let (data_dir, config) = {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        let config = app_state
            .config
            .clone()
            .ok_or("No device config set")?;
        (app_state.data_dir.clone(), config)
    };

    // 1. 디스크 이미지 생성
    let image_path = disk_image::create_image(&data_dir, &config.profile_id, config.storage_gb)?;

    // 2. 디스크 이미지 마운트
    let mount_point = disk_image::mount_image(&data_dir, &config.profile_id, &image_path)?;

    // 3. 메모리 제한 적용 (실패해도 계속 진행)
    if let Err(e) = memory_limit::apply_memory_limit(config.ram_mb) {
        log::warn!("Memory limit not applied (non-fatal): {}", e);
    }

    // 4. 사용량 조회
    let (total, used, available) = disk_image::get_usage(&mount_point).unwrap_or((
        (config.storage_gb as u64) * 1024 * 1024 * 1024,
        0,
        (config.storage_gb as u64) * 1024 * 1024 * 1024,
    ));

    // 5. 상태 업데이트
    {
        let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.disk_image_path = Some(image_path.clone());
        app_state.mount_point = Some(mount_point.clone());
        app_state.resource_reserved = true;
    }

    Ok(ResourceInfo {
        mount_point: mount_point.to_string_lossy().into(),
        disk_image_path: image_path.to_string_lossy().into(),
        ram_limit_mb: config.ram_mb,
        storage_total_bytes: total,
        storage_available_bytes: available,
        storage_used_bytes: used,
    })
}

/// 리소스 해제 (언마운트 + 메모리 제한 해제)
#[tauri::command]
pub async fn release_resources(state: State<'_, Mutex<AppState>>) -> Result<(), String> {
    let mount_point = {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.mount_point.clone()
    };

    // 1. 디스크 이미지 언마운트
    if let Some(mp) = &mount_point {
        disk_image::unmount_image(mp)?;
    }

    // 2. 메모리 제한 해제
    let _ = memory_limit::release_memory_limit();

    // 3. 상태 업데이트
    {
        let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.mount_point = None;
        app_state.resource_reserved = false;
    }

    Ok(())
}
