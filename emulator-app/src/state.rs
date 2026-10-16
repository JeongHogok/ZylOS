// ──────────────────────────────────────────────────────────
// [Clean Architecture] Domain Layer - State
//
// 역할: 에뮬레이터 앱 전역 상태 정의
// 수행범위: DeviceConfig, AppState 구조체 및 직렬화
// 의존방향: 없음 (순수 도메인 타입)
// SOLID: SRP — 상태 정의만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

/// On-screen navigation chrome the emulator draws for a device.
/// `None` covers devices with no nav chrome at all (TV, watch, dashboard).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum NavMode {
    Gesture,
    Softkeys,
    Hardware,
    None,
}

impl std::fmt::Display for NavMode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            NavMode::Gesture => write!(f, "gesture"),
            NavMode::Softkeys => write!(f, "softkeys"),
            NavMode::Hardware => write!(f, "hardware"),
            NavMode::None => write!(f, "none"),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceProfile {
    pub id: String,
    pub name: String,
    pub description: String,
    pub soc: String,
    pub ram_options: Vec<u32>,
    pub screen_width: u32,
    pub screen_height: u32,
    pub screen_label: String,
    pub nav_mode: NavMode,
    /// Primary input hardware, passed to the OS as a form-factor hint.
    /// The OS classifies the form factor; the emulator never declares one.
    pub input_model: String,
    pub has_notch: bool,
    pub frame_width: u32,
    pub frame_height: u32,
    pub frame_radius: u32,
}

/// Saved configs written before input_model existed must still load.
fn default_input_model() -> String {
    "touch".to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceConfig {
    pub profile_id: String,
    pub name: String,
    pub screen_width: u32,
    pub screen_height: u32,
    pub frame_width: u32,
    pub frame_height: u32,
    pub frame_radius: u32,
    pub nav_mode: NavMode,
    /// Carried from the profile so the booted session can hand the OS its
    /// input-hardware hint (see apps/shared/form-factor.js).
    #[serde(default = "default_input_model")]
    pub input_model: String,
    pub has_notch: bool,
    pub storage_gb: u32,
    pub ram_mb: u32,
    pub os_version: String,
    pub soc: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SavedDevice {
    pub config: DeviceConfig,
    pub image_path: Option<String>,
    pub created_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ResourceInfo {
    pub mount_point: String,
    pub disk_image_path: String,
    pub ram_limit_mb: u32,
    pub storage_total_bytes: u64,
    pub storage_used_bytes: u64,
    pub storage_available_bytes: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BootInfo {
    pub profile_id: String,
    pub mount_point: String,
    pub os_image_mount: String,
    pub ram_limit_mb: u32,
    pub nav_mode: String,
    pub screen_width: u32,
    pub screen_height: u32,
    /// The virtual device's own ID (a real device's chip ID): what development
    /// provisioning profiles list
    pub device_id: String,
}

#[derive(Debug)]
pub struct AppState {
    pub config: Option<DeviceConfig>,
    pub booted: bool,
    pub disk_image_path: Option<PathBuf>,
    pub mount_point: Option<PathBuf>,
    pub os_image_mount: Option<PathBuf>,
    pub resource_reserved: bool,
    pub data_dir: PathBuf,
    /// The CSP each store app's files are served with — the OS's (sandbox.js)
    pub app_policies: std::collections::HashMap<String, String>,
}

impl AppState {
    pub fn new() -> Self {
        let data_dir = dirs::data_local_dir()
            .unwrap_or_else(|| std::env::temp_dir())
            .join("zyl-emulator");

        Self {
            config: None,
            booted: false,
            disk_image_path: None,
            mount_point: None,
            os_image_mount: None,
            resource_reserved: false,
            data_dir,
            app_policies: std::collections::HashMap::new(),
        }
    }
}
