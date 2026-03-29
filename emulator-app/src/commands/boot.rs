// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: 디바이스 부팅/셧다운 오케스트레이션 Tauri 커맨드
// 수행범위: OS 이미지 마운트 + 리소스 예약 → 부팅 상태 전환 → BootInfo 반환
// 의존방향: commands/resource, state, resource/disk_image
// SOLID: SRP — 부팅 라이프사이클만 담당
// ──────────────────────────────────────────────────────────

use crate::commands::resource;
use crate::state::{AppState, BootInfo, DeviceConfig};
use std::path::PathBuf;
use std::process::Command;
use std::sync::Mutex;
use tauri::State;

/// OS 이미지(.img)를 마운트하고 마운트 포인트 반환
fn mount_os_image(img_path: &str) -> Result<String, String> {
    let path = PathBuf::from(img_path);
    if !path.exists() {
        return Err(format!("OS image not found: {}", img_path));
    }

    #[cfg(target_os = "macos")]
    {
        let output = Command::new("hdiutil")
            .args(["attach", "-nobrowse"])
            .arg(&path)
            .output()
            .map_err(|e| format!("hdiutil attach failed: {}", e))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Err(format!("Failed to mount OS image: {}", stderr));
        }

        // hdiutil 출력에서 마운트 포인트 추출
        let stdout = String::from_utf8_lossy(&output.stdout);
        for line in stdout.lines() {
            if line.contains("/Volumes/") {
                let mount = line
                    .split('\t')
                    .last()
                    .unwrap_or("")
                    .trim()
                    .to_string();
                if !mount.is_empty() {
                    log::info!("OS image mounted at: {}", mount);
                    return Ok(mount);
                }
            }
        }
        Err("Could not determine mount point from hdiutil output".into())
    }

    #[cfg(target_os = "linux")]
    {
        // Linux: udisksctl로 마운트
        let output = Command::new("udisksctl")
            .args(["loop-setup", "-f"])
            .arg(&path)
            .output()
            .map_err(|e| format!("udisksctl failed: {}", e))?;

        if !output.status.success() {
            return Err("Failed to setup loop device for OS image".into());
        }

        let stdout = String::from_utf8_lossy(&output.stdout);
        let loop_dev = stdout
            .split_whitespace()
            .find(|s| s.starts_with("/dev/loop"))
            .map(|s| s.trim_end_matches('.').to_string())
            .ok_or("Failed to parse loop device")?;

        let mount_output = Command::new("udisksctl")
            .args(["mount", "-b", &loop_dev, "--no-user-interaction"])
            .output()
            .map_err(|e| format!("udisksctl mount failed: {}", e))?;

        let mount_stdout = String::from_utf8_lossy(&mount_output.stdout);
        // "Mounted /dev/loopN at /media/user/ZylOS-0.1.0"
        let mount_point = mount_stdout
            .split(" at ")
            .nth(1)
            .map(|s| s.trim().to_string())
            .ok_or("Failed to parse mount point")?;

        log::info!("OS image mounted at: {}", mount_point);
        Ok(mount_point)
    }
}

/// OS 이미지 언마운트
fn unmount_os_image(mount_point: &str) {
    #[cfg(target_os = "macos")]
    {
        let _ = Command::new("hdiutil")
            .args(["detach", mount_point])
            .output();
    }

    #[cfg(target_os = "linux")]
    {
        let _ = Command::new("udisksctl")
            .args(["unmount", "-p", mount_point])
            .output();
    }

    log::info!("OS image unmounted: {}", mount_point);
}

/// 디바이스 부팅: OS 이미지 마운트 + 리소스 예약 → 부팅 정보 반환
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

    // 2. OS 이미지 마운트 (앱 소스)
    let os_version = config.os_version.clone();
    let data_dir = {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.data_dir.clone()
    };

    let os_img_path = data_dir
        .join("os-images")
        .join(format!("{}.img", os_version));

    let os_mount = if os_img_path.exists() {
        match mount_os_image(&os_img_path.to_string_lossy()) {
            Ok(mp) => {
                let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
                app_state.os_image_mount = Some(PathBuf::from(&mp));
                mp
            }
            Err(e) => {
                log::warn!("OS image mount failed: {}. Using bundled apps.", e);
                String::new()
            }
        }
    } else {
        log::warn!("OS image not found: {:?}. Using bundled apps.", os_img_path);
        String::new()
    };

    // 3. 사용자 데이터 리소스 예약 (스토리지 이미지)
    let resource_info = resource::reserve_resources(state.clone()).await?;

    // 4. 부팅 상태 전환
    {
        let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.booted = true;
    }

    log::info!(
        "Device booted: {} (OS v{}, {}MB RAM, {}GB storage)",
        config.profile_id,
        os_version,
        config.ram_mb,
        config.storage_gb
    );

    Ok(BootInfo {
        profile_id: config.profile_id,
        mount_point: resource_info.mount_point,
        os_image_mount: os_mount,
        ram_limit_mb: config.ram_mb,
        nav_mode: config.nav_mode.to_string(),
        screen_width: config.screen_width,
        screen_height: config.screen_height,
    })
}

/// 디바이스 셧다운: OS 이미지 언마운트 + 리소스 해제 → 상태 초기화
#[tauri::command]
pub async fn shutdown_device(state: State<'_, Mutex<AppState>>) -> Result<(), String> {
    // 1. OS 이미지 언마운트
    {
        let mut app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        if let Some(mp) = app_state.os_image_mount.take() {
            unmount_os_image(&mp.to_string_lossy());
        }
    }

    // 2. 데이터 리소스 해제
    resource::release_resources(state.clone()).await?;

    // 3. 상태 초기화
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
        "os_image_mount": app_state.os_image_mount.as_ref().map(|p| p.to_string_lossy().to_string()),
    }))
}
