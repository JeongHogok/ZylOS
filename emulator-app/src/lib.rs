// ──────────────────────────────────────────────────────────
// [Clean Architecture] Core Layer - Library Entry
//
// 역할: 단위 테스트를 위한 라이브러리 진입점. 프로덕션 빌드에는 포함되지 않음.
// 수행범위: 내부 모듈을 cargo test에서 접근 가능하게 re-export
// 의존방향: 없음 (모듈 re-export만)
// SOLID: SRP — 테스트 접근성만 담당
// ──────────────────────────────────────────────────────────

pub mod commands;
pub mod platform;
pub mod resource;
pub mod state;
