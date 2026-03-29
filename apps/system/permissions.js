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
    'com.zylos.store', 'com.zylos.keyboard',
    'com.zylos.phone', 'com.zylos.messages', 'com.zylos.contacts'
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
    'contacts':     ['contacts'],
    'messaging':    ['messaging'],
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

  /* System apps locked — cannot be overwritten by user apps */
  var _systemLocked = {};

  /* User overrides from settings (revoked permissions) */
  var _userOverrides = {};

  /* All known permission strings — system apps get all of these */
  var ALL_PERMISSIONS = [
    'storage', 'files.read', 'files.write', 'files.delete',
    'shell', 'credential', 'system', 'wifi', 'bluetooth',
    'notification', 'notification.read', 'camera', 'microphone',
    'audio', 'app.manage', 'app.list', 'app.launch',
    'location', 'network', 'webview', 'downloads', 'bookmarks',
    'input', 'i18n', 'battery', 'time', 'auth', 'wallpaper', 'gallery',
    'contacts', 'messaging', 'telephony'
  ];

  /**
   * Register an app's declared permissions (from app.json).
   * System apps receive ALL permissions automatically.
   */
  function registerApp(appId, permissions) {
    /* Reject attempts to register over a locked system app */
    if (_systemLocked[appId]) return;
    if (SYSTEM_APPS.indexOf(appId) !== -1) {
      _appPermissions[appId] = ALL_PERMISSIONS.slice();
      _systemLocked[appId] = true;
    } else {
      _appPermissions[appId] = (permissions || []).slice();
    }
  }

  /**
   * Bulk register from apps.getInstalled() response.
   * Each item should have { id, permissions: [...] }.
   * Also auto-registers all system apps with full permissions.
   */
  function registerFromAppList(appList) {
    /* Register system apps first with full permissions.
       These are locked — user apps CANNOT override them. */
    for (var s = 0; s < SYSTEM_APPS.length; s++) {
      _appPermissions[SYSTEM_APPS[s]] = ALL_PERMISSIONS.slice();
      _systemLocked[SYSTEM_APPS[s]] = true;
    }
    /* Register user apps from list.
       SECURITY: if an app claims a system app ID, it is REJECTED (spoofing defense). */
    if (!appList || !appList.length) return;
    for (var i = 0; i < appList.length; i++) {
      var app = appList[i];
      if (!app.id) continue;
      if (_systemLocked[app.id]) continue; /* Reject spoofed system app IDs */
      _appPermissions[app.id] = (app.permissions || []).slice();
    }
  }

  /**
   * Set user overrides — permissions explicitly revoked by user.
   * Format: { appId: ['camera', 'storage'], ... }
   */
  function setUserOverrides(overrides) {
    if (!overrides) { _userOverrides = {}; return; }
    /* Filter out any overrides targeting system apps — immutable */
    var safe = {};
    Object.keys(overrides).forEach(function (appId) {
      if (!_systemLocked[appId]) safe[appId] = overrides[appId];
    });
    _userOverrides = safe;
  }

  /**
   * Set override for a single app.
   * revokedPermissions: array of permission strings that are DENIED.
   * SECURITY: system apps CANNOT have permissions revoked.
   */
  function setAppOverride(appId, revokedPermissions) {
    if (_systemLocked[appId]) return; /* Immutable — reject silently */
    _userOverrides[appId] = revokedPermissions || [];
  }

  /**
   * Get effective permissions for an app (declared minus revoked).
   * System apps always return full permissions — overrides are ignored.
   */
  function getEffectivePermissions(appId) {
    var declared = _appPermissions[appId] || [];
    /* System apps: ALWAYS return full declared permissions, ignore overrides */
    if (_systemLocked[appId]) return declared.slice();
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
    var required = SERVICE_PERMISSIONS[service];
    /* Unknown service or no permission required */
    if (!required || required.length === 0) return true;

    /* All apps — including system — go through the same permission path.
       System apps have ALL_PERMISSIONS registered, so they pass naturally. */
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
