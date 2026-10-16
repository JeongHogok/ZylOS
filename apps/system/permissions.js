// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Policy
//
// Role: Zyl OS permission check — gates service access per app
// Scope: App registration, permission verification, system app whitelist,
//        user override (revoke/grant via settings), bulk registration from app list,
//        which dangerous permission a call needs the user's yes for (consentNeeded —
//        asked by apps/system/consent.js, answered in the system UI)
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
    'com.zylos.files', 'com.zylos.camera',
    'com.zylos.gallery', 'com.zylos.music', 'com.zylos.clock',
    'com.zylos.calc', 'com.zylos.notes', 'com.zylos.weather',
    'com.zylos.store', 'com.zylos.keyboard',
    'com.zylos.phone', 'com.zylos.messages', 'com.zylos.contacts',
    'com.zylos.alerts'
  ];

  /* Service → required permission(s). App needs at least ONE of these. */
  var SERVICE_PERMISSIONS = {
    'fs':           ['storage', 'files.read', 'files.write', 'files.delete'],
    'credential':   ['credential'],
    'settings':     ['system'],
    'wifi':         ['wifi'],
    'bluetooth':    ['bluetooth'],
    'notification': ['notification'],
    /* An alarm is a notification the user gets later: same permission */
    'alarm':        ['notification'],
    'camera':       ['camera'],
    'audio':        ['audio'],
    'appstore':     ['app.manage'],
    'contacts':     ['contacts'],
    'messaging':    ['messaging'],
    'network':      ['network'],
    /* telephony is the network's state (operator, signal); calls and SMS are
       their own permissions (METHOD_PERMISSIONS, EVENT_PERMISSIONS) */
    'telephony':    ['telephony', 'calls', 'sms'],
    'location':     ['location'],
    /* Only system apps (OOBE, Settings) may read or change telemetry consent —
       a third-party app able to switch collection on would void the consent */
    'telemetry':    ['system'],
    /* Usage per network and the mobile data limit: Settings and the status
       bar only — the limit switches mobile data off for every app */
    'netstats':     ['system'],
    /* Bootloader lock: only Settings may ask — an unlock erases the device */
    'bootctl':      ['system'],
    /* Screen lock PIN: lock screen, Settings and setup only */
    'lock':         ['system'],
    /* Installs and restarts into a new OS: Settings only */
    'updater':      ['system'],
    /* Adds, switches and removes users — removing one deletes their data */
    'user':         ['system'],
    /* Services with no special permission required */
    'device':        [],
    'storage':       [],
    'apps':          [],
    'display':       [],
    'input':         [],
    'sensors':       [],
    'usb':           [],
    'sandbox':       [],
    'logger':        [],
    'accessibility': [],
    'clipboard':     [],
    /* Every app has its own, named by the OS's caller — never another's */
    'appdata':       [],
    /* An app's server reaching it through the OS's one push connection */
    'push':          ['push'],
    'power':         [],
    /* Embedding a web content view is the OS's browser only (reserved) */
    'webcontent':    ['webcontent']
  };

  /* Methods that reach past the caller's own, checked after the service rule:
     every app's notifications and the device-wide quiet settings. An app posts
     and cancels its own (its appId is always its own); reading, dismissing or
     silencing other apps' is the status bar's. runtime/wam/src/bridge/bridge.c has the
     same table (verify-all compares them). */
  var METHOD_PERMISSIONS = {
    'notification.getActive':         ['notification.read'],
    'notification.getHistory':        ['notification.read'],
    'notification.getAllBadgeCounts': ['notification.read'],
    'notification.getGroupSummary':   ['notification.read'],
    'notification.clearAll':          ['notification.read'],
    'notification.clearHistory':      ['notification.read'],
    'notification.invokeAction':      ['notification.read'],
    'notification.setDndMode':        ['system'],
    'notification.setChannelEnabled': ['system'],
    /* The chip's ID outlives a factory reset — a tracking ID if every app read it */
    'device.getDeviceId':             ['system'],
    /* The emergency alert tone: full volume through mute and silent mode */
    'audio.playTone':                 ['system'],
    /* The device's power is the OS's: any app may read it, only system apps
       turn the screen, sleep, shut down or reboot it, or hold it awake */
    'power.screenOff':                ['system'],
    'power.screenOn':                 ['system'],
    'power.userActivity':             ['system'],
    'power.suspend':                  ['system'],
    'power.shutdown':                 ['system'],
    'power.reboot':                   ['system'],
    'power.setBrightness':            ['system'],
    'power.acquireWakelock':          ['system'],
    'power.releaseWakelock':          ['system'],
    /* The device's settings: any app may read them, only system apps change
       them (USB mode, display, accessibility, input). The log is the OS's — it
       names every app */
    'usb.setMode':                    ['system'],
    'display.setMode':                ['system'],
    'display.setScale':               ['system'],
    'display.setRotation':            ['system'],
    'display.setAutoRotate':          ['system'],
    'accessibility.setHighContrast':  ['system'],
    'accessibility.setFontScale':     ['system'],
    'accessibility.setScreenReader':  ['system'],
    'input.showKeyboard':             ['system'],
    'input.hideKeyboard':             ['system'],
    'input.switchLayout':             ['system'],
    'logger.getLogs':                 ['system'],
    'logger.getCrashReports':         ['system'],
    'logger.setFilterLevel':          ['system'],
    /* Which apps are on the device is the user's to give (app.list); bringing
       one forward is app.launch — another app's data goes in an intent. What is
       in front, panels, recents and app switching are the OS's own */
    'apps.getInstalled':              ['app.list'],
    'apps.listApps':                  ['app.list'],
    'apps.listRunning':               ['app.list'],
    'apps.launch':                    ['app.launch'],
    'apps.suspend':                   ['system'],
    'apps.resume':                    ['system'],
    'apps.goHome':                    ['system'],
    'apps.goBack':                    ['system'],
    'apps.openPanel':                 ['system'],
    'apps.showRecents':               ['system'],
    'apps.switchApp':                 ['system'],
    /* Calls (dialling, answering, the call log) and SMS are permissions of their
       own, each asked for; the emergency alerts, mobile data and the modem's
       hardware ID (IMEI — a tracking ID, as the chip's) are the OS's */
    'telephony.dial':                 ['calls'],
    'telephony.answer':               ['calls'],
    'telephony.hangup':               ['calls'],
    'telephony.getCallState':         ['calls'],
    'telephony.sendDTMF':             ['calls'],
    'telephony.getCallLog':           ['calls'],
    'telephony.deleteCallLog':        ['calls'],
    'telephony.sendSms':              ['sms'],
    'telephony.getImei':              ['system'],
    'telephony.setMobileData':        ['system'],
    'telephony.getEmergencyAlerts':   ['system'],
    'telephony.acknowledgeEmergencyAlert': ['system'],
    'telephony.getEmergencyAlertSettings': ['system'],
    'telephony.setEmergencyAlertEnabled':  ['system']
  };

  /* Events that need more than their service: who is calling, what an SMS says,
     an emergency alert. The same table as EVENT_PERMISSIONS in bridge.c
     (verify-all compares them) */
  var EVENT_PERMISSIONS = [
    { service: 'telephony', event: 'CallStateChanged', perms: ['calls'] },
    { service: 'telephony', event: 'SmsReceived',      perms: ['sms'] },
    { service: 'telephony', event: 'EmergencyAlert',   perms: ['system'] }
  ];

  /* What the user is asked for the first time an app uses it — the camera and
     microphone, where the user is, contacts and messages, calls and SMS. The
     sheet is the system UI's (apps/statusbar), never the app's own page: the app
     could answer it itself there. Allowed or refused, the answer stays —
     app_grants / app_permissions in settings — until the user changes it in
     Settings. The same list as bridge.c's (verify-all compares them) */
  var DANGEROUS_PERMISSIONS = ['camera', 'microphone', 'location', 'contacts', 'messaging', 'calls', 'sms'];

  /* Events about one app's own things reach a third-party app only when they
     are its own: they carry the owner second (data[1]); one that names none
     goes to none of them. event null: every event of the service. The same
     table as OWNED_EVENTS in bridge.c (verify-all compares them). */
  var OWNED_EVENTS = [
    { service: 'notification', event: null,            readAll: 'notification.read' },
    { service: 'location',     event: 'GeofenceEnter', readAll: null },
    { service: 'location',     event: 'GeofenceExit',  readAll: null },
    { service: 'alarm',        event: 'AlarmTriggered', readAll: null },
    { service: 'push',         event: 'PushReceived',   readAll: null },
    { service: 'webcontent',   event: null,             readAll: null }
  ];

  /* App-declared permissions from app.json */
  var _appPermissions = {};

  /* System apps locked — cannot be overwritten by user apps */
  var _systemLocked = {};

  /* User overrides from settings (revoked permissions) */
  var _userOverrides = {};

  /* What the user said yes to (app_grants) — dangerous permissions only count with it */
  var _grants = {};

  /* Permissions only the OS can hold. A third-party app that lists one of these
     in app.json does not get it: declaring "system" is declaring yourself a
     system app, which rule 8 says must not be trusted. Before this filter an
     installed app could write ["system", "shell", "credential"] and receive
     settings write access, command execution and the keychain.
     Dangerous-but-legitimate permissions (camera, location...) stay grantable;
     the user can turn them off per app in Settings (app_permissions). */
  var RESERVED_PERMISSIONS = [
    'system',             /* settings writes, telemetry consent */
    'credential',         /* keychain */
    'app.manage',         /* install / uninstall other apps */
    'notification.read',  /* read every app's notifications */
    'webcontent'          /* embed an OS-owned web content view (the browser) */
  ];

  function grantableFromDeclaration(perms) {
    return (perms || []).filter(function (p) {
      return RESERVED_PERMISSIONS.indexOf(p) === -1;
    });
  }

  /* All known permission strings — system apps get all of these */
  var ALL_PERMISSIONS = [
    'storage', 'files.read', 'files.write', 'files.delete',
    'credential', 'system', 'wifi', 'bluetooth',
    'notification', 'notification.read', 'camera', 'microphone',
    'audio', 'app.manage', 'app.list', 'app.launch',
    'location', 'network', 'downloads', 'bookmarks',
    'input', 'i18n', 'battery', 'time', 'auth', 'wallpaper', 'gallery',
    'contacts', 'messaging', 'telephony', 'calls', 'sms', 'push', 'webcontent'
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
      _appPermissions[appId] = grantableFromDeclaration(permissions);
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
      _appPermissions[app.id] = grantableFromDeclaration(app.permissions);
    }
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

  /* The dangerous permissions the user allowed an app (app_grants) */
  function setAppGrants(appId, granted) {
    if (_systemLocked[appId]) return;
    _grants[appId] = granted || [];
  }

  function isGranted(appId, permission) {
    return !!_systemLocked[appId] || (_grants[appId] || []).indexOf(permission) !== -1;
  }

  /* Of the permissions a rule accepts, is one the app holds usable now: one
     that needs no asking, or a dangerous one the user allowed. ask: the first
     held dangerous one still to ask for (null when there is none) */
  function usable(appId, list) {
    var effective = getEffectivePermissions(appId), ask = null;
    if (list.length === 0) return { ok: true, ask: null };
    for (var i = 0; i < list.length; i++) {
      var p = list[i];
      if (effective.indexOf(p) === -1) continue;
      if (DANGEROUS_PERMISSIONS.indexOf(p) === -1 || isGranted(appId, p)) return { ok: true, ask: null };
      if (!ask) ask = p;
    }
    return { ok: false, ask: ask };
  }

  /* The dangerous permission this call needs the user's yes for, or null. Asked
     after checkPermission passed: the method's rule when it has one, else the
     service's (system apps never ask) */
  function consentNeeded(appId, service, method) {
    if (_systemLocked[appId]) return null;
    var list = METHOD_PERMISSIONS[service + '.' + method] || SERVICE_PERMISSIONS[service] || [];
    var u = usable(appId, list);
    return u.ok ? null : u.ask;
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
    /* A service with no entry is unreviewed: closed, as on the device */
    if (!required) return false;

    /* All apps — including system — go through the same permission path.
       System apps have ALL_PERMISSIONS registered, so they pass naturally. */
    var appPerms = getEffectivePermissions(appId);
    function holdsOne(list) {
      if (list.length === 0) return true;
      for (var i = 0; i < list.length; i++) {
        if (appPerms.indexOf(list[i]) !== -1) return true;
      }
      return false;
    }
    var byMethod = METHOD_PERMISSIONS[service + '.' + method];
    return holdsOne(required) && (!byMethod || holdsOne(byMethod));
  }

  /**
   * May this app hear this service event? It must be allowed to call the
   * service, and an event about one app's own things must be its own.
   */
  function mayReceive(appId, service, event, data) {
    if (!appId || !checkPermission(appId, service, '')) return false;
    /* What the event needs (its own rule, else the service's): held, and if
       dangerous, allowed — an event never asks */
    if (!_systemLocked[appId]) {
      var need = SERVICE_PERMISSIONS[service] || [];
      for (var e = 0; e < EVENT_PERMISSIONS.length; e++) {
        if (EVENT_PERMISSIONS[e].service === service && EVENT_PERMISSIONS[e].event === event) {
          need = EVENT_PERMISSIONS[e].perms;
        }
      }
      if (!usable(appId, need).ok) return false;
    }
    for (var i = 0; i < OWNED_EVENTS.length; i++) {
      var r = OWNED_EVENTS[i];
      if (r.service !== service || (r.event && r.event !== event)) continue;
      if (r.readAll && hasPermission(appId, r.readAll)) return true;
      return Object.prototype.toString.call(data) === '[object Array]' && data[1] === appId;
    }
    return true;
  }

  /**
   * Check if a specific permission is granted for an app.
   */
  function hasPermission(appId, permission) {
    var effective = getEffectivePermissions(appId);
    return effective.indexOf(permission) !== -1;
  }


  /**
   * An app was removed: what it was granted and what the user revoked go with
   * it. A system app is never forgotten.
   */
  function forgetApp(appId) {
    if (_systemLocked[appId]) return;
    delete _appPermissions[appId];
    delete _userOverrides[appId];
    delete _grants[appId];
  }

  return {
    RESERVED_PERMISSIONS: RESERVED_PERMISSIONS,
    checkPermission: checkPermission,
    mayReceive: mayReceive,
    hasPermission: hasPermission,
    registerApp: registerApp,
    registerFromAppList: registerFromAppList,
    forgetApp: forgetApp,
    setAppOverride: setAppOverride,
    setAppGrants: setAppGrants,
    isGranted: isGranted,
    getGrants: function (appId) { return (_grants[appId] || []).slice(); },
    getRevoked: function (appId) { return (_userOverrides[appId] || []).slice(); },
    consentNeeded: consentNeeded,
    DANGEROUS_PERMISSIONS: DANGEROUS_PERMISSIONS,
    getEffectivePermissions: getEffectivePermissions,
    getDeclaredPermissions: getDeclaredPermissions,
    SYSTEM_APPS: SYSTEM_APPS,
    SERVICE_PERMISSIONS: SERVICE_PERMISSIONS
  };
})();
