// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service
//
// Role: Zyl OS system service framework — all 25 OS service business logic
// Scope: fs, device, storage, apps, settings, terminal, wifi, bluetooth,
//        browser, notification, power, display, input, sensors, location,
//        telephony, usb, user, credential, appstore, updater, sandbox,
//        logger, accessibility, audio
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

  function init(invoker) {
    _invoke = invoker || _invoke;
    /* Load persisted settings and audio state on boot */
    settings._loadFromBackend().then(function () {
      var s = settings.state.sound;
      if (s) {
        if (s.mediaVolume !== undefined) audioState.mediaVolume = parseInt(s.mediaVolume, 10);
        if (s.notifVolume !== undefined) audioState.notifVolume = parseInt(s.notifVolume, 10);
        if (s.alarmVolume !== undefined) audioState.alarmVolume = parseInt(s.alarmVolume, 10);
        if (s.ringtoneVolume !== undefined) audioState.ringtoneVolume = parseInt(s.ringtoneVolume, 10);
        if (s.vibration !== undefined) audioState.vibration = !!s.vibration;
        if (s.silentMode !== undefined) audioState.silentMode = !!s.silentMode;
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
      return _invoke('fs_read_dir', { path: path || '/' });
    },
    getUnixDirectory: function (path) {
      var p = _invoke('fs_read_dir', { path: path || '/' });
      if (p && typeof p.then === 'function') {
        return p.then(function (entries) {
          return (entries || []).map(function (e) {
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
        return { tree: { '/': entries || [] }, unixTree: {}, fileContents: {} };
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
              system: m.system || false,
              permissions: m.permissions || []
            };
          });
          /* Register app permissions with the permission system */
          if (typeof ZylPermissions !== 'undefined' && ZylPermissions.registerFromAppList) {
            ZylPermissions.registerFromAppList(apps._cache);
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
    ringtoneVolume: 80, vibration: true, silentMode: false
  };

  /* -- System apps list (used by appstore/sandbox) -- */
  var SYSTEM_APPS = [
    'com.zylos.home', 'com.zylos.lockscreen', 'com.zylos.statusbar',
    'com.zylos.oobe', 'com.zylos.settings', 'com.zylos.browser',
    'com.zylos.files', 'com.zylos.terminal', 'com.zylos.camera',
    'com.zylos.gallery', 'com.zylos.music', 'com.zylos.clock',
    'com.zylos.calc', 'com.zylos.notes', 'com.zylos.weather',
    'com.zylos.store', 'com.zylos.keyboard'
  ];


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
      getCallState: function () { return Promise.resolve({ state: 'IDLE', number: '' }); }
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

    /* -- 20. App Store — real install/uninstall with persistence -- */
    appstore: {
      install: function (p) {
        return settings.getSetting('appstore').then(function (store) {
          var installed = (store && store.installed) ? store.installed.split(',') : [];
          if (installed.indexOf(p.appId) === -1) {
            installed.push(p.appId);
          }
          settings.updateSetting('appstore', 'installed', installed.join(','));
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
      verify: function (p) {
        return apps.getInstalled().then(function (list) {
          var found = list.some(function (a) { return a.id === p.appId; });
          return { valid: found, appId: p.appId };
        });
      },
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
      }
    },

    /* -- 21. Updater — version comparison -- */
    updater: {
      checkForUpdate: function () {
        return Promise.resolve({
          available: false,
          currentVersion: deviceInfo.osVersion,
          message: 'Replace the OS image file manually to update.'
        });
      },
      getState: function () {
        return Promise.resolve({ state: 'UP_TO_DATE', version: deviceInfo.osVersion });
      },
      applyUpdate: function () {
        return Promise.resolve({
          success: false,
          message: 'In emulator mode, update the OS image file manually.'
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
      playKeyClick: function () {
        if (audioState.silentMode) return Promise.resolve(false);
        try {
          var ctx = new (window.AudioContext || window.webkitAudioContext)();
          var osc = ctx.createOscillator();
          var g = ctx.createGain();
          g.gain.value = 0.05;
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
  function handleRequest(service, method, params) {
    var svc = serviceMap[service];
    if (!svc) return null;
    var fn = svc[method];
    if (!fn) return null;
    return fn(params || {});
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
    applyDisplayProfile: applyDisplayProfile
  };
})();
