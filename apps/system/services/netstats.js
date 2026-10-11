// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Data usage service — usage per network this billing cycle, and the
//       billing cycle day, warning and mobile data limit
// Scope: getUsage, getPolicy, setPolicy
// Dependency Direction: Domain -> settingsSvc (persistence)
// SOLID: SRP — data usage policy only; accounting is the native daemon's job
//
// On a device these reach runtime/services/netstats over D-Bus
// (org.zylos.NetStats), which counts /sys/class/net per network kind and
// cuts mobile data at the limit. The emulator has no device traffic of its
// own to count, so usage reports supported:false — never invented numbers —
// while the policy is kept in the OS settings store with the same contract.
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.netstats = function (deps) {
    var settingsSvc = deps.settingsSvc;

    function bytes(v) {
      var n = Number(v);
      return isFinite(n) && n > 0 ? Math.floor(n) : 0;
    }

    function getPolicy() {
      if (!settingsSvc) return Promise.resolve({ cycleDay: 1, warningBytes: 0, limitBytes: 0 });
      return Promise.resolve(settingsSvc._getSetting('datausage')).then(function (d) {
        d = d || {};
        var day = Number(d.cycleDay);
        return {
          cycleDay: day >= 1 && day <= 28 ? day : 1,
          warningBytes: bytes(d.warningBytes),
          limitBytes: bytes(d.limitBytes)
        };
      });
    }

    function setPolicy(p) {
      var day = Number(p && p.cycleDay);
      /* Same rule as the daemon: the day must exist in every month */
      if (!(day >= 1 && day <= 28) || Math.floor(day) !== day) {
        return Promise.reject(new Error('cycleDay must be 1-28'));
      }
      if (settingsSvc) {
        settingsSvc._updateSetting('datausage', 'cycleDay', day);
        settingsSvc._updateSetting('datausage', 'warningBytes', bytes(p.warningBytes));
        settingsSvc._updateSetting('datausage', 'limitBytes', bytes(p.limitBytes));
      }
      return Promise.resolve({ ok: true });
    }

    return {
      getUsage: function () { return Promise.resolve({ supported: false }); },
      getPolicy: function () { return getPolicy(); },
      setPolicy: function (p) { return setPolicy(p); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
