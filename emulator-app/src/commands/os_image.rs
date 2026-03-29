// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: OS 이미지 관리 (.img 디스크 이미지 기반)
// 수행범위: .img 스캔/생성/마운트/삭제, 이미지에 앱 번들 주입
// 의존방향: state (data_dir), resource/disk_image (마운트)
// SOLID: SRP — OS 이미지 라이프사이클만 담당
// ──────────────────────────────────────────────────────────

use crate::state::AppState;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Mutex;
use tauri::State;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OsImage {
    pub version: String,
    pub label: String,
    pub description: String,
    pub path: String,
    pub size_bytes: u64,
    pub format: String,
}

/// 버전 문자열 유효성 검증
fn validate_version(version: &str) -> Result<(), String> {
    if version.is_empty() || version.len() > 32 {
        return Err("Version must be 1-32 characters".into());
    }
    if !version
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '.' || c == '-')
    {
        return Err("Version contains invalid characters".into());
    }
    Ok(())
}

/// OS 이미지 기본 디렉토리
fn images_dir(data_dir: &Path) -> PathBuf {
    let dir = data_dir.join("os-images");
    let _ = fs::create_dir_all(&dir);
    dir
}

/// .img 파일에서 메타데이터 추출
fn image_meta_from_file(img_path: &Path) -> Option<OsImage> {
    let filename = img_path.file_name()?.to_string_lossy().to_string();
    if !filename.ends_with(".img") {
        return None;
    }

    let version = filename.trim_end_matches(".img").to_string();
    let size = img_path.metadata().ok().map(|m| m.len()).unwrap_or(0);

    // 같은 디렉토리에 {version}.json 메타 파일이 있으면 읽기
    let meta_path = img_path.with_extension("json");
    if meta_path.exists() {
        if let Ok(content) = fs::read_to_string(&meta_path) {
            if let Ok(mut meta) = serde_json::from_str::<OsImage>(&content) {
                meta.path = img_path.to_string_lossy().into();
                meta.size_bytes = size;
                return Some(meta);
            }
        }
    }

    Some(OsImage {
        version: version.clone(),
        label: format!("v{}", version),
        description: format!("{}", format_bytes(size)),
        path: img_path.to_string_lossy().into(),
        size_bytes: size,
        format: "img".into(),
    })
}

fn format_bytes(bytes: u64) -> String {
    if bytes == 0 {
        return "0 B".into();
    }
    let units = ["B", "KB", "MB", "GB"];
    let i = (bytes as f64).log(1024.0).floor() as usize;
    let i = i.min(units.len() - 1);
    format!("{:.1} {}", bytes as f64 / 1024_f64.powi(i as i32), units[i])
}

/// 지정 경로에서 .img 파일 스캔
#[tauri::command]
pub fn scan_os_images_dir(dir_path: String) -> Result<Vec<OsImage>, String> {
    let dir = PathBuf::from(&dir_path);
    if !dir.exists() || !dir.is_dir() {
        return Err(format!("Directory not found: {}", dir_path));
    }

    let mut images = Vec::new();

    if let Ok(entries) = fs::read_dir(&dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_file() {
                if let Some(meta) = image_meta_from_file(&path) {
                    images.push(meta);
                }
            }
        }
    }

    images.sort_by(|a, b| b.version.cmp(&a.version));
    Ok(images)
}

/// 기본 디렉토리에서 .img 파일 스캔
#[tauri::command]
pub fn list_os_images(
    state: State<'_, Mutex<AppState>>,
) -> Result<Vec<OsImage>, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let img_dir = images_dir(&app_state.data_dir);
    scan_os_images_dir(img_dir.to_string_lossy().into())
}

/// 현재 앱 번들로 .img 파일 생성
#[tauri::command]
pub async fn create_os_image(
    version: String,
    description: String,
    size_mb: u32,
    state: State<'_, Mutex<AppState>>,
) -> Result<OsImage, String> {
    validate_version(&version)?;

    let data_dir = {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state.data_dir.clone()
    };

    let img_dir = images_dir(&data_dir);
    let img_path = img_dir.join(format!("{}.img", version));

    if img_path.exists() {
        return Err(format!("Image v{} already exists", version));
    }

    let size_mb = if size_mb == 0 { 64 } else { size_mb };

    // 1. sparse 이미지 생성
    #[cfg(target_os = "macos")]
    {
        let status = Command::new("hdiutil")
            .args([
                "create",
                "-size",
                &format!("{}m", size_mb),
                "-fs",
                "HFS+",
                "-volname",
                &format!("ZylOS-{}", version),
                "-o",
            ])
            .arg(&img_path)
            .status()
            .map_err(|e| format!("hdiutil create failed: {}", e))?;

        if !status.success() {
            return Err("Failed to create disk image".into());
        }

        // hdiutil은 .dmg를 추가할 수 있음
        let dmg_path = img_path.with_extension("img.dmg");
        if dmg_path.exists() && !img_path.exists() {
            fs::rename(&dmg_path, &img_path)
                .map_err(|e| format!("Rename failed: {}", e))?;
        }
    }

    #[cfg(target_os = "linux")]
    {
        let size_bytes = (size_mb as u64) * 1024 * 1024;
        let status = Command::new("dd")
            .args([
                "if=/dev/zero",
                &format!("of={}", img_path.display()),
                "bs=1",
                "count=0",
                &format!("seek={}", size_bytes),
            ])
            .status()
            .map_err(|e| format!("dd failed: {}", e))?;

        if !status.success() {
            return Err("Failed to create sparse image".into());
        }

        let status = Command::new("mkfs.ext4")
            .args(["-F", "-q"])
            .arg(&img_path)
            .status()
            .map_err(|e| format!("mkfs.ext4 failed: {}", e))?;

        if !status.success() {
            let _ = fs::remove_file(&img_path);
            return Err("Failed to format image".into());
        }
    }

    // 2. 메타데이터 저장
    let meta = OsImage {
        version: version.clone(),
        label: format!("v{}", version),
        description: description.clone(),
        path: img_path.to_string_lossy().into(),
        size_bytes: fs::metadata(&img_path).map(|m| m.len()).unwrap_or(0),
        format: "img".into(),
    };

    let meta_path = img_path.with_extension("json");
    let json = serde_json::to_string_pretty(&meta)
        .map_err(|e| format!("Serialize error: {}", e))?;
    fs::write(&meta_path, json).map_err(|e| format!("Write meta error: {}", e))?;

    log::info!("OS image created: v{} at {:?}", version, img_path);
    Ok(meta)
}

/// OS 이미지 삭제
#[tauri::command]
pub fn delete_os_image(
    version: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    validate_version(&version)?;

    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let img_dir = images_dir(&app_state.data_dir);

    let img_path = img_dir.join(format!("{}.img", version));
    let meta_path = img_dir.join(format!("{}.json", version));

    if img_path.exists() {
        fs::remove_file(&img_path).map_err(|e| format!("Delete image failed: {}", e))?;
    }
    if meta_path.exists() {
        let _ = fs::remove_file(&meta_path);
    }

    log::info!("OS image deleted: v{}", version);
    Ok(())
}

/// OS 이미지 기본 디렉토리 경로 반환
#[tauri::command]
pub fn get_os_images_dir(state: State<'_, Mutex<AppState>>) -> Result<String, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let dir = images_dir(&app_state.data_dir);
    Ok(dir.to_string_lossy().into())
}
