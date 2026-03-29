// ──────────────────────────────────────────────────────────
// [Clean Architecture] Application Layer - Module Registry
//
// 역할: 커맨드 모듈 공개 선언
// 수행범위: 하위 커맨드 모듈 re-export
// 의존방향: 없음
// SOLID: OCP — 새 커맨드 모듈 추가만으로 확장
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

pub mod boot;
pub mod config;
pub mod filesystem;
pub mod network;
pub mod os_image;
pub mod resource;
pub mod settings;
pub mod terminal;
