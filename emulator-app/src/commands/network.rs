// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: HTTP fetch 프록시
// 수행범위: 가져오기와 호스트 보호만 — 기기 network.c 처럼 https 만·리디렉트 없음·포트
//           없음, 루프백·IP 리터럴 차단, 이름이 가리키는 주소가 공인 주소일 때만(검사한
//           주소로 curl 을 고정). 어느 도메인을 허락할지는 OS 의 apps/system/sandbox.js 가
//           정한다 (기기 network.c 와 같은 목록)
// 의존방향: std::process::Command (curl)
// SOLID: SRP — 호스트 보호와 가져오기만
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use std::net::{IpAddr, Ipv4Addr, ToSocketAddrs};
use std::process::Command;

/// Block targets on the developer's host and network.
/// Returns Ok(hostname) or Err(human-readable reason). As the device's
/// network.c: https only, no port, no user info — the host is the whole authority.
fn validate_fetch_url(url: &str) -> Result<String, String> {
    // Scheme check: the device fetches https only
    let after_scheme = url
        .strip_prefix("https://")
        .ok_or_else(|| "Invalid URL: must start with https://".to_string())?;

    // Length limit
    if url.len() > 2048 {
        return Err("URL too long (max 2048 characters)".into());
    }
    if url.bytes().any(|b| b <= 0x20 || b == 0x7f) {
        return Err("Invalid URL: control characters or spaces".into());
    }

    // The authority, up to the path/query/fragment
    let host_part = after_scheme
        .split(|c| c == '/' || c == '?' || c == '#')
        .next()
        .unwrap_or(after_scheme);

    if host_part.contains('@') {
        return Err("Blocked: user info in the URL is not permitted".into());
    }
    if host_part.starts_with('[') {
        return Err(format!("Blocked: IPv6 literal address '{}' is not permitted", host_part));
    }
    if host_part.contains(':') {
        return Err("Blocked: a port is not permitted (the device fetches https on 443 only)".into());
    }

    let hostname = host_part.to_lowercase();
    if hostname.is_empty() {
        return Err("Malformed URL: no host".into());
    }

    // Block loopback and localhost
    if hostname == "localhost" || hostname.ends_with(".localhost") || hostname.starts_with("127.") {
        return Err(format!("Blocked: loopback address '{}' is not permitted", hostname));
    }

    // Block bare IPv4 addresses (prevents SSRF against RFC1918/link-local/cloud metadata)
    if hostname.chars().all(|c| c.is_ascii_digit() || c == '.') {
        return Err(format!(
            "Blocked: IP address '{}' is not permitted. Use a hostname.",
            hostname
        ));
    }

    Ok(hostname)
}

/// An address on the public internet — not the developer's machine, network
/// or cloud metadata. A name may resolve to any of those (a DNS answer of
/// 127.0.0.1 or 10.0.0.5), so the check is on what it resolves to.
fn public_v4(v4: &Ipv4Addr) -> bool {
    let o = v4.octets();
    !(v4.is_loopback() || v4.is_private() || v4.is_link_local() || v4.is_broadcast()
        || v4.is_unspecified() || v4.is_multicast() || v4.is_documentation()
        || o[0] == 0 || (o[0] == 100 && (o[1] & 0xc0) == 64)   // 0/8, CGNAT 100.64/10
        || (o[0] == 198 && (o[1] & 0xfe) == 18))                // benchmarking 198.18/15
}

fn public_address(ip: &IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => public_v4(v4),
        IpAddr::V6(v6) => {
            let s0 = v6.segments()[0];
            !(v6.is_loopback() || v6.is_unspecified() || v6.is_multicast()
                || (s0 & 0xfe00) == 0xfc00        // unique local fc00::/7
                || (s0 & 0xffc0) == 0xfe80)       // link local fe80::/10
                && v6.to_ipv4_mapped().map_or(true, |m| public_v4(&m))
        }
    }
}

/// The address to connect to: every address the name resolves to must be
/// public (one private answer among them is refused), and curl is pinned to
/// the one checked — a second lookup by curl could answer otherwise
fn checked_address(hostname: &str) -> Result<IpAddr, String> {
    let addrs: Vec<IpAddr> = (hostname, 443)
        .to_socket_addrs()
        .map_err(|e| format!("Cannot resolve '{}': {}", hostname, e))?
        .map(|a| a.ip())
        .collect();
    if addrs.is_empty() {
        return Err(format!("Cannot resolve '{}'", hostname));
    }
    if let Some(bad) = addrs.iter().find(|a| !public_address(a)) {
        return Err(format!("Blocked: '{}' resolves to a non-public address ({})", hostname, bad));
    }
    Ok(addrs[0])
}

