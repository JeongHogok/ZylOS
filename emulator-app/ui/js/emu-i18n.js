// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Service
//
// Role: The virtual device's language, as the OS last said (Settings,
//       language.locale) — the container hands it to every page it loads
// Scope: getLocale, setLocale. The system UI the emulator used to draw (QS
//        panel, notification panel, recents) is the OS's status bar app now
//        and translates itself; nothing here is shown to the user
// Dependency: None
// SOLID: SRP — the current locale only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// The emulator provides a real device runtime environment and must not contain OS image content
// ──────────────────────────────────────────────────────────

window.ZylEmuI18n = (function () {
  'use strict';

  var currentLocale = 'en';

  return {
    setLocale: function (locale) { if (locale) currentLocale = String(locale); },
    getLocale: function () { return currentLocale; }
  };
})();
