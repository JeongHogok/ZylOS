// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 터미널 명령 실행 (샌드박스 내, allowlist + denylist 필터링)
// 수행범위: 허용된 쉘 명령 실행, stdout/stderr 캡처, 작업 디렉토리 제한
// 의존방향: state (mount_point)
// SOLID: SRP — 명령 실행만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::state::AppState;
use regex::Regex;
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

/// Allowed command binaries — only these can be the first word of a command.
/// Anything not on this list is rejected outright.
const ALLOWED_COMMANDS: &[&str] = &[
    // POSIX/coreutils
    "ls", "cat", "head", "tail", "wc", "sort", "uniq", "grep", "egrep", "fgrep",
    "find", "xargs", "echo", "printf", "date", "cal", "env", "printenv",
    "pwd", "cd", "basename", "dirname", "realpath", "readlink",
    "cut", "tr", "sed", "awk", "diff", "comm", "paste", "join", "tee",
    "touch", "mkdir", "cp", "mv", "ln", "stat", "file", "du", "df",
    "true", "false", "test", "[", "seq", "yes", "sleep",
    // Text editors (view only in emulator)
    "less", "more", "vi", "nano",
    // File content
    "md5sum", "sha256sum", "base64", "od", "hexdump", "xxd", "strings",
    // Compression (safe subset)
    "gzip", "gunzip", "zcat", "bzip2", "bunzip2", "xz", "unxz",
    "tar", "zip", "unzip",
    // Development
    "python3", "python", "node", "sh", "bash",
    // System info (read-only)
    "uname", "hostname", "whoami", "id", "uptime", "free", "top",
    "ps", "lscpu", "lsblk", "mount", "ip",
    // Zyl OS specific
    "zyl-shell",
];

/// Deny patterns — these are checked against the FULL command string.
/// Regex-based to prevent trivial bypasses.
fn build_deny_patterns() -> Vec<Regex> {
    let patterns = [
        // Recursive destructive operations
        r"rm\s+.*-[^\s]*r[^\s]*f",       // rm with -rf in any order
        r"rm\s+.*-[^\s]*f[^\s]*r",       // rm with -fr in any order
        r"rm\s+-rf\b",                    // rm -rf
        r"rm\s+-r\s+/",                   // rm -r /
        // Filesystem formatting
        r"\bmkfs\b",
        r"\bdd\s+if=",
        // Device writes
        r">\s*/dev/",
        // Permission bombs
        r"chmod\s+.*777\s+/",
        r"chmod\s+-R\s+777",
        r"chown\s+-R\s",
        // Fork bombs
        r":\(\)\s*\{",
        r"\.\s*\(\)\s*\{",
        // System state
        r"\bshutdown\b",
        r"\breboot\b",
        r"\bhalt\b",
        r"\binit\s+[06]\b",
        r"\bpoweroff\b",
        r"\bsystemctl\s+(halt|poweroff|reboot|suspend|hibernate)",
        // Privilege escalation
        r"\bsudo\b",
        r"\bsu\s",
        r"\bsu$",
        r"\bpasswd\b",
        r"\bpkexec\b",
        r"\bdoas\b",
        // Dangerous network piping
        r"\bcurl\b.*\|\s*(sh|bash|zsh)",
        r"\bwget\b.*\|\s*(sh|bash|zsh)",
        r"\bcurl\b.*-o\s*/",
        r"\bwget\b.*-O\s*/",
        // Network listeners
        r"\bnc\s+-l",
        r"\bncat\b",
        r"\bsocat\b",
        r"\bnetcat\b",
        // Sensitive file access
        r"/etc/shadow",
        r"/etc/passwd",
        r"/etc/sudoers",
        r"/proc/kcore",
        r"/dev/mem",
        r"/dev/kmem",
        // Escape attempts
        r"\bchroot\b",
        r"\bnsentr\b",
        r"\bnsenter\b",
        r"\bunshare\b",
        // Kernel manipulation
        r"\binsmod\b",
        r"\brmmod\b",
        r"\bmodprobe\b",
        r"\bsysctl\s+-w",
        // History/credential theft
        r"\.bash_history",
        r"\.ssh/",
        r"\.gnupg/",
        // Python/interpreter abuse
        r"python[23]?\s+-c\s+.*import\s+os",
        r"python[23]?\s+-c\s+.*subprocess",
        r"python[23]?\s+-c\s+.*exec\(",
        r"\bperl\s+-e",
        r"\bruby\s+-e",
        // Path outside mount point
        r"\.\./\.\./",
    ];

    patterns
        .iter()
        .filter_map(|p| Regex::new(p).ok())
        .collect()
}

