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

fn main() {
    env_logger::init();

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Mutex::new(AppState::new()))
        /* Store apps as their own origin, zyl-app://<id>/, as on the device */
        .register_uri_scheme_protocol("zyl-app", |ctx, request| {
            commands::store::serve(ctx.app_handle(), &request)
        })
        .invoke_handler(tauri::generate_handler![
            // Config commands
            commands::config::get_device_profiles,
            commands::config::get_host_resources,
            commands::config::save_config,
            commands::config::get_saved_devices,
            commands::config::delete_saved_device,
            // Boot commands
            commands::boot::boot_device,
            // Filesystem commands
            commands::filesystem::fs_read_dir,
            commands::filesystem::fs_read_file,
            commands::filesystem::fs_read_binary,
            commands::filesystem::fs_write_file,
            commands::filesystem::fs_mkdir,
            commands::filesystem::fs_remove,
            commands::filesystem::fs_rename,
            commands::filesystem::fs_handoff,
            commands::filesystem::fs_get_usage,
            // OS image commands
            commands::os_image::list_os_images,
            commands::os_image::scan_os_images_dir,
            commands::os_image::get_os_images_dir,
            // Each app's private data
            commands::appdata::appdata_load,
            commands::appdata::appdata_save,
            // Store apps on the virtual device
            commands::store::store_config,
            commands::push::push_config,
            commands::push::push_poll,
            commands::store::store_download,
            commands::store::store_unpack,
            commands::store::store_inspect,
            commands::store::store_revoked,
            commands::store::store_commit,
            commands::store::store_discard,
            commands::store::store_remove,
            commands::store::store_app_policy,
            // Settings commands
            commands::settings::load_settings,
            commands::settings::save_settings,
            // Network commands
            commands::network::http_fetch,
            // App registry
            commands::config::list_installed_apps,
            // Emulated system services
            commands::emulated_services::power_get_state,
            commands::emulated_services::location_get_last_known,
            commands::emulated_services::credential_store,
            commands::emulated_services::credential_lookup,
            commands::emulated_services::credential_delete,
            // WiFi HAL
            commands::emulated_services::get_wifi_networks,
            commands::emulated_services::wifi_connect,
            commands::emulated_services::wifi_disconnect,
            commands::emulated_services::wifi_get_state,
            commands::emulated_services::wifi_set_enabled,
            // Bluetooth HAL
            commands::emulated_services::get_bluetooth_devices,
            commands::emulated_services::bt_set_enabled,
            commands::emulated_services::bt_pair,
            commands::emulated_services::bt_unpair,
            commands::emulated_services::bt_connect,
            commands::emulated_services::bt_disconnect,
            commands::emulated_services::bt_get_state,
            // Battery HAL
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
