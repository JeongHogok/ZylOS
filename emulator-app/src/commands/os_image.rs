// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - UseCase
//
// 역할: OS 이미지 관리 (.img 디스크 이미지 기반)
// 수행범위: .img 스캔·목록, 이미지 디렉토리
// 의존방향: state (data_dir), resource/disk_image (마운트)
// SOLID: SRP — OS 이미지 라이프사이클만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::state::AppState;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
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

/// OS 이미지 기본 디렉토리 경로 반환
#[tauri::command]
pub fn get_os_images_dir(state: State<'_, Mutex<AppState>>) -> Result<String, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let dir = images_dir(&app_state.data_dir);
    Ok(dir.to_string_lossy().into())
}
