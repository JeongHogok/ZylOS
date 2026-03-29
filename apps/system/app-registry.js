// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Registry
//
// Role: OS-level application registry — single source of truth for
//       installed apps, paths, icons, dock config, and visibility policy
// Scope: App registration from manifests, path resolution, icon lookup,
//        OS policy (default dock, hidden-from-grid, undeletable)
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

  var _apps = {}; // appId -> {name, path, system, icon, color, permissions, iconSvg}

  /* ═══ OS Policy Config ═══ */
  var OS_CONFIG = {
    defaultDock: ['com.zylos.phone', 'com.zylos.contacts', 'com.zylos.messages', 'com.zylos.settings'],
    hiddenFromGrid: ['com.zylos.lockscreen', 'com.zylos.statusbar', 'com.zylos.oobe', 'com.zylos.home', 'com.zylos.keyboard'],
    /* Apps excluded from recents/multitasking — system UI components */
    excludeFromRecents: ['com.zylos.lockscreen', 'com.zylos.statusbar', 'com.zylos.oobe'],
    /* undeletable is dynamically derived from app.system flag */
  };

  /* ═══ Registration ═══ */
  function register(appList) {
    (appList || []).forEach(function (app) {
      var appName = (app.id || '').split('.').pop();
      _apps[app.id] = {
        name: app.name || appName,
        path: 'apps/' + appName + '/index.html',
        system: app.system || false,
        icon: app.icon || appName,
        color: app.color || 'icon-blue',
        permissions: app.permissions || [],
        iconSvg: app.iconSvg || ''
      };
    });
    /* Also register hidden system apps that may not appear in getInstalled */
    var hiddenSys = {
      'com.zylos.oobe':       { name: 'Setup',       path: 'apps/oobe/index.html' },
      'com.zylos.lockscreen': { name: 'Lock Screen', path: 'apps/lockscreen/index.html' },
      'com.zylos.statusbar':  { name: 'Status Bar',  path: 'apps/statusbar/index.html' },
      'com.zylos.home':       { name: 'Home',        path: 'apps/home/index.html' },
      'com.zylos.keyboard':   { name: 'Keyboard',    path: 'apps/keyboard/index.html' }
    };
    Object.keys(hiddenSys).forEach(function (id) {
      if (!_apps[id]) {
        _apps[id] = {
          name: hiddenSys[id].name,
          path: hiddenSys[id].path,
          system: true,
          icon: '',
          color: '',
          permissions: [],
          iconSvg: ''
        };
      }
    });
  }

  /* ═══ Lookup ═══ */
  function getApp(appId) { return _apps[appId] || null; }
  function getPath(appId) { var a = _apps[appId]; return a ? a.path : null; }
  function getAllIds() { return Object.keys(_apps); }

  /* ═══ OS Policy ═══ */
  function isHiddenFromGrid(appId) {
    return OS_CONFIG.hiddenFromGrid.indexOf(appId) !== -1;
  }

  function isUndeletable(appId) {
    var app = _apps[appId];
    return app ? app.system : false;
  }

  function getDefaultDock() {
    return OS_CONFIG.defaultDock.slice();
  }

  function isExcludedFromRecents(appId) {
    return OS_CONFIG.excludeFromRecents.indexOf(appId) !== -1;
  }

  return {
    register: register,
    getApp: getApp,
    getPath: getPath,
    getAllIds: getAllIds,
    isHiddenFromGrid: isHiddenFromGrid,
    isUndeletable: isUndeletable,
    getDefaultDock: getDefaultDock,
    isExcludedFromRecents: isExcludedFromRecents
  };
})();
