// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: USB service — USB mode and connection status
// Scope: getMode, setMode, isConnected
// Dependency Direction: Domain -> none (in-memory state)
// SOLID: SRP — USB state only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.usb = function (/* deps */) {
    var usbState = { mode: 'charging', connected: true };

    return {
      getMode: function () { return Promise.resolve(usbState.mode); },
      setMode: function (p) {
        usbState.mode = p.mode || 'charging';
        return Promise.resolve(usbState.mode);
      },
      isConnected: function () { return Promise.resolve(usbState.connected); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
