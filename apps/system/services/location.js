// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Location service — GPS/IP-based location
// Scope: getLastKnown
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — location data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.location = function (deps) {
    var invoke = deps.invoke;

    return {
      getLastKnown: function () {
        return invoke('location_get_last_known');
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
