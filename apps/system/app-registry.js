// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Registry
//
// Role: OS-level application registry — single source of truth for
//       installed apps, paths, icons, dock config, and visibility policy
// Scope: App registration from manifests, path resolution, icon lookup,
//        recents policy. Launcher visibility is the manifest's "launcher",
//        the default dock is a setting (home.dock), deletability is "system"
// Dependency Direction: Domain -> none (pure registry, populated by service layer)
// SOLID: SRP — app metadata registry only
//        OCP — new apps added via register() without modifying existing code
//        DIP — consumers depend on registry abstraction, not hardcoded lists
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylAppRegistry = (function () {
  'use strict';

  var _apps = {}; // appId -> {name, path, system, icon, color, permissions, formFactors, iconSvg, launcher, intents}

  /* ═══ Registration: the installed apps, all of them — a removed app goes ═══ */
  function register(appList) {
    _apps = {};
    (appList || []).forEach(function (app) {
      var appName = (app.id || '').split('.').pop();
      _apps[app.id] = {
        name: app.name || appName,
        /* A store app is its own origin, as on the device (zyl-app://<id>/) */
        path: app.source === 'store'
          ? 'zyl-app://' + app.id + '/' + (app.entry || 'index.html')
          : 'apps/' + appName + '/index.html',
        origin: app.source === 'store' ? 'zyl-app://' + app.id : '',
        system: app.system || false,
        icon: app.icon || appName,
        color: app.color || 'icon-blue',
        permissions: app.permissions || [],
        formFactors: app.formFactors || [],
        iconSvg: app.iconSvg || '',
        launcher: app.launcher !== false,
        intents: Array.isArray(app.intents) ? app.intents : []
      };
    });
  }

  /* ═══ Lookup ═══ */
  function getApp(appId) { return _apps[appId] || null; }
  function getPath(appId) { var a = _apps[appId]; return a ? a.path : null; }
  /* '' for the image's apps (the system page's own origin) */
  function getOrigin(appId) { var a = _apps[appId]; return a ? a.origin : ''; }
  function getAllIds() { return Object.keys(_apps); }

  /* ═══ OS Policy ═══ */
  /* System UI (lock screen, status bar, setup, home) is not an app one
     returns to: the same manifest fact that keeps it out of the launcher */
  function isExcludedFromRecents(appId) {
    var app = _apps[appId];
    return !!app && app.launcher === false;
  }

  return {
    register: register,
    getApp: getApp,
    getPath: getPath,
    getOrigin: getOrigin,
    getAllIds: getAllIds,
    isExcludedFromRecents: isExcludedFromRecents
  };
})();
