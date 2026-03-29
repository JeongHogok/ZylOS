// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 터미널 명령 실행 (샌드박스 내)
// 수행범위: 쉘 명령 실행, stdout/stderr 캡처, 작업 디렉토리 관리
// 의존방향: state (mount_point)
// SOLID: SRP — 명령 실행만 담당
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

/// 쉘 명령 실행
#[tauri::command]
pub fn exec_command(
    command: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<ExecResult, String> {
    if command.trim().is_empty() {
        return Ok(ExecResult {
            stdout: String::new(),
            stderr: String::new(),
            exit_code: 0,
        });
    }

    // 작업 디렉토리: 마운트 포인트 또는 홈
    let cwd = {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state
            .mount_point
            .clone()
            .unwrap_or_else(|| std::env::temp_dir())
    };

    let output = Command::new("sh")
        .args(["-c", &command])
        .current_dir(&cwd)
        .output()
        .map_err(|e| format!("Command execution failed: {}", e))?;

    Ok(ExecResult {
        stdout: String::from_utf8_lossy(&output.stdout).to_string(),
        stderr: String::from_utf8_lossy(&output.stderr).to_string(),
        exit_code: output.status.code().unwrap_or(-1),
    })
}
