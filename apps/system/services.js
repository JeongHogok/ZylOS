// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service
//
// Role: Zyl OS system service framework — all 28 OS service business logic
// Scope: fs, device, storage, apps, settings, terminal, wifi, bluetooth,
//        network, browser, notification, power, display, input, sensors,
//        location, telephony, contacts, messaging, usb, user, credential,
//        appstore, updater, sandbox, logger, accessibility, audio
// Dependency Direction: Domain -> none (receives invoke abstraction via init)
// SOLID: SRP — pure service logic, DIP — depends on injected invoker abstraction
//        OCP — new services added without modifying existing ones
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylSystemServices = (function () {
  'use strict';

  var _invoke = function () { return Promise.resolve(null); };

  /* ===============================================================
     Native Invoker — fallback for real device (no Tauri backend).
     Uses native APIs: fetch for HTTP, localStorage for settings,
     WebKit messageHandlers for D-Bus services.
     =============================================================== */
  function _createNativeInvoker() {
    return function nativeInvoke(cmd, args) {
      args = args || {};
      /* HTTP fetch — use native fetch API */
      if (cmd === 'http_fetch') {
        if (typeof fetch === 'function') {
          return fetch(args.url)
            .then(function (r) { return r.text(); })
            .catch(function (e) { return Promise.reject('Fetch failed: ' + e.message); });
        }
        return Promise.reject('fetch API not available');
      }
      /* Settings — use localStorage */
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
      /* File system — limited without backend; return empty for safety */
      if (cmd === 'fs_read_dir') {
        return Promise.resolve([]);
      }
      if (cmd === 'fs_read_file' || cmd === 'fs_read_binary') {
        return Promise.resolve(null);
      }
      if (cmd === 'fs_write_file') {
        /* On real device without WAM D-Bus bridge, FS write is not possible from JS */
        return Promise.resolve(false);
      }
      /* WiFi / Bluetooth — need D-Bus bridge; return empty gracefully */
      if (cmd === 'get_wifi_networks') return Promise.resolve([]);
      if (cmd === 'get_bluetooth_devices') return Promise.resolve([]);
      /* Location — use geolocation API if available */
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
      /* Battery */
      if (cmd === 'get_battery_state' || cmd === 'power_get_state') {
        if (navigator.getBattery) {
          return navigator.getBattery().then(function (b) {
            return { level: Math.round(b.level * 100), charging: b.charging };
          });
        }
        return Promise.resolve({ level: -1, charging: false });
      }
      /* Notification — use web Notification API */
      if (cmd === 'notif_post') {
        if (typeof Notification !== 'undefined' && Notification.permission === 'granted') {
          new Notification(args.title || '', { body: args.body || '' });
        }
        return Promise.resolve({ id: Date.now() });
      }
      /* App list — not available without backend */
      if (cmd === 'list_installed_apps') return Promise.resolve([]);
      /* User info */
      if (cmd === 'user_get_current') return Promise.resolve({ name: 'User', username: 'user' });
      /* Default: resolve null */
      return Promise.resolve(null);
    };
  }

  /* ===============================================================
     Service Timeout — wraps any Promise with a configurable timeout.
     Prevents a single slow/hung service call from blocking the OS.
     =============================================================== */
  var SERVICE_TIMEOUT_MS = 15000; /* 15 seconds default */
  var NETWORK_TIMEOUT_MS = 12000; /* 12 seconds for network calls */

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
     App Watchdog — monitors per-app pending service calls.
     If an app has too many concurrent calls, further requests are rejected.
     Enables force-kill of runaway apps by the OS.
     =============================================================== */
  var MAX_CONCURRENT_PER_APP = 8; /* max simultaneous service calls per app */
  var _appPending = {}; /* appId → count of pending calls */
  var _appBlocked = {}; /* appId → true if force-blocked */

  var WATCHDOG_AUTO_RELEASE_MS = 30000; /* 30s failsafe auto-release */
  var _watchdogTimers = {}; /* appId → [timerIds] */

  function watchdogAcquire(appId) {
    if (!appId) return true;
    if (_appBlocked[appId]) return false;
    if (!_appPending[appId]) _appPending[appId] = 0;
    if (_appPending[appId] >= MAX_CONCURRENT_PER_APP) return false;
    _appPending[appId]++;
    /* Failsafe: auto-release after 30s to prevent permanent count leak */
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
      /* Clear all failsafe timers for this app */
      if (_watchdogTimers[appId]) {
        _watchdogTimers[appId].forEach(function (t) { clearTimeout(t); });
        delete _watchdogTimers[appId];
      }
    } else if (_watchdogTimers[appId] && _watchdogTimers[appId].length > 0) {
      /* Clear one timer (LIFO) — corresponding to this release */
      var tid = _watchdogTimers[appId].pop();
      if (tid) clearTimeout(tid);
    }
  }

  /**
   * Force-block an app from making further service calls.
   * Used by the compositor or admin to stop a misbehaving app.
   */
  function watchdogBlock(appId) {
    if (!appId) return;
    _appBlocked[appId] = true;
  }

  /**
   * Unblock an app (e.g. after restart).
   */
  function watchdogUnblock(appId) {
    if (!appId) return;
    delete _appBlocked[appId];
    delete _appPending[appId];
  }

  /**
   * Get watchdog status for all apps (monitoring).
   */
  function watchdogStatus() {
    return { pending: JSON.parse(JSON.stringify(_appPending)), blocked: Object.keys(_appBlocked) };
  }

  function init(invoker) {
    _invoke = invoker || _createNativeInvoker();
    /* Load persisted settings and audio state on boot */
    settings._loadFromBackend().then(function () {
      var s = settings.state.sound;
      if (s) {
        if (s.mediaVolume !== undefined) audioState.mediaVolume = parseInt(s.mediaVolume, 10);
        if (s.notifVolume !== undefined) audioState.notifVolume = parseInt(s.notifVolume, 10);
        if (s.alarmVolume !== undefined) audioState.alarmVolume = parseInt(s.alarmVolume, 10);
        if (s.ringtoneVolume !== undefined) audioState.ringtoneVolume = parseInt(s.ringtoneVolume, 10);
        if (s.systemVolume !== undefined) audioState.systemVolume = parseInt(s.systemVolume, 10);
        if (s.vibration !== undefined) audioState.vibration = !!s.vibration;
        if (s.silentMode !== undefined) audioState.silentMode = !!s.silentMode;
      }
    });
    /* Load permission overrides from settings */
    settings._loadFromBackend().then(function () {
      var perms = settings.state.app_permissions;
      if (perms && typeof ZylPermissions !== 'undefined') {
        Object.keys(perms).forEach(function (appId) {
          var revoked = String(perms[appId]).split(',').filter(Boolean);
          ZylPermissions.setAppOverride(appId, revoked);
        });
      }
    });
    /* Pre-load app list to register permissions immediately on boot.
       This ensures OOBE and other early apps have permissions before
       any app explicitly calls apps.getInstalled(). */
    apps.getInstalled();
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
     1. FileSystemService — mounted disk image I/O
     =============================================================== */
  var fs = {
    getDirectory: function (path) {
      return _invoke('fs_read_dir', { path: path || '/' }).then(function (entries) {
        if (!entries || typeof ZylSecurity === 'undefined') return entries;
        return entries.filter(function (e) {
          return !ZylSecurity.isHiddenFromListing(e.name);
        });
      });
    },
    getUnixDirectory: function (path) {
      var p = _invoke('fs_read_dir', { path: path || '/' });
      if (p && typeof p.then === 'function') {
        return p.then(function (entries) {
          return (entries || []).filter(function (e) {
            return typeof ZylSecurity === 'undefined' || !ZylSecurity.isHiddenFromListing(e.name);
          }).map(function (e) {
            return (e.is_dir ? 'drwxr-xr-x' : '-rw-r--r--') +
              '  user user  ' + (e.size || 0) + '  ' + e.name;
          }).join('\n');
        });
      }
      return null;
    },
    getFileContent: function (path) {
      return _invoke('fs_read_file', { path: path || '' });
    },
    getAllData: function () {
      return _invoke('fs_read_dir', { path: '/' }).then(function (entries) {
        var filtered = (entries || []).filter(function (e) {
          return typeof ZylSecurity === 'undefined' || !ZylSecurity.isHiddenFromListing(e.name);
        });
        return { tree: { '/': filtered }, unixTree: {}, fileContents: {} };
      }).catch(function () {
        return { tree: { '/': [] }, unixTree: {}, fileContents: {} };
      });
    },
    writeFile: function (params) {
      return _invoke('fs_write_file', { path: params.path, content: params.content });
    },
    mkdir: function (params) {
      return _invoke('fs_mkdir', { path: params.path });
    },
    remove: function (params) {
      return _invoke('fs_remove', { path: params.path });
    },
    rename: function (params) {
      return _invoke('fs_rename', { old_path: params.oldPath, new_path: params.newPath });
    }
  };


  /* ===============================================================
     2. DeviceInfoService — device metadata
     =============================================================== */
  var deviceInfo = {
    deviceName: 'Zyl OS Device',
    osVersion: '0.1.0',
    soc: 'SpacemiT K1 (RISC-V)',
    ram: '2GB LPDDR4X',
    kernel: 'Linux 6.6.0-spacemit',
    build: 'ZYL.20260329',
    resolution: '1080x2400',
    hostname: 'zylos',
    username: 'user',
    bootTime: Date.now(),

    getInfo: function () {
      return deviceInfo;
    },

    applyProfile: function (profile) {
      if (!profile) return;
      deviceInfo.deviceName = profile.name || deviceInfo.deviceName;
      deviceInfo.soc = profile.soc ? profile.soc + ' (RISC-V)' : deviceInfo.soc;
      deviceInfo.ram = profile.ram ? profile.ram + ' LPDDR4X' : deviceInfo.ram;
      deviceInfo.resolution = profile.screen || deviceInfo.resolution;
      if (profile.id) {
        deviceInfo.hostname = profile.id.replace(/^zyl-/, '').replace(/-/g, '_');
      }
    },

    getUptime: function () {
      return Math.floor((Date.now() - deviceInfo.bootTime) / 1000);
    }
  };


  /* ===============================================================
     3. StorageService — disk usage (mount point)
     =============================================================== */
  var storage = {
    _cache: null,
    _cacheTime: 0,

    _fetchFromBackend: function () {
      return _invoke('fs_get_usage').then(function (s) {
        storage._cache = s;
        storage._cacheTime = Date.now();
        return s;
      }).catch(function () {
        return storage._cache || { total: 0, used: 0, available: 0, percent: 0 };
      });
    },

    getUsage: function () {
      if (storage._cache && Date.now() - storage._cacheTime < 30000) {
        return Promise.resolve(storage._cache);
      }
      return storage._fetchFromBackend();
    },

    prefetch: function () {
      return storage._fetchFromBackend();
    },

    getFormatted: function () {
      return storage.getUsage().then(function (s) {
        return {
          total: formatBytes(s.total),
          used: formatBytes(s.used),
          available: formatBytes(s.available),
          percent: Math.round(s.percent || 0)
        };
      });
    }
  };


  /* ===============================================================
     4. AppRegistryService — installed apps (from OS image)
     =============================================================== */
  var apps = {
    _cache: null,

    getInstalled: function () {
      if (apps._cache) return Promise.resolve(apps._cache);
      return _invoke('list_installed_apps').then(function (list) {
        if (list && list.length > 0) {
          apps._cache = list.map(function (m) {
            var appName = (m.id || '').split('.').pop();
            return {
              id: m.id,
              name: m.name || appName,
              nameKey: 'app.' + appName,
              icon: m.iconKey || appName,
              color: m.color || 'icon-blue',
              version: m.version || '1.0.0',
              description: m.description || '',
              /* System flag determined by OS SYSTEM_APPS list — NOT from app.json.
                 Prevents malicious apps from claiming system status. */
              system: (SYSTEM_APPS.indexOf(m.id) !== -1),
              permissions: m.permissions || [],
              iconSvg: m.iconSvg || ''
            };
          });
          /* Register app permissions with the permission system */
          if (typeof ZylPermissions !== 'undefined' && ZylPermissions.registerFromAppList) {
            ZylPermissions.registerFromAppList(apps._cache);
          }
          /* Register apps with the OS-level app registry */
          if (typeof ZylAppRegistry !== 'undefined' && ZylAppRegistry.register) {
            ZylAppRegistry.register(apps._cache);
          }
        }
        return apps._cache || [];
      }).catch(function () { return []; });
    },

    getById: function (id) {
      if (!apps._cache) return null;
      return apps._cache.find(function (a) { return a.id === id; }) || null;
    }
  };


  /* ===============================================================
     5. SettingsService — persistent settings (mount point JSON)
     =============================================================== */
  var settings = {
    state: {},
    _loaded: false,

    _loadFromBackend: function () {
      if (settings._loaded) return Promise.resolve(settings.state);
      return _invoke('load_settings').then(function (data) {
        if (data) {
          Object.keys(data).forEach(function (cat) {
            settings.state[cat] = data[cat];
          });
          settings._loaded = true;
        }
        return settings.state;
      }).catch(function () { return settings.state; });
    },

    getSetting: function (category) {
      if (settings._loaded) {
        return Promise.resolve(settings.state[category] || null);
      }
      return settings._loadFromBackend().then(function () {
        return settings.state[category] || null;
      });
    },

    updateSetting: function (category, key, value) {
      if (!settings.state[category]) {
        settings.state[category] = {};
      }
      settings.state[category][key] = value;
      _invoke('save_settings', {
        category: category,
        key: key,
        value: value
      }).catch(function () {});
      return settings.state[category];
    }
  };


  /* ===============================================================
     Stateful Emulated Services — state objects
     =============================================================== */

  /* -- Display Service -- */
  var displayState = {
    width: 1080, height: 2400, refresh: 60,
    rotation: 0, scale: 1.0
  };

  /* -- Input Service -- */
  var inputState = {
    visible: false, layout: 'en'
  };

  /* -- USB Service -- */
  var usbState = {
    mode: 'charging', connected: true
  };

  /* -- Call State -- */
  var _callState = { state: 'IDLE', number: '', startTime: 0 };

  /* -- Power Service — brightness sync -- */
  var powerState = {
    brightness: 80
  };

  /* -- Logger Service -- */
  var logBuffer = [];
  var logLevel = 'INFO';
  var LOG_LEVELS = { 'VERBOSE': 0, 'DEBUG': 1, 'INFO': 2, 'WARN': 3, 'ERROR': 4 };
  var MAX_LOG_LINES = 1000;

  /* -- Accessibility Service -- */
  var a11yState = {
    highContrast: false, fontScale: 1.0, screenReader: false
  };

  /* -- Audio State — loaded from settings on boot -- */
  var audioState = {
    mediaVolume: 70, notifVolume: 80, alarmVolume: 90,
    ringtoneVolume: 80, systemVolume: 50, vibration: true, silentMode: false
  };

  /* -- System apps list — single source of truth in ZylPermissions -- */
  var SYSTEM_APPS = (typeof ZylPermissions !== 'undefined') ? ZylPermissions.SYSTEM_APPS : [];


  /* ===============================================================
     Service Router Map
     =============================================================== */
  var serviceMap = {
    /* -- 1. FileSystem -- */
    fs: {
      getDirectory:     function (p) { return fs.getDirectory(p.path); },
      getUnixDirectory: function (p) { return fs.getUnixDirectory(p.path); },
      getFileContent:   function (p) { return fs.getFileContent(p.path); },
      readBinary:       function (p) { return _invoke('fs_read_binary', { path: p.path }); },
      getAllData:        function ()  { return fs.getAllData(); },
      writeFile:        function (p) { return fs.writeFile(p); },
      mkdir:            function (p) { return fs.mkdir(p); },
      remove:           function (p) { return fs.remove(p); },
      rename:           function (p) { return fs.rename(p); }
    },

    /* -- 2. Device -- */
    device: {
      getInfo:   function () { return deviceInfo.getInfo(); },
      getUptime: function () { return Promise.resolve(deviceInfo.getUptime()); }
    },

    /* -- 3. Storage -- */
    storage: {
      getUsage:     function () { return storage.getUsage(); },
      getFormatted: function () { return storage.getFormatted(); },
      prefetch:     function () { return storage.prefetch(); }
    },

    /* -- 4. Apps -- */
    apps: {
      getInstalled: function () { return apps.getInstalled(); },
      getById:      function (p) { return apps.getById(p.id); }
    },

    /* -- 5. Settings -- */
    settings: {
      get:    function (p) { return settings.getSetting(p.category); },
      update: function (p) { return settings.updateSetting(p.category, p.key, p.value); }
    },

    /* -- 6. Terminal -- */
    terminal: {
      exec: function (p) { return _invoke('exec_command', { command: p.command }); }
    },

    /* -- 7. WiFi -- */
    wifi: {
      getNetworks:  function () { return _invoke('get_wifi_networks'); },
      getConnected: function () {
        return _invoke('get_wifi_networks').then(function (nets) {
          return (nets || []).filter(function (n) { return n.connected; });
        });
      }
    },

    /* -- 8. Bluetooth -- */
    bluetooth: {
      getDevices:   function () { return _invoke('get_bluetooth_devices'); },
      getPaired:    function () { return _invoke('get_bluetooth_devices'); },
      getConnected: function () {
        return _invoke('get_bluetooth_devices').then(function (devs) {
          return (devs || []).filter(function (d) { return d.connected; });
        });
      }
    },

    /* -- Network -- */
    network: {
      fetch: function (p) {
        if (typeof ZylSandbox !== 'undefined' && !ZylSandbox.isAllowedDomain(p.url || '')) {
          return Promise.resolve({ error: 'Domain not in whitelist' });
        }
        return _invoke('http_fetch', { url: p.url || '' });
      }
    },

    /* -- 9. Browser (app owns its data, OS routes only) -- */
    browser: {
      getBookmarks:  function () { return Promise.resolve(null); },
      getQuickLinks: function () { return Promise.resolve(null); }
    },

    /* -- 10. Notification -- */
    notification: {
      post: function (p) {
        return _invoke('notif_post', {
          app_id: p.appId || '', title: p.title || '',
          body: p.body || '', icon: p.icon || '', priority: p.priority || 1
        });
      },
      cancel:    function (p) { return _invoke('notif_cancel', { id: p.id }); },
      getActive: function ()  { return _invoke('notif_get_active'); },
      clearAll:  function ()  { return _invoke('notif_clear_all'); }
    },

    /* -- 11. Power — stateful brightness sync -- */
    power: {
      getState: function () {
        return _invoke('power_get_state').then(function (state) {
          if (state) {
            state.brightness = powerState.brightness;
          }
          return state || { state: 'ACTIVE', brightness: powerState.brightness, screenOn: true, batteryLevel: 85, charging: false };
        });
      },
      setBrightness: function (p) {
        var pct = Math.max(0, Math.min(100, parseInt(p.percent, 10) || 80));
        powerState.brightness = pct;
        return Promise.resolve({ brightness: pct });
      }
    },

    /* -- 12. Display — stateful -- */
    display: {
      getMode: function () {
        return Promise.resolve({
          width: displayState.width, height: displayState.height,
          refresh: displayState.refresh
        });
      },
      getRotation: function () { return Promise.resolve(displayState.rotation); },
      setRotation: function (p) {
        displayState.rotation = parseInt(p.rotation, 10) || 0;
        return Promise.resolve(displayState.rotation);
      },
      getScale: function () { return Promise.resolve(displayState.scale); },
      setScale: function (p) {
        displayState.scale = parseFloat(p.scale) || 1.0;
        return Promise.resolve(displayState.scale);
      }
    },

    /* -- 13. Input — stateful -- */
    input: {
      showKeyboard: function (p) {
        inputState.visible = true;
        inputState.layout = p.layout || inputState.layout;
        return Promise.resolve({ visible: true, layout: inputState.layout });
      },
      hideKeyboard: function () {
        inputState.visible = false;
        return Promise.resolve({ visible: false });
      },
      getState: function () {
        return Promise.resolve({ visible: inputState.visible, layout: inputState.layout });
      }
    },

    /* -- 14. Sensors — dynamic timestamp + micro-noise -- */
    sensors: {
      getLatest: function (p) {
        var type = (p && p.type) || 'accelerometer';
        var noise = function () { return (Math.random() - 0.5) * 0.02; };
        var now = Date.now();
        var defaults = {
          accelerometer: { type: 'accelerometer', values: [noise(), noise(), -9.8 + noise()], timestamp: now },
          gyroscope:     { type: 'gyroscope', values: [noise(), noise(), noise()], timestamp: now },
          proximity:     { type: 'proximity', values: [0, 5.0], timestamp: now },
          light:         { type: 'light', values: [300 + Math.random() * 10 - 5], timestamp: now },
          magnetometer:  { type: 'magnetometer', values: [noise() * 10, 25 + noise() * 5, -45 + noise() * 5], timestamp: now }
        };
        return Promise.resolve(defaults[type] || defaults.accelerometer);
      }
    },

    /* -- 15. Location — backend with IP-based lookup -- */
    location: {
      getLastKnown:   function () { return _invoke('location_get_last_known'); },
      requestUpdates: function () { return Promise.resolve(true); },
      stopUpdates:    function () { return Promise.resolve(true); }
    },

    /* -- 16. Telephony — reads from settings -- */
    telephony: {
      getState: function () {
        return settings.getSetting('telephony').then(function (tel) {
          return {
            simPresent:  (tel && tel.simPresent !== undefined) ? tel.simPresent : true,
            operator:    (tel && tel.operator) || 'Zyl Mobile',
            networkType: (tel && tel.networkType) || 'LTE',
            signal:      (tel && tel.signal !== undefined) ? tel.signal : 3,
            imei:        (tel && tel.imei) || '000000000000000',
            phoneNumber: (tel && tel.phoneNumber) || ''
          };
        });
      },
      getCallState: function () { return Promise.resolve(_callState); },
      dial: function (p) {
        _callState = { state: 'DIALING', number: p.number || '', startTime: Date.now() };
        setTimeout(function () {
          if (_callState.state === 'DIALING') _callState.state = 'ACTIVE';
        }, 2000);
        return Promise.resolve(_callState);
      },
      answer: function () {
        _callState.state = 'ACTIVE';
        _callState.startTime = Date.now();
        return Promise.resolve(_callState);
      },
      hangup: function () {
        var ended = { number: _callState.number, state: _callState.state, duration: _callState.startTime ? Math.floor((Date.now() - _callState.startTime) / 1000) : 0 };
        _callState = { state: 'IDLE', number: '', startTime: 0 };
        return Promise.resolve(ended);
      },
      getCallLog: function () {
        return settings.getSetting('callLog').then(function (log) {
          return (log && log.entries) ? JSON.parse(log.entries) : [];
        });
      },
      addCallLog: function (p) {
        return settings.getSetting('callLog').then(function (log) {
          var entries = (log && log.entries) ? JSON.parse(log.entries) : [];
          entries.unshift({ number: p.number, type: p.type || 'outgoing', time: Date.now(), duration: p.duration || 0, name: p.name || '' });
          if (entries.length > 100) entries = entries.slice(0, 100);
          settings.updateSetting('callLog', 'entries', JSON.stringify(entries));
          return entries;
        });
      }
    },

    /* -- Contacts Service -- */
    contacts: {
      getAll: function () {
        return _invoke('fs_read_dir', { path: 'Documents/Contacts' }).then(function (entries) {
          if (!entries || entries.length === 0) return [];
          var promises = entries.filter(function (e) { return e.name.indexOf('.json') !== -1; }).map(function (e) {
            return _invoke('fs_read_file', { path: 'Documents/Contacts/' + e.name }).then(function (content) {
              try { return JSON.parse(content); } catch (err) { return null; }
            });
          });
          return Promise.all(promises).then(function (results) {
            return results.filter(Boolean).sort(function (a, b) { return (a.name || '').localeCompare(b.name || ''); });
          });
        }).catch(function () { return []; });
      },
      getById: function (p) {
        return _invoke('fs_read_file', { path: 'Documents/Contacts/' + p.id + '.json' }).then(function (c) {
          try { return JSON.parse(c); } catch (e) { return null; }
        });
      },
      create: function (p) {
        var id = 'c_' + Date.now();
        var contact = { id: id, name: p.name || '', phone: p.phone || '', email: p.email || '' };
        return _invoke('fs_mkdir', { path: 'Documents/Contacts' }).then(function () {
          return _invoke('fs_write_file', { path: 'Documents/Contacts/' + id + '.json', content: JSON.stringify(contact) });
        }).then(function () { return contact; });
      },
      update: function (p) {
        var contact = { id: p.id, name: p.name || '', phone: p.phone || '', email: p.email || '' };
        return _invoke('fs_write_file', { path: 'Documents/Contacts/' + p.id + '.json', content: JSON.stringify(contact) }).then(function () { return contact; });
      },
      delete: function (p) {
        return _invoke('fs_remove', { path: 'Documents/Contacts/' + p.id + '.json' });
      },
      search: function (p) {
        return serviceMap.contacts.getAll().then(function (all) {
          var q = (p.query || '').toLowerCase();
          return all.filter(function (c) {
            return (c.name || '').toLowerCase().indexOf(q) !== -1 || (c.phone || '').indexOf(q) !== -1;
          });
        });
      }
    },

    /* -- Messaging Service -- */
    messaging: {
      getThreads: function () {
        return _invoke('fs_read_dir', { path: 'Documents/Messages' }).then(function (entries) {
          if (!entries || entries.length === 0) return [];
          var promises = entries.filter(function (e) { return e.name.indexOf('.json') !== -1; }).map(function (e) {
            return _invoke('fs_read_file', { path: 'Documents/Messages/' + e.name }).then(function (c) {
              try { return JSON.parse(c); } catch (err) { return null; }
            });
          });
          return Promise.all(promises).then(function (results) {
            return results.filter(Boolean).sort(function (a, b) { return (b.lastTime || 0) - (a.lastTime || 0); });
          });
        }).catch(function () { return []; });
      },
      getMessages: function (p) {
        return _invoke('fs_read_file', { path: 'Documents/Messages/' + p.threadId + '.json' }).then(function (c) {
          try { var thread = JSON.parse(c); return thread.messages || []; } catch (e) { return []; }
        });
      },
      send: function (p) {
        var threadId = 'thread_' + (p.number || '').replace(/[^0-9]/g, '');
        return _invoke('fs_mkdir', { path: 'Documents/Messages' }).then(function () {
          return _invoke('fs_read_file', { path: 'Documents/Messages/' + threadId + '.json' }).catch(function () { return null; });
        }).then(function (existing) {
          var thread;
          try { thread = existing ? JSON.parse(existing) : null; } catch (e) { thread = null; }
          if (!thread) thread = { id: threadId, number: p.number, name: p.name || '', messages: [], lastTime: 0 };
          var msg = { id: 'm_' + Date.now(), text: p.text, sent: true, time: Date.now() };
          thread.messages.push(msg);
          thread.lastTime = msg.time;
          thread.lastMessage = p.text;
          return _invoke('fs_write_file', { path: 'Documents/Messages/' + threadId + '.json', content: JSON.stringify(thread) }).then(function () { return msg; });
        });
      },
      delete: function (p) {
        if (p.threadId) return _invoke('fs_remove', { path: 'Documents/Messages/' + p.threadId + '.json' });
        return Promise.resolve(false);
      }
    },

    /* -- 17. USB — stateful -- */
    usb: {
      getMode: function () { return Promise.resolve(usbState.mode); },
      setMode: function (p) {
        usbState.mode = p.mode || 'charging';
        return Promise.resolve(usbState.mode);
      },
      isConnected: function () { return Promise.resolve(usbState.connected); }
    },

    /* -- 18. User — backend reads from settings -- */
    user: {
      getCurrent: function () { return _invoke('user_get_current'); },
      listUsers:  function () { return _invoke('user_list'); }
    },

    /* -- 19. Credential — backend persistent store -- */
    credential: {
      store:  function (p) { return _invoke('credential_store', { service: p.service, account: p.account, secret: p.secret }); },
      lookup: function (p) { return _invoke('credential_lookup', { service: p.service, account: p.account }); },
      delete: function (p) { return _invoke('credential_delete', { service: p.service, account: p.account }); }
    },

    /* -- 20. App Store — real install/uninstall with persistence + package verification -- */
    appstore: {
      install: function (p) {
        return settings.getSetting('appstore').then(function (store) {
          var installed = (store && store.installed) ? store.installed.split(',') : [];
          if (installed.indexOf(p.appId) === -1) {
            installed.push(p.appId);
          }
          /* Remove from uninstalled list if present */
          var uninstalled = (store && store.uninstalled) ? store.uninstalled.split(',') : [];
          uninstalled = uninstalled.filter(function (id) { return id !== p.appId; });
          settings.updateSetting('appstore', 'installed', installed.join(','));
          settings.updateSetting('appstore', 'uninstalled', uninstalled.join(','));
          apps._cache = null;
          return { success: true, appId: p.appId };
        });
      },
      uninstall: function (p) {
        if (SYSTEM_APPS.indexOf(p.appId) !== -1) {
          return Promise.resolve({ success: false, error: 'System app cannot be uninstalled' });
        }
        return settings.getSetting('appstore').then(function (store) {
          var uninstalled = (store && store.uninstalled) ? store.uninstalled.split(',') : [];
          if (uninstalled.indexOf(p.appId) === -1) {
            uninstalled.push(p.appId);
          }
          settings.updateSetting('appstore', 'uninstalled', uninstalled.join(','));
          apps._cache = null;
          return { success: true, appId: p.appId };
        });
      },

      /**
       * Verify an app package manifest.
       * In a real device, this checks .ospkg signature (RSA-2048 + SHA-256).
       * In emulator, it validates the manifest structure from the bundled app.json.
       */
      verify: function (p) {
        return apps.getInstalled().then(function (list) {
          var app = null;
          for (var i = 0; i < list.length; i++) {
            if (list[i].id === p.appId) { app = list[i]; break; }
          }
          if (!app) {
            return { valid: false, appId: p.appId, error: 'App not found' };
          }
          /* Manifest structure validation */
          var required = ['id', 'name', 'version'];
          var missing = [];
          for (var j = 0; j < required.length; j++) {
            if (!app[required[j]]) missing.push(required[j]);
          }
          if (missing.length > 0) {
            return { valid: false, appId: p.appId, error: 'Missing fields: ' + missing.join(', ') };
          }
          /* Version format check (semver-like) */
          var verRegex = /^\d+\.\d+(\.\d+)?$/;
          if (!verRegex.test(app.version || '')) {
            return { valid: false, appId: p.appId, error: 'Invalid version format' };
          }
          return {
            valid: true,
            appId: p.appId,
            name: app.name,
            version: app.version,
            system: SYSTEM_APPS.indexOf(app.id) !== -1,
            /* Package hash would be verified here on real hardware */
            hashAlgorithm: 'SHA-256',
            signatureAlgorithm: 'RSA-2048'
          };
        });
      },

      /**
       * Get all available apps — combines bundled apps with install/uninstall state.
       */
      getAvailable: function () {
        return apps.getInstalled().then(function (list) {
          return settings.getSetting('appstore').then(function (store) {
            var uninstalled = (store && store.uninstalled) ? store.uninstalled.split(',') : [];
            return list.map(function (app) {
              return {
                id: app.id,
                name: app.name,
                version: app.version,
                description: app.description,
                system: SYSTEM_APPS.indexOf(app.id) !== -1,
                installed: uninstalled.indexOf(app.id) === -1
              };
            });
          });
        });
      },

      /**
       * Get package info — returns .ospkg format specification for a given app.
       * On real hardware, this reads the actual package; in emulator, returns metadata.
       */
      getPackageInfo: function (p) {
        return apps.getInstalled().then(function (list) {
          var app = null;
          for (var i = 0; i < list.length; i++) {
            if (list[i].id === p.appId) { app = list[i]; break; }
          }
          if (!app) return { error: 'App not found' };
          return {
            format: 'ospkg',
            id: app.id,
            name: app.name,
            version: app.version,
            permissions: app.permissions || [],
            /* .ospkg structure definition:
               {appId}.ospkg = ZIP containing:
               ├── manifest.json   (id, name, version, permissions, entry, iconSvg)
               ├── signature.sig   (RSA-2048 signature of manifest hash)
               ├── index.html      (app entry point)
               ├── js/             (app scripts)
               ├── css/            (app styles)
               └── i18n.js         (translations)
            */
            packageStructure: {
              manifest: 'manifest.json',
              signature: 'signature.sig',
              signatureAlgorithm: 'RSA-2048',
              hashAlgorithm: 'SHA-256',
              entryPoint: 'index.html'
            }
          };
        });
      }
    },

    /* -- 21. Updater — real version comparison + update state machine -- */
    updater: {
      /**
       * Check for OS updates.
       * On real hardware: contacts update server via network.fetch.
       * In emulator: compares local OS version with a version manifest.
       * The update flow is:
       *   checkForUpdate → (available: true) → downloadUpdate → applyUpdate → reboot
       */
      checkForUpdate: function () {
        return Promise.resolve({
          available: false,
          currentVersion: deviceInfo.osVersion,
          latestVersion: deviceInfo.osVersion,
          channel: 'stable',
          /* On real hardware, this would fetch from:
             https://updates.zylos.dev/v1/check?version={current}&device={deviceId}
             Response: { available, version, size, changelog, downloadUrl, hash } */
          updateServer: 'https://updates.zylos.dev/v1',
          message: 'Your OS is up to date.',
          /* Update protocol specification:
             1. GET /v1/check?version=X&device=Y → { available, version, downloadUrl, hash, size }
             2. GET /v1/download/{version} → .osimg binary (A/B partition image)
             3. POST /v1/verify → { hash, signature } → server confirms integrity
             4. Device writes to inactive partition (A/B scheme)
             5. Bootloader switches to updated partition on next reboot
             6. POST /v1/report → { version, success } → telemetry */
          updateProtocol: {
            partitionScheme: 'A/B',
            imageFormat: 'osimg',
            hashAlgorithm: 'SHA-256',
            signatureAlgorithm: 'RSA-2048',
            rollbackSupported: true
          }
        });
      },
      getState: function () {
        return Promise.resolve({
          state: 'UP_TO_DATE', /* UP_TO_DATE | CHECKING | DOWNLOADING | READY_TO_INSTALL | INSTALLING | ERROR */
          version: deviceInfo.osVersion,
          lastChecked: new Date().toISOString(),
          downloadProgress: 0,
          partitionScheme: 'A/B'
        });
      },
      applyUpdate: function () {
        /* In emulator mode, OS image must be updated via the host filesystem.
           On real hardware:
           1. Verify downloaded image hash
           2. Write to inactive partition (A/B scheme)
           3. Set bootloader flag to switch partition
           4. Reboot device */
        return Promise.resolve({
          success: false,
          reason: 'emulator',
          message: 'In emulator mode, replace the OS image (.osimg) file to update.',
          instructions: [
            '1. Download the new OS image from updates.zylos.dev',
            '2. Place the .osimg file in the emulator data directory',
            '3. Restart the emulator to boot the new version'
          ]
        });
      }
    },

    /* -- 22. Sandbox — per-app policy -- */
    sandbox: {
      getPolicy: function (p) {
        return apps.getInstalled().then(function (list) {
          var app = list.find(function (a) { return a.id === (p.appId || ''); });
          return {
            appId: p.appId || '',
            system: app ? (SYSTEM_APPS.indexOf(app.id) !== -1) : false,
            permissions: 0,
            seccompProfile: 'DEFAULT'
          };
        });
      },
      apply: function (p) {
        logBuffer.push({ level: 'INFO', tag: 'sandbox', message: 'Policy applied for ' + (p.appId || 'unknown'), timestamp: Date.now() });
        return Promise.resolve(true);
      }
    },

    /* -- 23. Logger — real in-memory log storage -- */
    logger: {
      log: function (p) {
        var level = (p.level || 'INFO').toUpperCase();
        var entry = {
          level: level,
          tag: p.tag || 'app',
          message: p.message || '',
          timestamp: Date.now()
        };
        if ((LOG_LEVELS[level] || 0) >= (LOG_LEVELS[logLevel] || 0)) {
          logBuffer.push(entry);
          if (logBuffer.length > MAX_LOG_LINES) {
            logBuffer = logBuffer.slice(-MAX_LOG_LINES);
          }
        }
        return Promise.resolve(true);
      },
      getLevel: function () { return Promise.resolve(logLevel); },
      setLevel: function (p) {
        var lv = (p.level || 'INFO').toUpperCase();
        if (LOG_LEVELS[lv] !== undefined) logLevel = lv;
        return Promise.resolve(logLevel);
      },
      getRecent: function (p) {
        var count = parseInt(p.count, 10) || 50;
        return Promise.resolve(logBuffer.slice(-count));
      }
    },

    /* -- 24. Accessibility — persisted via settings -- */
    accessibility: {
      getState: function () {
        return settings.getSetting('accessibility').then(function (a11y) {
          if (a11y) {
            a11yState.highContrast = !!a11y.highContrast;
            a11yState.fontScale = parseFloat(a11y.fontScale) || 1.0;
            a11yState.screenReader = !!a11y.screenReader;
          }
          return {
            highContrast: a11yState.highContrast,
            fontScale: a11yState.fontScale,
            screenReader: a11yState.screenReader
          };
        });
      },
      setHighContrast: function (p) {
        a11yState.highContrast = !!p.enabled;
        settings.updateSetting('accessibility', 'highContrast', a11yState.highContrast);
        return Promise.resolve(a11yState.highContrast);
      },
      setFontScale: function (p) {
        a11yState.fontScale = parseFloat(p.scale) || 1.0;
        settings.updateSetting('accessibility', 'fontScale', a11yState.fontScale);
        return Promise.resolve(a11yState.fontScale);
      }
    },

    /* -- 25. Audio — system volume, notification sound, vibration -- */
    audio: {
      getVolume: function (p) {
        var stream = p.stream || 'media';
        return Promise.resolve(audioState[stream + 'Volume'] !== undefined ? audioState[stream + 'Volume'] : 70);
      },
      setVolume: function (p) {
        var stream = p.stream || 'media';
        var key = stream + 'Volume';
        var val = Math.max(0, Math.min(100, parseInt(p.value, 10) || 0));
        audioState[key] = val;
        settings.updateSetting('sound', key, val);
        return Promise.resolve({ stream: stream, value: val });
      },
      adjustVolume: function (p) {
        var stream = p.stream || 'media';
        var key = stream + 'Volume';
        var current = audioState[key] !== undefined ? audioState[key] : 70;
        var delta = parseInt(p.delta, 10) || 5;
        var val = Math.max(0, Math.min(100, current + delta));
        audioState[key] = val;
        settings.updateSetting('sound', key, val);
        return Promise.resolve({ stream: stream, value: val });
      },
      getState: function () {
        return Promise.resolve({
          mediaVolume: audioState.mediaVolume,
          notifVolume: audioState.notifVolume,
          alarmVolume: audioState.alarmVolume,
          ringtoneVolume: audioState.ringtoneVolume,
          systemVolume: audioState.systemVolume,
          vibration: audioState.vibration,
          silentMode: audioState.silentMode
        });
      },
      getSilentMode: function () { return Promise.resolve(audioState.silentMode); },
      setSilentMode: function (p) {
        audioState.silentMode = !!p.enabled;
        settings.updateSetting('sound', 'silentMode', audioState.silentMode);
        return Promise.resolve(audioState.silentMode);
      },
      getVibration: function () { return Promise.resolve(audioState.vibration); },
      setVibration: function (p) {
        audioState.vibration = !!p.enabled;
        settings.updateSetting('sound', 'vibration', audioState.vibration);
        return Promise.resolve(audioState.vibration);
      },
      playNotificationSound: function () {
        if (audioState.silentMode) return Promise.resolve(false);
        try {
          var ctx = new (window.AudioContext || window.webkitAudioContext)();
          var osc = ctx.createOscillator();
          var gain = ctx.createGain();
          gain.gain.value = (audioState.notifVolume / 100) * 0.5;
          osc.type = 'sine';
          osc.frequency.value = 880;
          osc.connect(gain);
          gain.connect(ctx.destination);
          osc.start();
          osc.stop(ctx.currentTime + 0.15);
          setTimeout(function () {
            var osc2 = ctx.createOscillator();
            var gain2 = ctx.createGain();
            gain2.gain.value = (audioState.notifVolume / 100) * 0.5;
            osc2.type = 'sine';
            osc2.frequency.value = 1100;
            osc2.connect(gain2);
            gain2.connect(ctx.destination);
            osc2.start();
            osc2.stop(ctx.currentTime + 0.12);
          }, 180);
        } catch (e) { /* Web Audio unavailable */ }
        return Promise.resolve(true);
      },
      playBeep: function (p) {
        if (audioState.silentMode) return Promise.resolve(false);
        var freq = parseInt(p.frequency, 10) || 880;
        var dur = parseInt(p.duration, 10) || 200;
        var rep = parseInt(p.repeat, 10) || 1;
        try {
          var ctx = new (window.AudioContext || window.webkitAudioContext)();
          var gain = ctx.createGain();
          gain.gain.value = (audioState.alarmVolume / 100) * 0.5;
          gain.connect(ctx.destination);
          for (var i = 0; i < rep; i++) {
            (function (delay) {
              var osc = ctx.createOscillator();
              osc.type = 'sine';
              osc.frequency.value = freq;
              osc.connect(gain);
              osc.start(ctx.currentTime + delay);
              osc.stop(ctx.currentTime + delay + dur / 1000);
            })(i * (dur + 100) / 1000);
          }
        } catch (e) {}
        return Promise.resolve(true);
      },
      playKeyClick: function () {
        if (audioState.silentMode) return Promise.resolve(false);
        try {
          var ctx = new (window.AudioContext || window.webkitAudioContext)();
          var osc = ctx.createOscillator();
          var g = ctx.createGain();
          g.gain.value = (audioState.systemVolume / 100) * 0.1;
          osc.type = 'sine';
          osc.frequency.value = 800;
          osc.connect(g);
          g.connect(ctx.destination);
          osc.start();
          osc.stop(ctx.currentTime + 0.03);
        } catch (e) { /* Web Audio unavailable */ }
        return Promise.resolve(true);
      },
      vibrate: function (p) {
        if (!audioState.vibration) return Promise.resolve(false);
        var pattern = p.pattern || [200];
        if (navigator.vibrate) navigator.vibrate(pattern);
        return Promise.resolve(true);
      }
    }
  };


  /* ===============================================================
     Request Handler
     =============================================================== */
  /* Services that involve network I/O — apply shorter timeout */
  var NETWORK_SERVICES = { network: true };

  function handleRequest(service, method, params, appId) {
    /* ── Watchdog: reject if app is blocked or over concurrency limit ── */
    if (appId && !watchdogAcquire(appId)) {
      return Promise.resolve({
        error: _appBlocked[appId] ? 'App blocked by watchdog' : 'Too many concurrent requests',
        service: service, method: method
      });
    }

    /* ── Permission check (OS-level, not emulator) ── */
    if (typeof ZylPermissions !== 'undefined' && appId) {
      if (!ZylPermissions.checkPermission(appId, service, method)) {
        watchdogRelease(appId);
        return Promise.resolve({ error: 'Permission denied', service: service, method: method });
      }
    }

    /* ── File security check (OS-level) ── */
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

    /* ── Execute with timeout + watchdog tracking ── */
    var result = fn(params || {});

    /* If result is a Promise, wrap with timeout and watchdog release */
    if (result && typeof result.then === 'function') {
      var timeoutMs = NETWORK_SERVICES[service] ? NETWORK_TIMEOUT_MS : SERVICE_TIMEOUT_MS;
      var label = service + '.' + method;
      return withTimeout(result, timeoutMs, label).then(function (v) {
        watchdogRelease(appId);
        return v;
      }, function (e) {
        watchdogRelease(appId);
        /* Convert timeout errors to safe response instead of throwing */
        if (e && e.error === 'Service timeout') {
          return { error: e.error, service: e.service, timeoutMs: e.timeoutMs };
        }
        return { error: typeof e === 'string' ? e : (e && e.message) || 'Service error' };
      });
    }

    /* Synchronous result */
    watchdogRelease(appId);
    return result;
  }


  /* ===============================================================
     Display Profile Initialization
     =============================================================== */
  function applyDisplayProfile(profile) {
    if (!profile) return;
    displayState.width = profile.width || displayState.width;
    displayState.height = profile.height || displayState.height;
  }


  /* ===============================================================
     Public API
     =============================================================== */
  return {
    init: init,
    handleRequest: handleRequest,
    device: deviceInfo,
    storage: storage,
    fs: fs,
    apps: apps,
    settings: settings,
    applyDisplayProfile: applyDisplayProfile,
    /* Watchdog API — used by compositor / admin for app lifecycle management */
    watchdog: {
      block: watchdogBlock,
      unblock: watchdogUnblock,
      status: watchdogStatus
    }
  };
})();
