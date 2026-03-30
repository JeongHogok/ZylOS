// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Sandbox service — per-app security policy
// Scope: getPolicy, apply sandbox
// Dependency Direction: Domain -> apps service, SYSTEM_APPS
// SOLID: SRP — sandbox policy only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.sandbox = function (deps) {
    var appsSvc = deps.appsSvc;
    var SYSTEM_APPS = deps.SYSTEM_APPS;
    var logFn = deps.logFn;

    return {
      getPolicy: function (p) {
        return appsSvc.getInstalled().then(function (list) {
          var app = list.find(function (a) { return a.id === (p.appId || ''); });
          return {
            appId: p.appId || '',
            system: app ? (SYSTEM_APPS.indexOf(app.id) !== -1) : false,
            permissions: 0,
            seccompProfile: 'DEFAULT'
          };
        }).catch(function () { return null; });
      },
      apply: function (p) {
        if (logFn) logFn('INFO', 'sandbox', 'Policy applied for ' + (p.appId || 'unknown'));
        return Promise.resolve(true);
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
