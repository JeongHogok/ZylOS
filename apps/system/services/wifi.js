// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: WiFi service — the emulator's virtual radio, same contract as
//       runtime/services/wifi (org.zylos.WifiService)
// Scope: getCapabilities, getState, setEnabled, getNetworks, connect,
//        disconnect, getHotspot, setHotspot
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — wireless network management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.wifi = function (deps) {
    var invoke = deps.invoke;
    /* The virtual radio has no access point mode */
    var NO_HOTSPOT = { ok: false, reason: 'unsupported', active: false, ssid: '', passphrase: '' };

    return {
      getCapabilities: function () { return Promise.resolve({ present: true, hotspot: false }); },
      getState: function () {
        return invoke('wifi_get_state').then(function (s) {
          s = s || {};
          return { enabled: !!s.enabled, connected: !!s.connected, ssid: s.currentSsid || '' };
        });
      },
      /* true once the radio is in the asked state — same answer as the device */
      setEnabled: function (p) {
        var on = !!(p && p.enabled);
        return invoke('wifi_set_enabled', { enabled: on }).then(function (r) { return !!r && r.enabled === on; });
      },
      getNetworks:  function () { return invoke('get_wifi_networks'); },
      connect: function (p) {
        return invoke('wifi_connect', { ssid: p.ssid, password: p.passphrase || '' })
          .then(function (r) { return !!(r && r.success); });
      },
      disconnect: function () { return invoke('wifi_disconnect'); },
      getHotspot: function () {
        return Promise.resolve({ ok: true, reason: '', active: false, ssid: '', passphrase: '' });
      },
      setHotspot: function () { return Promise.resolve(NO_HOTSPOT); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
