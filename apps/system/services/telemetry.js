// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Telemetry consent service — the one switch that decides whether any
//       diagnostic data may be collected
// Scope: getEnabled, setEnabled. Opt-in: never asked means off.
// Dependency Direction: Domain -> settingsSvc (persistence)
// SOLID: SRP — consent only; collection itself is the native daemon's job
//
// On a device these method names reach runtime/services/telemetry over D-Bus
// (org.zylos.Telemetry.GetEnabled / SetEnabled), which persists consent and
// erases collected data on withdrawal. In the emulator there is no collector,
// so consent is kept in the OS settings store with the same contract.
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.telemetry = function (deps) {
    var settingsSvc = deps.settingsSvc;

    function getEnabled() {
      if (!settingsSvc) return Promise.resolve(false);
      return Promise.resolve(settingsSvc._getSetting('privacy')).then(function (p) {
        /* Only an explicit true is consent — absent or anything else is not */
        return !!(p && p.telemetry === true);
      });
    }

    function setEnabled(params) {
      var on = !!(params && params.enabled === true);
      if (settingsSvc) settingsSvc._updateSetting('privacy', 'telemetry', on);
      return Promise.resolve(on);
    }

    return {
      getEnabled: function () { return getEnabled(); },
      setEnabled: function (p) { return setEnabled(p); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
