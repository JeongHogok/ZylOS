// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 에뮬레이터의 푸시 입출력 — 가상 기기가 푸시 서버에 묻는 길
// 수행범위: OS 이미지의 푸시 설정(system/push.json) 제공, 토픽들에 그 뒤로 온 것을
//           묻기(ntfy ?poll=1, curl — 기기의 zyl-push 는 스트림 하나를 붙들고, 에뮬레이터
//           는 OS 가 이따금 묻는다). 무엇을 누구에게 줄지는 OS(apps/system/services/push.js)
// 의존방향: curl, serde_json
// SOLID: SRP — 푸시 서버 입출력만
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use std::process::Command;

/// The OS image's push configuration, as the device has it in /etc/zyl-os
const PUSH_CONFIG: &str = include_str!("../../../system/push.json");

#[tauri::command]
pub fn push_config() -> String {
    PUSH_CONFIG.to_string()
}

/// Topics and an id as the OS makes them — nothing that could reshape the URL
fn safe(topics: &str, since: &str) -> bool {
    !topics.is_empty()
        && topics.len() <= 4096
        && topics.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == ',')
        && since.len() <= 64
        && since.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

/// What came for these topics after `since`: the messages, as the server sent them
#[tauri::command]
pub async fn push_poll(topics: String, since: String) -> Result<Vec<serde_json::Value>, String> {
    if !safe(&topics, &since) {
        return Err("bad topics".into());
    }
    let config: serde_json::Value = serde_json::from_str(PUSH_CONFIG).map_err(|e| e.to_string())?;
    let base = config["url"].as_str().unwrap_or("").trim_end_matches('/').to_string();
    if !base.starts_with("https://") {
        return Err("push unavailable".into());
    }
    let mut url = format!("{}/{}/json?poll=1", base, topics);
    if !since.is_empty() {
        url.push_str("&since=");
        url.push_str(&since);
    }
    let out = Command::new("curl")
        .args(["-s", "-f", "--proto", "=https", "-m", "15", &url])
        .output()
        .map_err(|e| format!("curl: {}", e))?;
    if !out.status.success() {
        return Err(format!("push server: curl exit {}", out.status.code().unwrap_or(-1)));
    }
    Ok(String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|l| serde_json::from_str::<serde_json::Value>(l).ok())
        .filter(|v| v["event"] == "message")
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_what_the_os_makes_reaches_the_url() {
        assert!(safe("zyl0a1b,zyl2c3d", "AbC_12-x"));
        assert!(safe("zyla", ""));
        assert!(!safe("", ""));
        assert!(!safe("zyla/../x", ""));
        assert!(!safe("zyla?x=1", ""));
        assert!(!safe("zyla", "a&b=1"));
    }

    #[test]
    fn the_image_names_an_https_server() {
        let c: serde_json::Value = serde_json::from_str(PUSH_CONFIG).unwrap();
        let url = c["url"].as_str().unwrap();
        assert!(url.is_empty() || url.starts_with("https://"));
    }
}
