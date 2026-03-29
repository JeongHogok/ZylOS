// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 가상 디바이스용 디스크 이미지 생성/마운트/해제
// 수행범위: sparse 이미지 생성, 포맷, 루프 마운트, 언마운트
// 의존방향: platform (OS별 구현), state (경로 정보)
// SOLID: OCP — 플랫폼별 구현을 trait 없이 cfg 분기로 확장
// ──────────────────────────────────────────────────────────

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

/// profile_id 유효성 검증 (커맨드 인젝션 + 경로 탐색 방지)
fn validate_profile_id(profile_id: &str) -> Result<(), String> {
    if profile_id.is_empty() || profile_id.len() > 64 {
        return Err("Profile ID must be 1-64 characters".into());
    }
    if !profile_id
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
    {
        return Err(format!(
            "Profile ID '{}' contains invalid characters (only a-z, 0-9, -, _ allowed)",
            profile_id
        ));
    }
    Ok(())
}

/// 디스크 이미지 저장 디렉토리 확보
pub fn ensure_dirs(data_dir: &Path) -> std::io::Result<()> {
    fs::create_dir_all(data_dir.join("images"))?;
    fs::create_dir_all(data_dir.join("mnt"))?;
    fs::create_dir_all(data_dir.join("vfs"))?;
    Ok(())
}

/// sparse 디스크 이미지 생성 (이미 존재하면 스킵)
pub fn create_image(data_dir: &Path, profile_id: &str, size_gb: u32) -> Result<PathBuf, String> {
    validate_profile_id(profile_id)?;
    ensure_dirs(data_dir).map_err(|e| format!("Failed to create dirs: {}", e))?;

    let image_path = data_dir.join("images").join(format!("{}.img", profile_id));

    if image_path.exists() {
        log::info!("Disk image already exists: {:?}", image_path);
        return Ok(image_path);
    }

    log::info!(
        "Creating {}GB sparse disk image: {:?}",
        size_gb,
        image_path
    );

    #[cfg(target_os = "linux")]
    {
        let size_bytes = (size_gb as u64) * 1024 * 1024 * 1024;
        let status = Command::new("fallocate")
            .args(["-l", &size_bytes.to_string()])
            .arg(&image_path)
            .status()
            .map_err(|e| format!("fallocate failed: {}", e))?;

        if !status.success() {
            // fallocate 실패 시 dd 폴백
            let status = Command::new("dd")
                .args([
                    "if=/dev/zero",
                    &format!("of={}", image_path.display()),
                    "bs=1",
                    "count=0",
                    &format!("seek={}", size_bytes),
                ])
                .status()
                .map_err(|e| format!("dd failed: {}", e))?;

            if !status.success() {
                return Err("Failed to create sparse image".into());
            }
        }

        // ext4 포맷
        let status = Command::new("mkfs.ext4")
            .args(["-F", "-q"])
            .arg(&image_path)
            .status()
            .map_err(|e| format!("mkfs.ext4 failed: {}", e))?;

        if !status.success() {
            let _ = fs::remove_file(&image_path);
            return Err("Failed to format image as ext4".into());
        }
    }

    #[cfg(target_os = "macos")]
    {
        let status = Command::new("hdiutil")
            .args([
                "create",
                "-size",
                &format!("{}g", size_gb),
                "-fs",
                "APFS",
                "-type",
                "SPARSE",
                "-volname",
                &format!("ZylOS-{}", profile_id),
            ])
            .arg(&image_path)
            .status()
            .map_err(|e| format!("hdiutil create failed: {}", e))?;

        if !status.success() {
            return Err("Failed to create disk image via hdiutil".into());
        }

        // hdiutil은 .sparseimage 확장자를 붙일 수 있음
        let sparse_path = image_path.with_extension("img.sparseimage");
        if sparse_path.exists() && !image_path.exists() {
            fs::rename(&sparse_path, &image_path)
                .map_err(|e| format!("Failed to rename image: {}", e))?;
        }
    }

    log::info!("Disk image created: {:?}", image_path);
    Ok(image_path)
}

