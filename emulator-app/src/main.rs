// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Entry Point
//
// 역할: Tauri 앱 부트스트랩 및 커맨드 등록
// 수행범위: 상태 초기화, 커맨드 등록, 종료 시 리소스 정리
// 의존방향: state, commands/*, resource/*, platform/*
// SOLID: DIP — 커맨드 모듈들을 조합만 함, 구현에 의존하지 않음
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod platform;
mod resource;
mod state;

use state::AppState;
use std::sync::Mutex;
use tauri::Manager;

/// 배터리 상태 조회 (HAL 커맨드)
#[tauri::command]
fn get_battery_state() -> Result<serde_json::Value, String> {
    platform::get_battery_info()
}

/// 플랫폼 정보 조회
#[tauri::command]
fn get_platform_info() -> serde_json::Value {
    serde_json::json!({
        "platform": platform::platform_name(),
        "arch": std::env::consts::ARCH,
    })
}

fn main() {
    env_logger::init();

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Mutex::new(AppState::new()))
        .invoke_handler(tauri::generate_handler![
            // Config commands
            commands::config::get_device_profiles,
            commands::config::get_storage_options,
            commands::config::get_ram_options,
            commands::config::get_host_resources,
            commands::config::validate_config,
            commands::config::save_config,
            commands::config::get_saved_devices,
            commands::config::delete_saved_device,
            // Resource commands
            commands::resource::reserve_resources,
            commands::resource::release_resources,
            // Boot commands
            commands::boot::boot_device,
            commands::boot::shutdown_device,
            commands::boot::get_boot_status,
            // Filesystem commands
            commands::filesystem::fs_read_dir,
            commands::filesystem::fs_read_file,
            commands::filesystem::fs_write_file,
            commands::filesystem::fs_mkdir,
            commands::filesystem::fs_remove,
            commands::filesystem::fs_rename,
            commands::filesystem::fs_get_usage,
            // Terminal command
            commands::terminal::exec_command,
            // OS image commands
            commands::os_image::list_os_images,
            commands::os_image::scan_os_images_dir,
            commands::os_image::create_os_image,
            commands::os_image::delete_os_image,
            commands::os_image::get_os_images_dir,
            // Settings commands
            commands::settings::load_settings,
            commands::settings::save_settings,
            // Network commands
            commands::network::get_wifi_networks,
            commands::network::get_bluetooth_devices,
            // App registry
            commands::config::list_installed_apps,
            // HAL commands
            get_battery_state,
            get_platform_info,
        ])
        .setup(|_app| {
            log::info!("Zyl OS Emulator started");
            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::Destroyed = event {
                // 마운트된 디스크 이미지 해제
                if let Some(state) = window.try_state::<Mutex<AppState>>() {
                    if let Ok(mut app_state) = state.lock() {
                        // OS 이미지 언마운트
                        if let Some(os_mp) = app_state.os_image_mount.take() {
                            #[cfg(target_os = "macos")]
                            { let _ = std::process::Command::new("hdiutil").args(["detach", &os_mp.to_string_lossy()]).output(); }
                            #[cfg(target_os = "linux")]
                            { let _ = std::process::Command::new("udisksctl").args(["unmount", "-p", &os_mp.to_string_lossy()]).output(); }
                        }
                        // 데이터 이미지 언마운트
                        if let Some(mp) = app_state.mount_point.take() {
                            let _ = resource::disk_image::unmount_image(&mp);
                        }
                        app_state.booted = false;
                        app_state.resource_reserved = false;
                    }
                }
                // 메모리 제한 해제
                let _ = resource::memory_limit::release_memory_limit();
            }
        })
        .run(tauri::generate_context!())
        .unwrap_or_else(|e| {
            log::error!("Failed to start Zyl OS Emulator: {}", e);
            std::process::exit(1);
        });
}
