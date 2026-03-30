// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: WiFi service — wireless network scan and status
// Scope: getNetworks, getConnected
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — WiFi operations only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.wifi = function (deps) {
    var invoke = deps.invoke;

    return {
      getNetworks:  function () { return invoke('get_wifi_networks'); },
      getConnected: function () {
        return invoke('get_wifi_networks').then(function (nets) {
          return (nets || []).filter(function (n) { return n.connected; });
        }).catch(function () { return []; });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