/// Extract the base command name from a shell command string.
/// Handles: "command args", "VAR=val command args", "/path/to/command args"
fn extract_command_name(command: &str) -> &str {
    let trimmed = command.trim();

    // Skip leading env-var assignments (VAR=val ...)
    let mut rest = trimmed;
    loop {
        let word_end = rest.find(|c: char| c.is_whitespace()).unwrap_or(rest.len());
        let word = &rest[..word_end];
        if word.contains('=') && !word.starts_with('=') {
            rest = rest[word_end..].trim_start();
            if rest.is_empty() {
                return "";
            }
        } else {
            break;
        }
    }

    // Get first word (command)
    let word_end = rest.find(|c: char| c.is_whitespace()).unwrap_or(rest.len());
    let cmd = &rest[..word_end];

    // Extract basename from path
    cmd.rsplit('/').next().unwrap_or(cmd)
}

/// Check if a command is safe to execute.
/// Returns Ok(()) if allowed, Err(reason) if blocked.
fn validate_command(command: &str, deny_patterns: &[Regex]) -> Result<(), String> {
    let trimmed = command.trim();

    // Empty command: allow (no-op)
    if trimmed.is_empty() {
        return Ok(());
    }

    // Shell operators that chain commands — validate each segment
    // Split on ;, &&, ||, | but NOT inside quotes
    let segments = split_command_segments(trimmed);
    for segment in &segments {
        let seg = segment.trim();
        if seg.is_empty() {
            continue;
        }

        // Check command name against allowlist
        let cmd_name = extract_command_name(seg);
        if !cmd_name.is_empty() && !ALLOWED_COMMANDS.contains(&cmd_name) {
            return Err(format!(
                "zyl-shell: command not allowed: '{}'. Only approved commands are permitted.",
                cmd_name
            ));
        }
    }

    // Check full command against deny patterns
    let lower = command.to_lowercase();
    for pattern in deny_patterns {
        if pattern.is_match(&lower) {
            return Err(format!(
                "zyl-shell: command blocked by security policy: {}",
                trimmed.split_whitespace().next().unwrap_or("")
            ));
        }
    }

    Ok(())
}

/// Split a command string on shell operators (;, &&, ||, |)
/// without splitting inside quotes.
fn split_command_segments(command: &str) -> Vec<String> {
    let mut segments = Vec::new();
    let mut current = String::new();
    let mut in_single = false;
    let mut in_double = false;
    let mut chars = command.chars().peekable();

    while let Some(c) = chars.next() {
        match c {
            '\'' if !in_double => {
                in_single = !in_single;
                current.push(c);
            }
            '"' if !in_single => {
                in_double = !in_double;
                current.push(c);
            }
            '\\' if !in_single => {
                current.push(c);
                if let Some(next) = chars.next() {
                    current.push(next);
                }
            }
            ';' if !in_single && !in_double => {
                segments.push(current.clone());
                current.clear();
            }
            '&' if !in_single && !in_double => {
                if chars.peek() == Some(&'&') {
                    chars.next();
                    segments.push(current.clone());
                    current.clear();
                } else {
                    current.push(c);
                }
            }
            '|' if !in_single && !in_double => {
                if chars.peek() == Some(&'|') {
                    chars.next();
                    segments.push(current.clone());
                    current.clear();
                } else {
                    // Pipe — check the next command too
                    segments.push(current.clone());
                    current.clear();
                }
            }
            _ => current.push(c),
        }
    }
    if !current.is_empty() {
        segments.push(current);
    }
    segments
}

/// 쉘 명령 실행 (샌드박스: 마운트 포인트 내에서만, allowlist + denylist 검증)
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

    // Build deny patterns (could be cached, but correctness > performance here)
    let deny_patterns = build_deny_patterns();

    // Validate command against allowlist + denylist
    if let Err(reason) = validate_command(trimmed, &deny_patterns) {
        return Ok(ExecResult {
            stdout: String::new(),
            stderr: reason,
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

    // Clear environment and set only safe variables
    let output = Command::new("sh")
        .args(["-c", trimmed])
        .current_dir(&cwd)
        .env_clear()
        .env("HOME", &cwd)
        .env("PATH", "/usr/local/bin:/usr/bin:/bin")
        .env("TERM", "xterm-256color")
        .env("LANG", "en_US.UTF-8")
        .env("SHELL", "/bin/sh")
        .output()
        .map_err(|e| format!("Command execution failed: {}", e))?;

    Ok(ExecResult {
        stdout: String::from_utf8_lossy(&output.stdout).to_string(),
        stderr: String::from_utf8_lossy(&output.stderr).to_string(),
        exit_code: output.status.code().unwrap_or(-1),
    })
}
