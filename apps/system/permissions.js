// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Policy
//
// Role: Zyl OS permission check — gates service access per app
// Scope: App registration, permission verification, system app whitelist
// Dependency Direction: Domain -> none (pure policy logic)
// SOLID: SRP — permission checking only, OCP — new permissions via map extension
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylPermissions = (function () {
  'use strict';

  var SYSTEM_APPS = [
    'com.zylos.home', 'com.zylos.lockscreen', 'com.zylos.statusbar',
    'com.zylos.oobe', 'com.zylos.settings', 'com.zylos.browser',
    'com.zylos.files', 'com.zylos.terminal', 'com.zylos.camera',
    'com.zylos.gallery', 'com.zylos.music', 'com.zylos.clock',
    'com.zylos.calc', 'com.zylos.notes', 'com.zylos.weather',
    'com.zylos.store', 'com.zylos.keyboard'
  ];

  var SERVICE_PERMISSIONS = {
    'fs':           ['storage', 'files.read', 'files.write'],
    'terminal':     ['shell'],
    'credential':   ['credential'],
    'settings':     ['system'],
    'wifi':         ['wifi'],
    'bluetooth':    ['bluetooth'],
    'notification': ['notification'],
    'camera':       ['camera'],
    'audio':        ['audio'],
    'appstore':     ['app.manage'],
    /* Services with no special permission required */
    'device':        [],
    'storage':       [],
    'apps':          [],
    'browser':       [],
    'display':       [],
    'input':         [],
    'sensors':       [],
    'location':      [],
    'telephony':     [],
    'usb':           [],
    'user':          [],
    'updater':       [],
    'sandbox':       [],
    'logger':        [],
    'accessibility': [],
    'power':         []
  };

  var _appPermissions = {}; /* cached: { appId: ['storage', 'camera', ...] } */

  function registerApp(appId, permissions) {
    _appPermissions[appId] = permissions || [];
  }

  function checkPermission(appId, service, method) {
    /* System apps bypass all permission checks */
    if (SYSTEM_APPS.indexOf(appId) !== -1) return true;

    var required = SERVICE_PERMISSIONS[service];
    /* Unknown service or no permission required */
    if (!required || required.length === 0) return true;

    var appPerms = _appPermissions[appId] || [];
    for (var i = 0; i < required.length; i++) {
      if (appPerms.indexOf(required[i]) !== -1) return true;
    }
    return false;
  }

  return {
    checkPermission: checkPermission,
    registerApp: registerApp,
    SYSTEM_APPS: SYSTEM_APPS
  };
})();
