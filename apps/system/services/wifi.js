// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: WiFi service — wireless network scan and status
// Scope: getNetworks, connect, getConnected
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — WiFi operations only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.wifi = function (deps) {
    var invoke = deps.invoke;

    return {
      getNetworks:  function () { return invoke('get_wifi_networks'); },
      /* true once joined — same answer as the device's Connect */
      connect: function (p) {
        return invoke('wifi_connect', { ssid: p.ssid, password: p.passphrase || '' })
          .then(function (r) { return !!(r && r.success); });
      },
      getConnected: function () {
        return invoke('get_wifi_networks').then(function (nets) {
          return (nets || []).filter(function (n) { return n.connected; });
        }).catch(function () { return []; });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
