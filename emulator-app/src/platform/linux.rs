// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Platform (Linux)
//
// 역할: Linux 전용 시스템 정보 조회
// 수행범위: /sys/class/power_supply 배터리 정보
// 의존방향: std::fs (sysfs 읽기)
// SOLID: SRP — Linux 플랫폼 어댑터만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use std::fs;

pub fn get_battery_info() -> Result<serde_json::Value, String> {
    let bat_path = "/sys/class/power_supply/BAT0";

    if !std::path::Path::new(bat_path).exists() {
        return Ok(serde_json::json!({
            "level": 100,
            "charging": true,
            "health": "Good",
            "temperature": 25.0,
            "voltage": 4200
        }));
    }

    let capacity = fs::read_to_string(format!("{}/capacity", bat_path))
        .unwrap_or_else(|_| "100".into())
        .trim()
        .parse::<u32>()
        .unwrap_or(100);

    let status = fs::read_to_string(format!("{}/status", bat_path))
        .unwrap_or_else(|_| "Charging".into())
        .trim()
        .to_string();

    let charging = status == "Charging" || status == "Full";

    let temp = fs::read_to_string(format!("{}/temp", bat_path))
        .unwrap_or_else(|_| "250".into())
        .trim()
        .parse::<f64>()
        .unwrap_or(250.0)
        / 10.0;

    let voltage = fs::read_to_string(format!("{}/voltage_now", bat_path))
        .unwrap_or_else(|_| "4200000".into())
        .trim()
        .parse::<u64>()
        .unwrap_or(4200000)
        / 1000;

    Ok(serde_json::json!({
        "level": capacity,
        "charging": charging,
        "health": "Good",
        "temperature": temp,
        "voltage": voltage
    }))
}
