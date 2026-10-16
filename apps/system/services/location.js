// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Location service — GPS/IP-based location
// Scope: getLastKnown; the user's switch (settings location.enabled) — off,
//        nothing is asked of the host and no app gets a place (provider "off"),
//        as the device's runtime/services/location
// Dependency Direction: Domain -> invoker abstraction, settings service
// SOLID: SRP — location data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.location = function (deps) {
    var invoke = deps.invoke;
    var settingsSvc = deps.settingsSvc;

    function switchedOff() {
      if (!settingsSvc || !settingsSvc._getSetting) return Promise.resolve(false);
      return settingsSvc._getSetting('location').then(function (l) {
        return !!l && l.enabled === false;
      }, function () { return false; });
    }

    return {
      getLastKnown: function () {
        return switchedOff().then(function (off) {
          if (off) return { latitude: 0, longitude: 0, altitude: 0, accuracy: 0, speed: 0,
                            bearing: 0, timestamp: 0, provider: 'off' };
          return invoke('location_get_last_known');
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
