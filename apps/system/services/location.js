// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Location service — GPS/IP-based location
// Scope: getLastKnown, requestUpdates, stopUpdates
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — location data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.location = function (deps) {
    var invoke = deps.invoke;

    return {
      getLastKnown:   function () { return invoke('location_get_last_known'); },
      requestUpdates: function () { return Promise.resolve(true); },
      stopUpdates:    function () { return Promise.resolve(true); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
