// ----------------------------------------------------------
// [Clean Architecture] Infrastructure Layer - IPC Router
//
// Role: Emulator IPC bridge — connects OS service framework to Tauri backend
// Scope: Tauri invoke wrapper, permission gate, security gate, delegation to ZylSystemServices
// Dependency Direction: Infrastructure -> Domain (delegates to OS service framework)
// SOLID: SRP — IPC routing only (zero business logic), DIP — depends on OS abstractions
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// The emulator provides a real device runtime environment and must not contain OS image content
// ----------------------------------------------------------

var ZylServices = (function () {
  'use strict';

  var IS_TAURI = (typeof window !== 'undefined' && typeof window.__TAURI__ !== 'undefined');

  function tauriInvoke(cmd, args) {
    if (!IS_TAURI) return Promise.resolve(null);
    try {
      return window.__TAURI__.core.invoke(cmd, args || {});
    } catch (e) {
      return Promise.resolve(null);
    }
  }

  /* Initialize OS service framework with hardware access */
  if (typeof ZylSystemServices !== 'undefined') {
    ZylSystemServices.init(tauriInvoke);
  }


  /* ===============================================================
     Request Handler — permission + security gates, then delegate
     =============================================================== */
  function handleRequest(service, method, params, appId) {
    /* All security checks (permissions, file protection) are handled
       by the OS service framework (apps/system/). Emulator is just an IPC relay. */

    /* Delegate to OS service framework */
    if (typeof ZylSystemServices !== 'undefined') {
      return ZylSystemServices.handleRequest(service, method, params, appId);
    }

    return null;
  }


  /* ===============================================================
     Public API — thin proxy to OS service framework
     =============================================================== */
  return {
    handleRequest: handleRequest,
    tauriInvoke: tauriInvoke,
    get device() { return typeof ZylSystemServices !== 'undefined' ? ZylSystemServices.device : null; },
    get storage() { return typeof ZylSystemServices !== 'undefined' ? ZylSystemServices.storage : null; },
    get settings() { return typeof ZylSystemServices !== 'undefined' ? ZylSystemServices.settings : null; },
    get apps() { return typeof ZylSystemServices !== 'undefined' ? ZylSystemServices.apps : null; },
    get fs() { return typeof ZylSystemServices !== 'undefined' ? ZylSystemServices.fs : null; },
    get applyDisplayProfile() { return typeof ZylSystemServices !== 'undefined' ? ZylSystemServices.applyDisplayProfile : function () {}; }
  };
})();
