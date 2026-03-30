// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Service Router
//
// Role: Zyl OS service framework — router, watchdog, init orchestrator
// Scope: Assembles service modules from services/*.js, routes handleRequest(),
//        manages per-app watchdog, native invoker fallback, boot init.
//        Individual service logic lives in services/*.js (28 modules).
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
     Native Invoker — fallback for real device (no Tauri backend).
     =============================================================== */
  function _createNativeInvoker() {
    return function nativeInvoke(cmd, args) {
      args = args || {};
      if (cmd === 'http_fetch') {
        if (typeof fetch === 'function') {
          return fetch(args.url)
            .then(function (r) { return r.text(); })
            .catch(function (e) { return Promise.reject('Fetch failed: ' + e.message); });
        }
        return Promise.reject('fetch API not available');
      }
      if (cmd === 'load_settings') {
        try {
          var stored = localStorage.getItem('zylos_settings');
          return Promise.resolve(stored ? JSON.parse(stored) : null);
        } catch (e) { return Promise.resolve(null); }
      }
      if (cmd === 'save_settings') {
        try {
          var current = JSON.parse(localStorage.getItem('zylos_settings') || '{}');
          if (args.category) {
            if (!current[args.category]) current[args.category] = {};
            if (args.key !== undefined) current[args.category][args.key] = args.value;
          }
          localStorage.setItem('zylos_settings', JSON.stringify(current));
          return Promise.resolve(true);
        } catch (e) { return Promise.resolve(false); }
      }
      if (cmd === 'fs_read_dir') return Promise.resolve([]);
      if (cmd === 'fs_read_file' || cmd === 'fs_read_binary') return Promise.resolve(null);
      if (cmd === 'fs_write_file') return Promise.resolve(false);
      if (cmd === 'get_wifi_networks') return Promise.resolve([]);
      if (cmd === 'get_bluetooth_devices') return Promise.resolve([]);
      if (cmd === 'location_get_last_known') {
        if (navigator.geolocation) {
          return new Promise(function (resolve) {
            navigator.geolocation.getCurrentPosition(function (pos) {
              resolve({
                latitude: pos.coords.latitude,
                longitude: pos.coords.longitude,
                altitude: pos.coords.altitude || 0,
                accuracy: pos.coords.accuracy,
                speed: pos.coords.speed || 0,
                timestamp: Date.now(),
                provider: 'geolocation-api'
              });
            }, function () {
              resolve({ latitude: 0, longitude: 0, provider: 'unavailable' });
            }, { timeout: 5000 });
          });
        }
        return Promise.resolve({ latitude: 0, longitude: 0, provider: 'unavailable' });
      }
      if (cmd === 'get_battery_state' || cmd === 'power_get_state') {
        if (navigator.getBattery) {
          return navigator.getBattery().then(function (b) {
            return { level: Math.round(b.level * 100), charging: b.charging };
          });
        }
        return Promise.resolve({ level: -1, charging: false });
      }
      if (cmd === 'notif_post') {
        if (typeof Notification !== 'undefined' && Notification.permission === 'granted') {
          new Notification(args.title || '', { body: args.body || '' });
        }
        return Promise.resolve({ id: Date.now() });
      }
      if (cmd === 'list_installed_apps') return Promise.resolve([]);
      if (cmd === 'user_get_current') return Promise.resolve({ name: 'User', username: 'user' });
      return Promise.resolve(null);
    };
  }

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
  var _appBlocked = {};
  var WATCHDOG_AUTO_RELEASE_MS = 30000;
  var _watchdogTimers = {};

  function watchdogAcquire(appId) {
    if (!appId) return true;
    if (_appBlocked[appId]) return false;
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

  function watchdogBlock(appId) { if (appId) _appBlocked[appId] = true; }
  function watchdogUnblock(appId) {
    if (!appId) return;
    delete _appBlocked[appId];
    delete _appPending[appId];
  }
  function watchdogStatus() {
    return { pending: JSON.parse(JSON.stringify(_appPending)), blocked: Object.keys(_appBlocked) };
  }

  /* ===============================================================
     Utilities
     =============================================================== */
  function formatBytes(bytes) {
    if (!bytes) return '0 B';
    var units = ['B', 'KB', 'MB', 'GB'];
    var i = Math.floor(Math.log(bytes) / Math.log(1024));
    if (i >= units.length) i = units.length - 1;
    return (bytes / Math.pow(1024, i)).toFixed(1) + ' ' + units[i];
  }

  /* ===============================================================
     Module Assembly + Service Map
     =============================================================== */
  var SYSTEM_APPS = (typeof ZylPermissions !== 'undefined') ? ZylPermissions.SYSTEM_APPS : [];
  var serviceMap = {};
  var _modules = {}; /* references to instantiated modules for cross-deps */

  function assembleModules() {
    var deps = { invoke: _invoke, formatBytes: formatBytes, SYSTEM_APPS: SYSTEM_APPS };

    /* Independent modules */
    if (ns.fs)            serviceMap.fs           = ns.fs(deps);
    if (ns.device) {
      var devMod = ns.device(deps);
      serviceMap.device = devMod;
      _modules.device = devMod;
    }
    if (ns.storage)       serviceMap.storage      = ns.storage(deps);
    if (ns.terminal)      serviceMap.terminal     = ns.terminal(deps);
    if (ns.wifi)          serviceMap.wifi         = ns.wifi(deps);
    if (ns.bluetooth)     serviceMap.bluetooth    = ns.bluetooth(deps);
    if (ns.network)       serviceMap.network      = ns.network(deps);
    if (ns.browser)       serviceMap.browser      = ns.browser(deps);
    if (ns.notification)  serviceMap.notification = ns.notification(deps);
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

    /* Apps (needed by appstore, sandbox) */
    var appsMod = null;
    if (ns.apps) {
      appsMod = ns.apps(deps);
      serviceMap.apps = appsMod;
      _modules.apps = appsMod;
    }

    /* Modules with cross-dependencies */
    var crossDeps = {
      invoke: _invoke, formatBytes: formatBytes, SYSTEM_APPS: SYSTEM_APPS,
      settingsSvc: settingsMod,
      appsSvc: appsMod,
      deviceRef: (_modules.device && _modules.device._getRef) ? _modules.device._getRef() : {},
      logFn: loggerMod ? loggerMod._addEntry : null
    };

    if (ns.telephony)     serviceMap.telephony    = ns.telephony(crossDeps);
    if (ns.contacts)      serviceMap.contacts     = ns.contacts(crossDeps);
    if (ns.messaging)     serviceMap.messaging    = ns.messaging(crossDeps);
    if (ns.appstore)      serviceMap.appstore     = ns.appstore(crossDeps);
    if (ns.updater)       serviceMap.updater      = ns.updater(crossDeps);
    if (ns.sandbox)       serviceMap.sandbox      = ns.sandbox(crossDeps);
    if (ns.accessibility) serviceMap.accessibility = ns.accessibility(crossDeps);
    if (ns.audio) {
      var audioMod = ns.audio(crossDeps);
      serviceMap.audio = audioMod;
      _modules.audio = audioMod;
    }
  }

  /* ===============================================================
     Init
     =============================================================== */
  function init(invoker) {
    _invoke = invoker || _createNativeInvoker();
    assembleModules();

    /* Load persisted settings and audio state on boot */
    if (_modules.settings) {
      _modules.settings._loadFromBackend().then(function () {
        var s = _modules.settings._getState().sound;
        if (s && _modules.audio && _modules.audio._loadFromSettings) {
          _modules.audio._loadFromSettings(s);
        }
      });
      /* Load permission overrides from settings */
      _modules.settings._loadFromBackend().then(function () {
        var perms = _modules.settings._getState().app_permissions;
        if (perms && typeof ZylPermissions !== 'undefined') {
          Object.keys(perms).forEach(function (appId) {
            var revoked = String(perms[appId]).split(',').filter(Boolean);
            ZylPermissions.setAppOverride(appId, revoked);
          });
        }
      });
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
        error: _appBlocked[appId] ? 'App blocked by watchdog' : 'Too many concurrent requests',
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

    var result = fn(params || {});

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
  return {
    init: init,
    handleRequest: handleRequest,
    device: null, /* set after init */
    storage: null,
    fs: null,
    apps: null,
    settings: null,
    applyDisplayProfile: applyDisplayProfile,
    watchdog: {
      block: watchdogBlock,
      unblock: watchdogUnblock,
      status: watchdogStatus
    },
    /* Lazy accessors for backward compat */
    _getModule: function (name) { return _modules[name] || serviceMap[name]; }
  };
})();
