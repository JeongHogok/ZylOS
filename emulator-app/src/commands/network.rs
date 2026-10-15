// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: HTTP fetch 프록시
// 수행범위: 도메인 화이트리스트 기반 HTTP 프록시 (Rust 레벨 강제)
// 의존방향: std::process::Command (curl)
// SOLID: SRP — 도메인 검증과 가져오기만
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use std::process::Command;

/// Hostname suffix allowlist. A request is permitted iff its parsed hostname
/// equals one of these entries OR ends with ".<entry>".
const ALLOWED_DOMAINS: &[&str] = &[
    // Weather services used by Zyl OS weather app
    "wttr.in",
    "api.open-meteo.com",
    // IP geolocation used by location service
    "ipinfo.io",
    // Zyl update / app-store endpoints
    "updates.zylos.dev",
    "store.zylos.dev",
    // OpenStreetMap tile/nominatim (maps app)
    "tile.openstreetmap.org",
    "nominatim.openstreetmap.org",
];

/// Validate a URL against the domain whitelist and block unsafe targets.
/// Returns Ok(hostname) or Err(human-readable reason).
fn validate_fetch_url(url: &str) -> Result<String, String> {
    // Scheme check
    if !url.starts_with("https://") && !url.starts_with("http://") {
        return Err("Invalid URL: must start with http:// or https://".into());
    }

    // Length limit
    if url.len() > 2048 {
        return Err("URL too long (max 2048 characters)".into());
    }

    // Extract hostname from URL (simple parser — no external crate required)
    // Format: scheme://[userinfo@]host[:port]/path?query#fragment
    let after_scheme = url
        .find("://")
        .map(|i| &url[i + 3..])
        .ok_or_else(|| "Malformed URL: missing '://'".to_string())?;

    // Drop path/query/fragment
    let host_part = after_scheme
        .split('/')
        .next()
        .unwrap_or(after_scheme);

    // Drop userinfo (user:pass@host)
    let host_part = host_part
        .split('@')
        .last()
        .unwrap_or(host_part);

    // Drop port
    let hostname = if host_part.contains(':') && !host_part.starts_with('[') {
        // IPv4 with port or plain host:port
        host_part.split(':').next().unwrap_or(host_part)
    } else if host_part.starts_with('[') {
        // IPv6 literal — always blocked (see below)
        host_part
    } else {
        host_part
    };

    let hostname = hostname.to_lowercase();

    // Block loopback and localhost
    if hostname == "localhost"
        || hostname == "127.0.0.1"
        || hostname == "::1"
        || hostname.starts_with("127.")
    {
        return Err(format!("Blocked: loopback address '{}' is not permitted", hostname));
    }

    // Block IPv6 literals (prevent bypass via [::1] etc.)
    if hostname.starts_with('[') {
        return Err(format!("Blocked: IPv6 literal address '{}' is not permitted", hostname));
    }

    // Block bare IPv4 addresses (prevents SSRF against RFC1918/link-local/cloud metadata)
    if hostname.chars().all(|c| c.is_ascii_digit() || c == '.') {
        return Err(format!(
            "Blocked: IP address '{}' is not permitted. Use a hostname.",
            hostname
        ));
    }

    // Domain allowlist check
    let permitted = ALLOWED_DOMAINS.iter().any(|&allowed| {
        hostname == allowed || hostname.ends_with(&format!(".{}", allowed))
    });

    if !permitted {
        return Err(format!(
            "Domain '{}' is not on the http_fetch allowlist. \
             Contact the OS service team to add it.",
            hostname
        ));
    }

    Ok(hostname)
}

/// Fetch a URL via curl (non-blocking).
///
/// The domain whitelist is enforced at the Rust command boundary. Callers
/// that invoke this command directly (bypassing sandbox.js) still receive the
/// same domain restrictions. IP literals and loopback are blocked to prevent
/// SSRF against the host.
///
/// Runs curl in a background thread via std::thread::spawn so the Tauri main
/// thread (and therefore the entire WebView) is never blocked by slow networks.
#[tauri::command]
pub async fn http_fetch(url: String) -> Result<String, String> {
    // Validate URL before spawning any threads
    validate_fetch_url(&url)?;

    let (tx, rx) = tokio::sync::oneshot::channel();

    std::thread::spawn(move || {
        let result = Command::new("curl")
            .args([
                "-s",          // silent
                "-m", "8",     // max time 8 seconds
                "--connect-timeout", "5",
                "-L",          // follow redirects
                "--max-redirs", "3",
                "--max-filesize", "1048576", // max 1MB response
            ])
            .arg(&url)
            .output();

        let send_result = match result {
            Ok(output) => {
                if !output.status.success() {
                    Err(format!("HTTP request failed with status: {}", output.status))
                } else {
                    String::from_utf8(output.stdout)
                        .map_err(|e| format!("Response encoding error: {}", e))
                }
            }
            Err(e) => Err(format!("curl failed: {}", e)),
        };

        let _ = tx.send(send_result);
    });

    rx.await.map_err(|_| "Network request cancelled".to_string())?
}

// ────────────────────────────────────────────────────────────────────────────
// Unit tests
// ────────────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_allowed_domain_exact() {
        assert!(validate_fetch_url("https://wttr.in/Seoul").is_ok());
    }

    #[test]
    fn test_allowed_domain_subdomain() {
        assert!(validate_fetch_url("https://tile.openstreetmap.org/path").is_ok());
    }

    #[test]
    fn test_allowed_ipinfo() {
        assert!(validate_fetch_url("https://ipinfo.io/json").is_ok());
    }

    #[test]
    fn test_blocked_unlisted_domain() {
        let err = validate_fetch_url("https://evil.com/payload");
        assert!(err.is_err(), "unlisted domain must be blocked");
        let msg = err.unwrap_err();
        assert!(msg.contains("allowlist"), "should mention allowlist");
    }

    #[test]
    fn test_blocked_localhost() {
        let err = validate_fetch_url("http://localhost/admin");
        assert!(err.is_err());
        let msg = err.unwrap_err();
        assert!(msg.contains("loopback"), "should mention loopback");
    }

    #[test]
    fn test_blocked_loopback_ip() {
        let err = validate_fetch_url("http://127.0.0.1:8080/secret");
        assert!(err.is_err());
    }

    #[test]
    fn test_blocked_ipv4_literal() {
        let err = validate_fetch_url("https://192.168.1.1/router");
        assert!(err.is_err());
        let msg = err.unwrap_err();
        assert!(msg.contains("IP address"), "should mention IP address");
    }

    #[test]
    fn test_blocked_ipv6_literal() {
        let err = validate_fetch_url("http://[::1]:80/");
        assert!(err.is_err());
    }

    #[test]
    fn test_blocked_non_http_scheme() {
        let err = validate_fetch_url("file:///etc/passwd");
        assert!(err.is_err());
    }

    #[test]
    fn test_blocked_url_too_long() {
        let long_url = format!("https://wttr.in/{}", "a".repeat(2048));
        let err = validate_fetch_url(&long_url);
        assert!(err.is_err());
    }

    #[test]
    fn test_subdomain_of_unlisted_domain_blocked() {
        // sub.evil.com should NOT be allowed just because evil.com contains wttr.in as substring
        let err = validate_fetch_url("https://not-wttr.in/data");
        assert!(err.is_err(), "subdomain of unlisted domain must be blocked");
    }

    #[test]
    fn test_cloud_metadata_blocked() {
        // AWS EC2 metadata endpoint — must be blocked
        let err = validate_fetch_url("http://169.254.169.254/latest/meta-data/");
        assert!(err.is_err());
    }
}
