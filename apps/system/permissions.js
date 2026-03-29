// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Policy
//
// Role: Zyl OS permission check — gates service access per app
// Scope: App registration, permission verification, system app whitelist,
//        user override (revoke/grant via settings), bulk registration from app list
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

  /* Service → required permission(s). App needs at least ONE of these. */
  var SERVICE_PERMISSIONS = {
    'fs':           ['storage', 'files.read', 'files.write', 'files.delete'],
    'terminal':     ['shell'],
    'credential':   ['credential'],
    'settings':     ['system'],
    'wifi':         ['wifi'],
    'bluetooth':    ['bluetooth'],
    'notification': ['notification'],
    'camera':       ['camera'],
    'audio':        ['audio'],
    'appstore':     ['app.manage'],
    'location':     ['location'],
    /* Services with no special permission required */
    'device':        [],
    'storage':       [],
    'apps':          [],
    'browser':       [],
    'display':       [],
    'input':         [],
    'sensors':       [],
    'telephony':     [],
    'usb':           [],
    'user':          [],
    'updater':       [],
    'sandbox':       [],
    'logger':        [],
    'accessibility': [],
    'power':         []
  };

  /* App-declared permissions from app.json */
  var _appPermissions = {};

  /* User overrides from settings (revoked permissions) */
  var _userOverrides = {};

  /**
   * Register an app's declared permissions (from app.json).
   * Called during app list loading.
   */
  function registerApp(appId, permissions) {
    _appPermissions[appId] = (permissions || []).slice();
  }

  /**
   * Bulk register from apps.getInstalled() response.
   * Each item should have { id, permissions: [...] }.
   */
  function registerFromAppList(appList) {
    if (!appList || !appList.length) return;
    for (var i = 0; i < appList.length; i++) {
      var app = appList[i];
      if (app.id && app.permissions) {
        registerApp(app.id, app.permissions);
      }
    }
  }

  /**
   * Set user overrides — permissions explicitly revoked by user.
   * Format: { appId: ['camera', 'storage'], ... }
   */
  function setUserOverrides(overrides) {
    _userOverrides = overrides || {};
  }

  /**
   * Set override for a single app.
   * revokedPermissions: array of permission strings that are DENIED.
   */
  function setAppOverride(appId, revokedPermissions) {
    _userOverrides[appId] = revokedPermissions || [];
  }

  /**
   * Get effective permissions for an app (declared minus revoked).
   */
  function getEffectivePermissions(appId) {
    var declared = _appPermissions[appId] || [];
    var revoked = _userOverrides[appId] || [];
    if (revoked.length === 0) return declared.slice();
    return declared.filter(function (p) {
      return revoked.indexOf(p) === -1;
    });
  }

  /**
   * Get declared permissions for an app (from app.json).
   */
  function getDeclaredPermissions(appId) {
    return (_appPermissions[appId] || []).slice();
  }

  /**
   * Check if an app has permission to access a service.
   */
  function checkPermission(appId, service, method) {
    /* System apps bypass all permission checks */
    if (SYSTEM_APPS.indexOf(appId) !== -1) return true;

    var required = SERVICE_PERMISSIONS[service];
    /* Unknown service or no permission required */
    if (!required || required.length === 0) return true;

    /* Get effective permissions (declared - user revoked) */
    var appPerms = getEffectivePermissions(appId);
    for (var i = 0; i < required.length; i++) {
      if (appPerms.indexOf(required[i]) !== -1) return true;
    }
    return false;
  }

  /**
   * Check if a specific permission is granted for an app.
   */
  function hasPermission(appId, permission) {
    if (SYSTEM_APPS.indexOf(appId) !== -1) return true;
    var effective = getEffectivePermissions(appId);
    return effective.indexOf(permission) !== -1;
  }

  return {
    checkPermission: checkPermission,
    hasPermission: hasPermission,
    registerApp: registerApp,
    registerFromAppList: registerFromAppList,
    setUserOverrides: setUserOverrides,
    setAppOverride: setAppOverride,
    getEffectivePermissions: getEffectivePermissions,
    getDeclaredPermissions: getDeclaredPermissions,
    SYSTEM_APPS: SYSTEM_APPS,
    SERVICE_PERMISSIONS: SERVICE_PERMISSIONS
  };
})();
