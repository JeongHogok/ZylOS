// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Updater service — system update state for Settings
// Scope: getState, check, install, restart
// Dependency Direction: Domain -> device info
// SOLID: SRP — OS update lifecycle only
//
// On a device these reach runtime/services/updater (org.zylos.Updater) with
// the same answer: { state, progress, version, available }. The emulator boots
// an OS image file — a new version is a new image, never an update installed
// in place — so here the state is always 'unsupported'.
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.updater = function (deps) {
    var deviceRef = deps.deviceRef;

    function state() {
      return Promise.resolve({ state: 'unsupported', progress: 0, version: deviceRef.osVersion, available: '' });
    }

    return { getState: state, check: state, install: state, restart: state };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
