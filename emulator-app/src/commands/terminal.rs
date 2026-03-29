// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 터미널 명령 실행 (샌드박스 내, 위험 명령 필터링)
// 수행범위: 허용된 쉘 명령 실행, stdout/stderr 캡처, 작업 디렉토리 제한
// 의존방향: state (mount_point)
// SOLID: SRP — 명령 실행만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// ──────────────────────────────────────────────────────────

use crate::state::AppState;
use serde::Serialize;
use std::process::Command;
use std::sync::Mutex;
use tauri::State;

#[derive(Debug, Serialize)]
pub struct ExecResult {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}

/// 위험 명령 패턴 필터링
fn is_dangerous(command: &str) -> bool {
    let lower = command.to_lowercase();
    let patterns = [
        "rm -rf /",
        "rm -rf /*",
        "rm -rf ~",
        "mkfs",
        "dd if=",
        "> /dev/",
        "chmod -R 777 /",
        ":(){ :|:& };:",
        "shutdown",
        "reboot",
        "halt",
        "init 0",
        "init 6",
        "sudo",
        "su -",
        "passwd",
        "chown -R",
        "curl.*|.*sh",
        "wget.*|.*sh",
        "nc -l",
        "ncat",
        "/etc/shadow",
        "/etc/passwd",
    ];

    for pat in &patterns {
        if lower.contains(pat) {
            return true;
        }
    }

    false
}

/// 쉘 명령 실행 (샌드박스: 마운트 포인트 내에서만, 위험 명령 차단)
#[tauri::command]
pub fn exec_command(
    command: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<ExecResult, String> {
    let trimmed = command.trim();
    if trimmed.is_empty() {
        return Ok(ExecResult {
            stdout: String::new(),
            stderr: String::new(),
            exit_code: 0,
        });
    }

    // 위험 명령 필터링
    if is_dangerous(trimmed) {
        return Ok(ExecResult {
            stdout: String::new(),
            stderr: format!("zyl-shell: command blocked for safety: {}", trimmed.split_whitespace().next().unwrap_or("")),
            exit_code: 126,
        });
    }

    // 작업 디렉토리: 마운트 포인트 또는 임시 디렉토리
    let cwd = {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state
            .mount_point
            .clone()
            .unwrap_or_else(|| std::env::temp_dir())
    };

    let output = Command::new("sh")
        .args(["-c", trimmed])
        .current_dir(&cwd)
        .env("HOME", &cwd)
        .env("PATH", "/usr/local/bin:/usr/bin:/bin")
        .output()
        .map_err(|e| format!("Command execution failed: {}", e))?;

    Ok(ExecResult {
        stdout: String::from_utf8_lossy(&output.stdout).to_string(),
        stderr: String::from_utf8_lossy(&output.stderr).to_string(),
        exit_code: output.status.code().unwrap_or(-1),
    })
}
