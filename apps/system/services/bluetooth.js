// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Bluetooth service — device scanning and connection status
// Scope: getDevices, getPaired, getConnected
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — Bluetooth operations only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.bluetooth = function (deps) {
    var invoke = deps.invoke;

    return {
      getDevices:   function () { return invoke('get_bluetooth_devices'); },
      getPaired:    function () { return invoke('get_bluetooth_devices'); },
      getConnected: function () {
        return invoke('get_bluetooth_devices').then(function (devs) {
          return (devs || []).filter(function (d) { return d.connected; });
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
