// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: 디바이스 부팅 오케스트레이션 Tauri 커맨드 (종료 정리는 main.rs 의 창 닫힘)
// 수행범위: OS 이미지 마운트 + 리소스 예약 → 부팅 상태 전환 → BootInfo 반환
// 의존방향: commands/resource, state, resource/disk_image
// SOLID: SRP — 부팅 라이프사이클만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
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

/* 앱 배포는 scripts/deploy-apps.sh (dev) 또는 빌드 스크립트 (prod)에서 수행.
   런타임에 ui/apps/로 복사하면 Tauri watcher가 재시작 루프를 발생시키므로
   런타임 복사는 하지 않는다. */

/// The virtual device's hardware ID, as a real device's chip ID: made once,
/// kept beside its disk image, gone with the device (delete_saved_device)
fn device_id(data_dir: &std::path::Path, profile_id: &str) -> String {
    let path = data_dir.join("images").join(format!("{}.id", profile_id));
    match std::fs::read_to_string(&path) {
        Ok(id) if !id.trim().is_empty() => id.trim().to_string(),
        _ => {
            let id = format!("EMU-{:016X}", rand::random::<u64>());
            if let Err(e) = std::fs::write(&path, &id) {
                log::warn!("Device ID not kept ({}): a new one next boot", e);
            }
            id
        }
    }
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
            return Err("Device already booted.".into());
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
    // 참고: 앱 배포는 scripts/deploy-apps.sh (beforeDevCommand)에서 수행.
    // 런타임에 ui/apps/로 복사하면 Tauri watcher가 재시작 루프를 발생시킴.
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

    let device_id = device_id(&data_dir, &config.profile_id);
    Ok(BootInfo {
        profile_id: config.profile_id,
        mount_point: resource_info.mount_point,
        os_image_mount: os_mount,
        ram_limit_mb: config.ram_mb,
        nav_mode: config.nav_mode.to_string(),
        screen_width: config.screen_width,
        screen_height: config.screen_height,
        device_id,
    })
}
