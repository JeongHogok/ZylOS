// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Release Identity
//
// Role: The single source of the OS image's identity — version and security
//       patch level
// Scope: Read by the device service (Settings > About), meson (C services,
//        /etc/zyl-release) and the OTA updater. tests/verify-all.sh fails if
//        meson.build's project version disagrees with this file.
// Dependency Direction: Domain -> none (pure data)
// SOLID: SRP — release identity only
//
// securityPatch is the date of the newest security fix included, in the
// format Android uses (YYYY-MM-DD). Bump it when a security fix lands —
// carriers and device-management systems compare it, and users read it to
// know whether a device is current.
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylRelease = {
  version: '0.1.0',
  securityPatch: '2026-10-11'
};
