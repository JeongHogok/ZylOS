// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Service Router
//
// Role: Zyl OS service framework — router, watchdog, init orchestrator
// Scope: Assembles service modules from services/*.js, routes handleRequest(),
//        limits each app's concurrent requests (watchdog), boot init with the
//        container's invoker. Individual service logic lives in services/*.js.
// Dependency Direction: Presentation -> Domain (service modules via ZylServiceModules)
// SOLID: SRP — routing + orchestration only, OCP — new services added without modifying router
//        DIP — depends on module factory interface, not concrete implementations
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylSystemServices = (function () {
  'use strict';

  var _invoke = function () { return Promise.resolve(null); };
  var ns = window.ZylServiceModules || {};

  /* ===============================================================
     Service Timeout
     =============================================================== */
  var SERVICE_TIMEOUT_MS = 15000;
  var NETWORK_TIMEOUT_MS = 12000;

  function withTimeout(promise, ms, label) {
    if (!promise || typeof promise.then !== 'function') return promise;
    var timeout = ms || SERVICE_TIMEOUT_MS;
    return new Promise(function (resolve, reject) {
      var settled = false;
      var timer = setTimeout(function () {
        if (!settled) {
          settled = true;
          reject({ error: 'Service timeout', service: label || 'unknown', timeoutMs: timeout });
        }
      }, timeout);
      promise.then(function (v) {
        if (!settled) { settled = true; clearTimeout(timer); resolve(v); }
      }, function (e) {
        if (!settled) { settled = true; clearTimeout(timer); reject(e); }
      });
    });
  }

  /* ===============================================================
     App Watchdog
     =============================================================== */
  var MAX_CONCURRENT_PER_APP = 8;
  var _appPending = {};
  var WATCHDOG_AUTO_RELEASE_MS = 30000;
  var _watchdogTimers = {};

  function watchdogAcquire(appId) {
    if (!appId) return true;
    if (!_appPending[appId]) _appPending[appId] = 0;
    if (_appPending[appId] >= MAX_CONCURRENT_PER_APP) return false;
    _appPending[appId]++;
    if (!_watchdogTimers[appId]) _watchdogTimers[appId] = [];
    var timerId = setTimeout(function () {
      watchdogRelease(appId);
      if (_watchdogTimers[appId]) {
        _watchdogTimers[appId] = _watchdogTimers[appId].filter(function (t) { return t !== timerId; });
      }
    }, WATCHDOG_AUTO_RELEASE_MS);
    _watchdogTimers[appId].push(timerId);
    return true;
  }

  function watchdogRelease(appId) {
    if (!appId || !_appPending[appId]) return;
    _appPending[appId]--;
    if (_appPending[appId] <= 0) {
      delete _appPending[appId];
      if (_watchdogTimers[appId]) {
        _watchdogTimers[appId].forEach(function (t) { clearTimeout(t); });
        delete _watchdogTimers[appId];
      }
    } else if (_watchdogTimers[appId] && _watchdogTimers[appId].length > 0) {
      var tid = _watchdogTimers[appId].pop();
      if (tid) clearTimeout(tid);
    }
  }


  /* ===============================================================
     Module Assembly + Service Map
     =============================================================== */
  var SYSTEM_APPS = (typeof ZylPermissions !== 'undefined') ? ZylPermissions.SYSTEM_APPS : [];
  var serviceMap = {};
  var _modules = {}; /* references to instantiated modules for cross-deps */

  function assembleModules() {
    var deps = { invoke: _invoke, SYSTEM_APPS: SYSTEM_APPS };

    /* Independent modules */
    if (ns.fs)            serviceMap.fs           = ns.fs(deps);
    if (ns.clipboard)     serviceMap.clipboard    = ns.clipboard(deps);
    if (ns.device) {
      var devMod = ns.device(deps);
      serviceMap.device = devMod;
      _modules.device = devMod;
    }
    if (ns.storage)       serviceMap.storage      = ns.storage(deps);
    if (ns.wifi)          serviceMap.wifi         = ns.wifi(deps);
    if (ns.bluetooth)     serviceMap.bluetooth    = ns.bluetooth(deps);
    if (ns.network)       serviceMap.network      = ns.network(deps);
    if (ns.appdata)       serviceMap.appdata      = ns.appdata(deps);
    if (ns.notification) {
      var notifMod = ns.notification(deps);
      serviceMap.notification = notifMod;
      _modules.notification = notifMod;
    }
    if (ns.power)         serviceMap.power        = ns.power(deps);
    if (ns.display) {
      var dispMod = ns.display(deps);
      serviceMap.display = dispMod;
      _modules.display = dispMod;
    }
    if (ns.input)         serviceMap.input        = ns.input(deps);
    if (ns.sensors)       serviceMap.sensors      = ns.sensors(deps);
    if (ns.location)      serviceMap.location     = ns.location(deps);
    if (ns.usb)           serviceMap.usb          = ns.usb(deps);
    if (ns.user)          serviceMap.user         = ns.user(deps);
    if (ns.credential)    serviceMap.credential   = ns.credential(deps);

    /* Settings (needed by other modules) */
    var settingsMod = null;
    if (ns.settings) {
      settingsMod = ns.settings(deps);
      serviceMap.settings = settingsMod;
      _modules.settings = settingsMod;
    }

    /* Logger */
    var loggerMod = null;
    if (ns.logger) {
      loggerMod = ns.logger(deps);
      serviceMap.logger = loggerMod;
      _modules.logger = loggerMod;
    }

    /* App seals: whose code may run (apps, appstore) — knows this device's ID */
    var sealMod = ns.appSeal ? ns.appSeal({
      invoke: _invoke,
      deviceRef: (_modules.device && _modules.device._getRef) ? _modules.device._getRef() : {}
    }) : null;

    /* Apps (needed by appstore, sandbox) */
    var appsMod = null;
    if (ns.apps) {
      appsMod = ns.apps({ invoke: _invoke, SYSTEM_APPS: SYSTEM_APPS, seal: sealMod });
      serviceMap.apps = appsMod;
      _modules.apps = appsMod;
    }

    /* An app was removed (the app store): every part of the OS drops what it kept
       for it — each module its own (_forgetApp), the permission tables, the
       user's revocations in settings — and the compositor closes it */
    function forgetApp(appId) {
      Object.keys(serviceMap).forEach(function (name) {
        var m = serviceMap[name];
        if (m && typeof m._forgetApp === 'function') m._forgetApp(appId);
      });
      if (typeof ZylPermissions !== 'undefined' && ZylPermissions.forgetApp) ZylPermissions.forgetApp(appId);
      if (typeof ZylPermissionDialog !== 'undefined' && ZylPermissionDialog.forgetApp) ZylPermissionDialog.forgetApp(appId);
      if (settingsMod) settingsMod._removeSetting('app_permissions', appId);
      if (appsMod) appsMod._invalidate();
      window.dispatchEvent(new CustomEvent('zyl:app-removed', { detail: appId }));
    }

    /* Modules with cross-dependencies */
    var crossDeps = {
      forgetApp: forgetApp,
      invoke: _invoke, SYSTEM_APPS: SYSTEM_APPS,
      settingsSvc: settingsMod,
      appsSvc: appsMod,
      seal: sealMod,
      deviceRef: (_modules.device && _modules.device._getRef) ? _modules.device._getRef() : {},
      logFn: loggerMod ? loggerMod._addEntry : null,
      notificationSvc: _modules.notification
    };

    if (ns.telephony)     serviceMap.telephony    = ns.telephony(crossDeps);
    if (ns.contacts)      serviceMap.contacts     = ns.contacts(crossDeps);
    if (ns.messaging)     serviceMap.messaging    = ns.messaging(crossDeps);
    if (ns.appstore)      serviceMap.appstore     = ns.appstore(crossDeps);
    if (ns.updater)       serviceMap.updater      = ns.updater(crossDeps);
    if (ns.sandbox)       serviceMap.sandbox      = ns.sandbox(crossDeps);
    if (ns.accessibility) serviceMap.accessibility = ns.accessibility(crossDeps);
    if (ns.telemetry)     serviceMap.telemetry    = ns.telemetry(crossDeps);
    if (ns.netstats)      serviceMap.netstats     = ns.netstats(crossDeps);
    if (ns.bootctl)       serviceMap.bootctl      = ns.bootctl(crossDeps);
    if (ns.lock)          serviceMap.lock         = ns.lock(crossDeps);
    if (ns.alarm)         serviceMap.alarm        = ns.alarm(crossDeps);
    if (ns.audio) {
      var audioMod = ns.audio(crossDeps);
      serviceMap.audio = audioMod;
      _modules.audio = audioMod;
    }
  }

  /* ===============================================================
     Init
     =============================================================== */
  var _publicRef = null; /* Reference to the returned public API object */

  function init(invoker) {
    _invoke = invoker;
    assembleModules();

    /* A5: Bind module references to public API after assembly */
    if (_publicRef) {
      _publicRef.storage = _modules.storage || serviceMap.storage || null;
    }

    /* Load persisted settings and audio state on boot */
    if (_modules.settings) {
      _modules.settings._loadFromBackend().then(function () {
        var s = _modules.settings._getState().sound;
        if (s && _modules.audio && _modules.audio._loadFromSettings) {
          _modules.audio._loadFromSettings(s);
        }
      }).catch(function () { /* audio settings load failed — use defaults */ });
      /* Load permission overrides from settings */
      _modules.settings._loadFromBackend().then(function () {
        var perms = _modules.settings._getState().app_permissions;
        if (perms && typeof ZylPermissions !== 'undefined') {
          Object.keys(perms).forEach(function (appId) {
            var revoked = String(perms[appId]).split(',').filter(Boolean);
            ZylPermissions.setAppOverride(appId, revoked);
          });
        }
      }).catch(function () { /* permission overrides load failed — use defaults */ });
    }

    /* Pre-load app list */
    if (serviceMap.apps && serviceMap.apps.getInstalled) {
      serviceMap.apps.getInstalled();
    }
  }

  /* ===============================================================
     Request Handler
     =============================================================== */
  var NETWORK_SERVICES = { network: true };

  function handleRequest(service, method, params, appId) {
    if (appId && !watchdogAcquire(appId)) {
      return Promise.resolve({
        error: 'Too many concurrent requests',
        service: service, method: method
      });
    }

    if (typeof ZylPermissions !== 'undefined' && appId) {
      if (!ZylPermissions.checkPermission(appId, service, method)) {
        watchdogRelease(appId);
        return Promise.resolve({ error: 'Permission denied', service: service, method: method });
      }
    }

    if (service === 'fs' && typeof ZylSecurity !== 'undefined') {
      var path = (params && (params.path || params.oldPath)) || '';
      if (ZylSecurity.isProtectedPath(path)) {
        watchdogRelease(appId);
        return Promise.resolve({ error: 'Access denied: protected system file' });
      }
    }

    var svc = serviceMap[service];
    if (!svc) { watchdogRelease(appId); return null; }
    var fn = svc[method];
    if (!fn) { watchdogRelease(appId); return null; }

    /* The caller is the OS's to say, never the request's: whose data it is */
    var result = fn(params || {}, appId || '');

    if (result && typeof result.then === 'function') {
      var timeoutMs = NETWORK_SERVICES[service] ? NETWORK_TIMEOUT_MS : SERVICE_TIMEOUT_MS;
      var label = service + '.' + method;
      return withTimeout(result, timeoutMs, label).then(function (v) {
        watchdogRelease(appId);
        return v;
      }, function (e) {
        watchdogRelease(appId);
        if (e && e.error === 'Service timeout') {
          return { error: e.error, service: e.service, timeoutMs: e.timeoutMs };
        }
        return { error: typeof e === 'string' ? e : (e && e.message) || 'Service error' };
      });
    }

    watchdogRelease(appId);
    return result;
  }

  /* ===============================================================
     Display Profile Initialization
     =============================================================== */
  function applyDisplayProfile(profile) {
    if (_modules.display && _modules.display._applyProfile) {
      _modules.display._applyProfile(profile);
    }
    if (_modules.device && _modules.device._applyProfile) {
      _modules.device._applyProfile(profile);
    }
  }

  /* ===============================================================
     Public API
     =============================================================== */
  _publicRef = {
    init: init,
    handleRequest: handleRequest,
    storage: null, /* populated by init() after assembleModules() */
    applyDisplayProfile: applyDisplayProfile
  };
  return _publicRef;
})();
