// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 가상 파일시스템 접근 Tauri 커맨드
// 수행범위: 마운트 포인트 기준 디렉토리/파일 읽기, 쓰기, 사용량 조회
// 의존방향: state (mount_point), resource/disk_image (get_usage)
// SOLID: SRP — 파일시스템 I/O만 담당, LSP — 모든 커맨드 동일 에러 포맷
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

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

/// 바이너리 파일을 base64로 읽기 (이미지 등)
#[tauri::command]
pub fn fs_read_binary(
    path: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<String, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    let full_path = safe_path(mount_point, &path)?;
    let bytes = fs::read(&full_path).map_err(|e| format!("Read error: {}", e))?;

    // base64 인코딩
    Ok(base64_encode(&bytes))
}

fn base64_encode(data: &[u8]) -> String {
    const TABLE: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut result = String::new();
    let chunks = data.chunks(3);
    for chunk in chunks {
        let b0 = chunk[0] as u32;
        let b1 = if chunk.len() > 1 { chunk[1] as u32 } else { 0 };
        let b2 = if chunk.len() > 2 { chunk[2] as u32 } else { 0 };
        let n = (b0 << 16) | (b1 << 8) | b2;
        result.push(TABLE[((n >> 18) & 63) as usize] as char);
        result.push(TABLE[((n >> 12) & 63) as usize] as char);
        if chunk.len() > 1 { result.push(TABLE[((n >> 6) & 63) as usize] as char); } else { result.push('='); }
        if chunk.len() > 2 { result.push(TABLE[(n & 63) as usize] as char); } else { result.push('='); }
    }
    result
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

    // base64 감지: JPEG/PNG 등 바이너리 데이터는 base64로 전달됨
    if content.len() > 100 && !content.contains('\n') && content.chars().all(|c| c.is_ascii_alphanumeric() || c == '+' || c == '/' || c == '=') {
        // base64 디코딩 시도
        use std::io::Write;
        let decoded = base64_decode(&content);
        if !decoded.is_empty() {
            let mut file = fs::File::create(&full_path).map_err(|e| format!("Create error: {}", e))?;
            file.write_all(&decoded).map_err(|e| format!("Write error: {}", e))?;
            return Ok(());
        }
    }

    fs::write(&full_path, content).map_err(|e| format!("Write error: {}", e))
}

/// 간단한 base64 디코딩 (외부 크레이트 없이)
fn base64_decode(input: &str) -> Vec<u8> {
    const TABLE: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut result = Vec::new();
    let mut buf: u32 = 0;
    let mut bits: u32 = 0;
    for &b in input.as_bytes() {
        if b == b'=' { break; }
        let val = TABLE.iter().position(|&c| c == b);
        if let Some(v) = val {
            buf = (buf << 6) | (v as u32);
            bits += 6;
            if bits >= 8 {
                bits -= 8;
                result.push((buf >> bits) as u8);
                buf &= (1 << bits) - 1;
            }
        }
    }
    result
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

/// 마운트 포인트의 실제 파일 사용량 계산 (재귀)
fn dir_size(path: &Path) -> u64 {
    if !path.is_dir() {
        return path.metadata().map(|m| m.len()).unwrap_or(0);
    }
    fs::read_dir(path)
        .map(|entries| {
            entries
                .flatten()
                .map(|e| {
                    let p = e.path();
                    if p.is_dir() { dir_size(&p) } else { e.metadata().map(|m| m.len()).unwrap_or(0) }
                })
                .sum()
        })
        .unwrap_or(0)
}

/// 저장공간 사용량 조회 (가상 디바이스 기준 — 호스트 디스크 아님)
#[tauri::command]
pub fn fs_get_usage(state: State<'_, Mutex<AppState>>) -> Result<StorageUsage, String> {
    let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let mount_point = app_state
        .mount_point
        .as_ref()
        .ok_or("No filesystem mounted")?;

    /* 설정된 가상 스토리지 크기를 total로 사용 */
    let config_total = app_state
        .config
        .as_ref()
        .map(|c| (c.storage_gb as u64) * 1024 * 1024 * 1024)
        .unwrap_or(8 * 1024 * 1024 * 1024);

    /* 실제 사용량: 마운트 포인트 내 파일 크기 합산 */
    let used = dir_size(mount_point);
    let available = if config_total > used { config_total - used } else { 0 };
    let percent = if config_total > 0 {
        (used as f64 / config_total as f64) * 100.0
    } else {
        0.0
    };

    Ok(StorageUsage {
        total: config_total,
        used,
        available,
        percent,
    })
}
