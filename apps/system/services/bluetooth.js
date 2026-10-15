// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Bluetooth service — the emulator's virtual adapter, same contract as
//       runtime/services/bluetooth (org.zylos.BluetoothService)
// Scope: getCapabilities, getState, setEnabled, startScan, stopScan,
//        getDevices, pair, connect, disconnect, remove
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — Bluetooth device management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.bluetooth = function (deps) {
    var invoke = deps.invoke;

    function ok(r) { return !!(r && (r.success === undefined || r.success)); }

    return {
      getCapabilities: function () { return Promise.resolve({ present: true }); },
      getState: function () {
        return invoke('bt_get_state').then(function (s) { return { enabled: !!(s && s.enabled) }; });
      },
      setEnabled: function (p) {
        var on = !!(p && p.enabled);
        return invoke('bt_set_enabled', { enabled: on }).then(function (r) { return !!r && r.enabled === on; });
      },
      startScan: function () { return Promise.resolve(true); },
      stopScan: function () { return Promise.resolve(); },
      getDevices: function () { return invoke('get_bluetooth_devices'); },
      pair: function (p) { return invoke('bt_pair', { address: p.address }).then(ok); },
      connect: function (p) { return invoke('bt_connect', { address: p.address }).then(ok); },
      disconnect: function (p) { return invoke('bt_disconnect', { address: p.address }).then(ok); },
      remove: function (p) { return invoke('bt_unpair', { address: p.address }).then(ok); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
