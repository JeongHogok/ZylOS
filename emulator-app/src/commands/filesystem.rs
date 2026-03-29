// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 가상 파일시스템 접근 Tauri 커맨드
// 수행범위: 마운트 포인트 기준 디렉토리/파일 읽기, 쓰기, 사용량 조회
// 의존방향: state (mount_point), resource/disk_image (get_usage)
// SOLID: SRP — 파일시스템 I/O만 담당, LSP — 모든 커맨드 동일 에러 포맷
// ──────────────────────────────────────────────────────────

use crate::resource::disk_image;
use crate::state::AppState;
use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use tauri::State;

#[derive(Debug, Serialize)]
pub struct FileEntry {
    pub name: String,
    pub is_dir: bool,
    pub size: u64,
    pub modified: String,
    pub file_type: String,
}

#[derive(Debug, Serialize)]
pub struct StorageUsage {
    pub total: u64,
    pub used: u64,
    pub available: u64,
    pub percent: f64,
}

/// 마운트 포인트 내부의 안전한 경로 계산
fn safe_path(mount_point: &Path, rel_path: &str) -> Result<PathBuf, String> {
    // ".." 포함 여부 먼저 확인
    if rel_path.contains("..") {
        return Err("Path traversal blocked: '..' not allowed".into());
    }

    let cleaned = rel_path.trim_start_matches('/');
    let full = mount_point.join(cleaned);

    // 경로 탐색 공격 방지: 정규화 후 마운트 포인트 안에 있는지 확인
    let canonical_mount = mount_point
        .canonicalize()
        .map_err(|e| format!("Mount point canonicalize failed: {}", e))?;

    // 파일이 아직 없으면 (쓰기 등) 부모 디렉토리 기준으로 확인
    let check_path = if full.exists() {
        full.canonicalize()
            .map_err(|e| format!("Path canonicalize failed: {}", e))?
    } else if let Some(parent) = full.parent() {
        let canonical_parent = parent
            .canonicalize()
            .map_err(|e| format!("Parent canonicalize failed: {}", e))?;
        canonical_parent.join(full.file_name().unwrap_or_default())
    } else {
        return Err("Invalid path".into());
    };

    if !check_path.starts_with(&canonical_mount) {
        return Err("Path traversal blocked".into());
    }

    Ok(full)
}

/// 파일 확장자로 타입 추정
fn detect_file_type(name: &str) -> String {
    let ext = name
        .rsplit('.')
        .next()
        .unwrap_or("")
        .to_lowercase();

    match ext.as_str() {
        "jpg" | "jpeg" | "png" | "gif" | "bmp" | "svg" | "webp" => "image".into(),
        "mp4" | "mkv" | "avi" | "mov" | "webm" => "video".into(),
        "mp3" | "wav" | "ogg" | "flac" | "aac" => "audio".into(),
        "pdf" | "doc" | "docx" | "txt" | "rtf" | "odt" => "document".into(),
        "zip" | "tar" | "gz" | "7z" | "rar" => "archive".into(),
        "c" | "h" | "js" | "py" | "rs" | "java" | "html" | "css" => "code".into(),
        _ => "unknown".into(),
    }
}

/// 디렉토리 내용 읽기
#[tauri::command]
pub fn fs_read_dir(
    path: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<Vec<FileEntry>, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let full_path = safe_path(mount_point, &path)?;

    if !full_path.exists() {
        return Ok(vec![]);
    }

    let mut entries = Vec::new();
    let dir_entries = fs::read_dir(&full_path).map_err(|e| format!("Read dir error: {}", e))?;

    for entry in dir_entries.flatten() {
        let metadata = match entry.metadata() {
            Ok(m) => m,
            Err(_) => continue, // 메타데이터 읽기 실패 시 스킵
        };

        let name = entry.file_name().to_string_lossy().into_owned();

        // 숨김 파일 스킵 (lost+found 등)
        if name.starts_with('.') || name == "lost+found" {
            continue;
        }

        let modified = metadata
            .modified()
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map(|d| d.as_secs().to_string())
            .unwrap_or_default();

        entries.push(FileEntry {
            file_type: if metadata.is_dir() {
                "folder".into()
            } else {
                detect_file_type(&name)
            },
            name,
            is_dir: metadata.is_dir(),
            size: metadata.len(),
            modified,
        });
    }

    // 폴더 먼저, 이름순 정렬
    entries.sort_by(|a, b| {
        b.is_dir
            .cmp(&a.is_dir)
            .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
    });

    Ok(entries)
}

/// 파일 읽기 (텍스트)
#[tauri::command]
pub fn fs_read_file(
    path: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<String, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let full_path = safe_path(mount_point, &path)?;
    fs::read_to_string(&full_path).map_err(|e| format!("Read error: {}", e))
}

/// 파일 쓰기
#[tauri::command]
pub fn fs_write_file(
    path: String,
    content: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let full_path = safe_path(mount_point, &path)?;

    // 부모 디렉토리 생성
    if let Some(parent) = full_path.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("Mkdir error: {}", e))?;
    }

    fs::write(&full_path, content).map_err(|e| format!("Write error: {}", e))
}

/// 디렉토리 생성
#[tauri::command]
pub fn fs_mkdir(
    path: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let full_path = safe_path(mount_point, &path)?;
    fs::create_dir_all(&full_path).map_err(|e| format!("Mkdir error: {}", e))
}

/// 파일/디렉토리 삭제
#[tauri::command]
pub fn fs_remove(
    path: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let full_path = safe_path(mount_point, &path)?;

    if full_path.is_dir() {
        fs::remove_dir_all(&full_path).map_err(|e| format!("Remove dir error: {}", e))
    } else {
        fs::remove_file(&full_path).map_err(|e| format!("Remove file error: {}", e))
    }
}

/// 파일/디렉토리 이름 변경
#[tauri::command]
pub fn fs_rename(
    old_path: String,
    new_path: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<(), String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let old_full = safe_path(mount_point, &old_path)?;
    let new_full = safe_path(mount_point, &new_path)?;

    fs::rename(&old_full, &new_full).map_err(|e| format!("Rename error: {}", e))
}

/// 저장공간 사용량 조회
#[tauri::command]
pub fn fs_get_usage(state: State<'_, Mutex<AppState>>) -> Result<StorageUsage, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let (total, used, available) = disk_image::get_usage(mount_point)?;
    let percent = if total > 0 {
        (used as f64 / total as f64) * 100.0
    } else {
        0.0
    };

    Ok(StorageUsage {
        total,
        used,
        available,
        percent,
    })
}
