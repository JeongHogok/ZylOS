// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Boot control — bootloader lock state and unlock requests
// Scope: getState, setUnlockAllowed, requestLockState
// Dependency Direction: Domain -> none
// SOLID: SRP — boot control contract only
//
// On a device these reach runtime/services/bootctl (org.zylos.BootControl),
// which records the request in the U-Boot environment; the bootloader then
// asks for a button press and erases user data before changing state. The
// emulator has no bootloader, so it reports supported:false and refuses.
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.bootctl = function () {
    return {
      getState: function () { return Promise.resolve({ supported: false }); },
      setUnlockAllowed: function () { return Promise.resolve(false); },
      requestLockState: function () { return Promise.resolve(false); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