/// 디스크 이미지 마운트 → 마운트 포인트 반환
pub fn mount_image(data_dir: &Path, profile_id: &str, image_path: &Path) -> Result<PathBuf, String> {
    validate_profile_id(profile_id)?;
    let mount_point = data_dir.join("mnt").join(profile_id);
    fs::create_dir_all(&mount_point)
        .map_err(|e| format!("Failed to create mount point: {}", e))?;

    #[cfg(target_os = "linux")]
    {
        // udisksctl은 root 불필요 (polkit 사용)
        let output = Command::new("udisksctl")
            .args(["loop-setup", "-f"])
            .arg(image_path)
            .output()
            .map_err(|e| format!("udisksctl loop-setup failed: {}", e))?;

        if !output.status.success() {
            return fallback_vfs(data_dir, profile_id);
        }

        let stdout = String::from_utf8_lossy(&output.stdout);
        // "Mapped file ... as /dev/loopN."
        let loop_dev = stdout
            .split_whitespace()
            .find(|s| s.starts_with("/dev/loop"))
            .map(|s| s.trim_end_matches('.').to_string())
            .ok_or_else(|| "Failed to parse loop device".to_string())?;

        let status = Command::new("udisksctl")
            .args(["mount", "-b", &loop_dev, "--no-user-interaction"])
            .status()
            .map_err(|e| format!("udisksctl mount failed: {}", e))?;

        if !status.success() {
            return fallback_vfs(data_dir, profile_id);
        }

        log::info!("Mounted {} at {:?}", loop_dev, mount_point);
    }

    #[cfg(target_os = "macos")]
    {
        let output = Command::new("hdiutil")
            .args(["attach", "-mountpoint"])
            .arg(&mount_point)
            .arg(image_path)
            .output()
            .map_err(|e| format!("hdiutil attach failed: {}", e))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            log::warn!("hdiutil attach failed: {}, falling back to vfs", stderr);
            return fallback_vfs(data_dir, profile_id);
        }

        log::info!("Mounted image at {:?}", mount_point);
    }

    Ok(mount_point)
}

/// 마운트 실패 시 일반 디렉토리로 폴백
fn fallback_vfs(data_dir: &Path, profile_id: &str) -> Result<PathBuf, String> {
    let vfs_path = data_dir.join("vfs").join(profile_id);
    fs::create_dir_all(&vfs_path)
        .map_err(|e| format!("Failed to create vfs fallback: {}", e))?;

    // 기본 디렉토리 구조 생성
    for dir in &["Documents", "Downloads", "Pictures", "Music", "Videos"] {
        let _ = fs::create_dir_all(vfs_path.join(dir));
    }

    log::warn!(
        "Using directory fallback instead of disk image: {:?}",
        vfs_path
    );
    Ok(vfs_path)
}

/// 디스크 이미지 언마운트
pub fn unmount_image(mount_point: &Path) -> Result<(), String> {
    if !mount_point.exists() {
        return Ok(());
    }

    #[cfg(target_os = "linux")]
    {
        let _ = Command::new("udisksctl")
            .args(["unmount", "-p"])
            .arg(mount_point)
            .status();
    }

    #[cfg(target_os = "macos")]
    {
        let _ = Command::new("hdiutil")
            .args(["detach"])
            .arg(mount_point)
            .status();
    }

    log::info!("Unmounted: {:?}", mount_point);
    Ok(())
}

/// 마운트 포인트의 디스크 사용량 조회
pub fn get_usage(mount_point: &Path) -> Result<(u64, u64, u64), String> {
    if !mount_point.exists() {
        return Err("Mount point does not exist".into());
    }

    // statvfs를 사용하지 않고 portable하게 du + df 대신 Rust std로 처리
    // 간단한 방법: walkdir로 계산하거나 Command 사용
    let output = Command::new("df")
        .args(["-k"])
        .arg(mount_point)
        .output()
        .map_err(|e| format!("df failed: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout);
    let lines: Vec<&str> = stdout.lines().collect();

    if lines.len() < 2 {
        return Ok((0, 0, 0));
    }

    let fields: Vec<&str> = lines[1].split_whitespace().collect();
    if fields.len() < 4 {
        return Ok((0, 0, 0));
    }

    let total = fields[1].parse::<u64>().unwrap_or(0) * 1024;
    let used = fields[2].parse::<u64>().unwrap_or(0) * 1024;
    let available = fields[3].parse::<u64>().unwrap_or(0) * 1024;

    Ok((total, used, available))
}
