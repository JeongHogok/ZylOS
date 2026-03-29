// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 에뮬레이터 프로세스 메모리 제한 적용
// 수행범위: Linux cgroup v2 memory.max, macOS setrlimit
// 의존방향: nix (rlimit), std::fs (cgroup sysfs)
// SOLID: OCP — cfg 분기로 플랫폼 확장
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// ──────────────────────────────────────────────────────────

#[cfg(target_os = "linux")]
use std::fs;
#[cfg(target_os = "linux")]
use std::path::Path;

/// 메모리 제한 적용 (MB 단위)
pub fn apply_memory_limit(ram_mb: u32) -> Result<(), String> {
    let bytes = (ram_mb as u64) * 1024 * 1024;

    #[cfg(target_os = "linux")]
    {
        apply_cgroup_limit(bytes)?;
    }

    #[cfg(target_os = "macos")]
    {
        apply_rlimit(bytes)?;
    }

    log::info!("Memory limit applied: {} MB ({} bytes)", ram_mb, bytes);
    Ok(())
}

/// 메모리 제한 해제
pub fn release_memory_limit() -> Result<(), String> {
    #[cfg(target_os = "linux")]
    {
        release_cgroup_limit()?;
    }

    #[cfg(target_os = "macos")]
    {
        release_rlimit()?;
    }

    log::info!("Memory limit released");
    Ok(())
}

// ── Linux: cgroup v2 ──

#[cfg(target_os = "linux")]
fn apply_cgroup_limit(bytes: u64) -> Result<(), String> {
    let cgroup_path = Path::new("/sys/fs/cgroup/zyl-emulator");

    // cgroup 디렉토리 생성 (권한 필요할 수 있음)
    if !cgroup_path.exists() {
        fs::create_dir_all(cgroup_path).map_err(|e| {
            format!(
                "Failed to create cgroup (may need root): {}. Continuing without memory limit.",
                e
            )
        })?;
    }

    // memory.max 설정
    let memory_max_path = cgroup_path.join("memory.max");
    fs::write(&memory_max_path, bytes.to_string())
        .map_err(|e| format!("Failed to set memory.max: {}", e))?;

    // 현재 프로세스를 cgroup에 추가
    let pid = std::process::id();
    let procs_path = cgroup_path.join("cgroup.procs");
    fs::write(&procs_path, pid.to_string())
        .map_err(|e| format!("Failed to add process to cgroup: {}", e))?;

    Ok(())
}

#[cfg(target_os = "linux")]
fn release_cgroup_limit() -> Result<(), String> {
    let cgroup_path = Path::new("/sys/fs/cgroup/zyl-emulator");
    if !cgroup_path.exists() {
        return Ok(());
    }

    // memory.max를 max로 설정 (무제한)
    let memory_max_path = cgroup_path.join("memory.max");
    let _ = fs::write(&memory_max_path, "max");

    // 프로세스를 루트 cgroup으로 이동
    let pid = std::process::id();
    let root_procs = Path::new("/sys/fs/cgroup/cgroup.procs");
    let _ = fs::write(root_procs, pid.to_string());

    Ok(())
}

// ── macOS: setrlimit ──

#[cfg(target_os = "macos")]
fn apply_rlimit(bytes: u64) -> Result<(), String> {
    use nix::sys::resource::{setrlimit, Resource};

    // RLIMIT_AS: 가상 메모리 상한 (best-effort)
    setrlimit(Resource::RLIMIT_AS, bytes, bytes).map_err(|e| {
        format!(
            "setrlimit RLIMIT_AS failed: {}. Continuing without memory limit.",
            e
        )
    })?;

    Ok(())
}

#[cfg(target_os = "macos")]
fn release_rlimit() -> Result<(), String> {
    use nix::sys::resource::{setrlimit, Resource};

    // 무제한으로 복원
    let unlimited = nix::sys::resource::RLIM_INFINITY;
    let _ = setrlimit(Resource::RLIMIT_AS, unlimited, unlimited);

    Ok(())
}