/// Fetch a URL via curl (non-blocking).
///
/// The OS has already checked the domain (sandbox.js). Here the host is kept
/// safe: https only, no redirects (as the device), and only to a public
/// address the name was checked to resolve to.
///
/// Runs curl in a background thread via std::thread::spawn so the Tauri main
/// thread (and therefore the entire WebView) is never blocked by slow networks.
#[tauri::command]
pub async fn http_fetch(url: String) -> Result<String, String> {
    // Validate URL before spawning any threads
    let hostname = validate_fetch_url(&url)?;

    let (tx, rx) = tokio::sync::oneshot::channel();

    std::thread::spawn(move || {
        let send_result = checked_address(&hostname).and_then(|ip| {
            let pin = match ip {
                IpAddr::V4(v4) => format!("{}:443:{}", hostname, v4),
                IpAddr::V6(v6) => format!("{}:443:[{}]", hostname, v6),
            };
            let output = Command::new("curl")
                .args([
                    "-s", "-f",
                    "--proto", "=https",   // no other scheme, and no redirect is followed
                    "-m", "8",             // max time 8 seconds
                    "--connect-timeout", "5",
                    "--max-filesize", "1048576", // max 1MB response
                    "--resolve", &pin,
                    "--",
                ])
                .arg(&url)
                .output()
                .map_err(|e| format!("curl failed: {}", e))?;
            if !output.status.success() {
                return Err(format!("HTTP request failed with status: {}", output.status));
            }
            String::from_utf8(output.stdout).map_err(|e| format!("Response encoding error: {}", e))
        });

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
    fn test_public_hostname_passes() {
        // Which domains an app may reach is the OS's (sandbox.js), not the container's
        assert!(validate_fetch_url("https://api.open-meteo.com/v1/forecast").is_ok());
    }

    #[test]
    fn test_blocked_localhost() {
        let err = validate_fetch_url("https://localhost/admin");
        assert!(err.is_err());
        let msg = err.unwrap_err();
        assert!(msg.contains("loopback"), "should mention loopback");
    }

    #[test]
    fn test_blocked_loopback_ip() {
        let err = validate_fetch_url("https://127.0.0.1/secret");
        assert!(err.is_err());
    }

    #[test]
    fn test_https_only_without_port_or_user_info() {
        // As the device's network.c: plain http, another port or user info is refused
        assert!(validate_fetch_url("http://api.open-meteo.com/v1").is_err());
        assert!(validate_fetch_url("https://api.open-meteo.com:8443/v1").is_err());
        assert!(validate_fetch_url("https://user@api.open-meteo.com/").is_err());
        assert!(validate_fetch_url("https://evil.localhost/").is_err());
        assert!(validate_fetch_url("https://api.open-meteo.com/a b").is_err());
        assert_eq!(validate_fetch_url("https://API.open-meteo.com?x=1").unwrap(), "api.open-meteo.com");
    }

    #[test]
    fn test_what_a_name_resolves_to_must_be_public() {
        // A DNS answer of the developer's own machine or network is refused
        for a in ["127.0.0.1", "10.0.0.5", "172.16.3.4", "192.168.1.1", "169.254.169.254",
                  "100.64.0.1", "0.0.0.0", "::1", "fc00::1", "fe80::1", "::ffff:10.0.0.1"] {
            assert!(!public_address(&a.parse().unwrap()), "{} is not public", a);
        }
        for a in ["93.184.216.34", "2606:2800:220:1:248:1893:25c8:1946", "::ffff:93.184.216.34"] {
            assert!(public_address(&a.parse().unwrap()), "{} is public", a);
        }
        assert!(checked_address("localhost").is_err());   // resolves to loopback
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
        let err = validate_fetch_url("https://[::1]/");
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
    fn test_cloud_metadata_blocked() {
        // AWS EC2 metadata endpoint — must be blocked
        let err = validate_fetch_url("https://169.254.169.254/latest/meta-data/");
        assert!(err.is_err());
    }
}
