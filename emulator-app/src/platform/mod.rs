// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Platform
//
// 역할: OS별 플랫폼 유틸리티 디스패치
// 수행범위: cfg(target_os) 기반 분기
// 의존방향: 없음
// SOLID: OCP — 새 플랫폼 추가 시 모듈만 추가
// ──────────────────────────────────────────────────────────

#[cfg(target_os = "linux")]
pub mod linux;

#[cfg(target_os = "macos")]
pub mod macos;

/// 현재 플랫폼 이름
pub fn platform_name() -> &'static str {
    #[cfg(target_os = "linux")]
    { "linux" }
    #[cfg(target_os = "macos")]
    { "macos" }
}

/// 배터리 상태 조회 (플랫폼 독립)
pub fn get_battery_info() -> Result<serde_json::Value, String> {
    #[cfg(target_os = "linux")]
    { linux::get_battery_info() }
    #[cfg(target_os = "macos")]
    { macos::get_battery_info() }
}
