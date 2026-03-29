// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Platform (macOS)
//
// 역할: macOS 전용 시스템 정보 조회
// 수행범위: pmset 기반 배터리 정보
// 의존방향: std::process::Command (pmset)
// SOLID: SRP — macOS 플랫폼 어댑터만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use std::process::Command;

pub fn get_battery_info() -> Result<serde_json::Value, String> {
    let output = Command::new("pmset")
        .args(["-g", "batt"])
        .output()
        .map_err(|e| format!("pmset failed: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut level: u32 = 100;
    let mut charging = true;

    for line in stdout.lines() {
        let trimmed = line.trim();
        // "InternalBattery-0 (id=...)  85%; charging; 1:30 remaining"
        if trimmed.contains("InternalBattery") || trimmed.contains('%') {
            if let Some(pct_pos) = trimmed.find('%') {
                // '%' 앞의 숫자 파싱
                let before = &trimmed[..pct_pos];
                if let Some(num_start) = before.rfind(|c: char| !c.is_ascii_digit()) {
                    level = before[num_start + 1..].parse().unwrap_or(100);
                }
            }
            charging = trimmed.contains("charging") && !trimmed.contains("discharging");
        }
    }

    Ok(serde_json::json!({
        "level": level,
        "charging": charging,
        "health": "Good",
        "temperature": 25.0,
        "voltage": 4200
    }))
}
