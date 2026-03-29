// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Service
//
// 역할: Zyl OS 시스템 서비스 — Tauri 백엔드를 통해 앱에 데이터 제공
// 수행범위: 파일시스템, 디바이스정보, 저장공간, 앱목록, 설정
// 의존방향: Tauri invoke (백엔드 커맨드), emulator.js (postMessage 라우팅)
// SOLID: SRP — 서비스 라우팅만 담당, DIP — 백엔드에 의존
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

var ZylServices = (function () {
  'use strict';

  var IS_TAURI = (typeof window !== 'undefined' && typeof window.__TAURI__ !== 'undefined');

  function tauriInvoke(cmd, args) {
    if (!IS_TAURI) return Promise.resolve(null);
    try {
      return window.__TAURI__.core.invoke(cmd, args || {});
    } catch (e) {
      return Promise.resolve(null);
    }
  }

  /* ═══════════════════════════════════════════════════════
     1. FileSystemService — 마운트된 디스크 이미지 I/O
     ═══════════════════════════════════════════════════════ */
  var fs = {
    getDirectory: function (path) {
      return tauriInvoke('fs_read_dir', { path: path || '/' });
    },
    getUnixDirectory: function (path) {
      var p = tauriInvoke('fs_read_dir', { path: path || '/' });
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
      return tauriInvoke('fs_read_file', { path: path || '' });
    },
    getAllData: function () {
      return tauriInvoke('fs_read_dir', { path: '/' }).then(function (entries) {
        return { tree: { '/': entries || [] }, unixTree: {}, fileContents: {} };
      }).catch(function () {
        return { tree: { '/': [] }, unixTree: {}, fileContents: {} };
      });
    },
    writeFile: function (params) {
      return tauriInvoke('fs_write_file', { path: params.path, content: params.content });
    },
    mkdir: function (params) {
      return tauriInvoke('fs_mkdir', { path: params.path });
    },
    remove: function (params) {
      return tauriInvoke('fs_remove', { path: params.path });
    },
    rename: function (params) {
      return tauriInvoke('fs_rename', { old_path: params.oldPath, new_path: params.newPath });
    }
  };


  /* ═══════════════════════════════════════════════════════
     2. DeviceInfoService — 디바이스 정보
     ═══════════════════════════════════════════════════════ */
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
    }
  };


  /* ═══════════════════════════════════════════════════════
     3. StorageService — 디스크 사용량 (마운트 포인트)
     ═══════════════════════════════════════════════════════ */
  var storage = {
    _cache: null,
    _cacheTime: 0,

    _fetchFromBackend: function () {
      return tauriInvoke('fs_get_usage').then(function (s) {
        storage._cache = s;
        storage._cacheTime = Date.now();
        return s;
      }).catch(function () {
        return storage._cache || { total: 0, used: 0, available: 0, percent: 0 };
      });
    },

    getUsage: function () {
      if (storage._cache && Date.now() - storage._cacheTime < 30000) {
        return storage._cache;
      }
      storage._fetchFromBackend();
      return storage._cache || { total: 0, used: 0, available: 0, percent: 0 };
    },

    prefetch: function () {
      storage._fetchFromBackend();
    },

    getFormatted: function () {
      var s = storage.getUsage();
      return {
        total: formatBytes(s.total),
        used: formatBytes(s.used),
        available: formatBytes(s.available),
        percent: Math.round(s.percent || 0)
      };
    }
  };


  /* ═══════════════════════════════════════════════════════
     4. AppRegistryService — 설치된 앱 (OS 이미지에서 조회)
     ═══════════════════════════════════════════════════════ */
  var apps = {
    _cache: null,

    getInstalled: function () {
      if (apps._cache) return apps._cache;
      return tauriInvoke('list_installed_apps').then(function (list) {
        if (list && list.length > 0) {
          apps._cache = list.map(function (m) {
            var appName = (m.id || '').split('.').pop();
            return {
              id: m.id,
              nameKey: 'app.' + appName,
              icon: m.iconKey || appName,
              color: m.color || 'icon-blue',
              version: m.version || '1.0.0'
            };
          });
        }
        return apps._cache || [];
      }).catch(function () { return []; });
    },

    getById: function (id) {
      if (!apps._cache) return null;
      return apps._cache.find(function (a) { return a.id === id; }) || null;
    }
  };


  /* ═══════════════════════════════════════════════════════
     5. SettingsService — 영속화 설정 (마운트 포인트 JSON)
     ═══════════════════════════════════════════════════════ */
  var settings = {
    state: {},
    _loaded: false,

    _loadFromBackend: function () {
      if (settings._loaded) return;
      tauriInvoke('load_settings').then(function (data) {
        if (data) {
          Object.keys(data).forEach(function (cat) {
            settings.state[cat] = data[cat];
          });
          settings._loaded = true;
        }
      }).catch(function () {});
    },

    getSetting: function (category) {
      settings._loadFromBackend();
      return settings.state[category] || null;
    },

    updateSetting: function (category, key, value) {
      if (!settings.state[category]) {
        settings.state[category] = {};
      }
      settings.state[category][key] = value;
      tauriInvoke('save_settings', {
        category: category,
        key: key,
        value: value
      }).catch(function () {});
      return settings.state[category];
    }
  };


  /* ═══════════════════════════════════════════════════════
     Service Router
     ═══════════════════════════════════════════════════════ */
  var serviceMap = {
    fs: {
      getDirectory:     function (p) { return fs.getDirectory(p.path); },
      getUnixDirectory: function (p) { return fs.getUnixDirectory(p.path); },
      getFileContent:   function (p) { return fs.getFileContent(p.path); },
      readBinary:       function (p) { return fs.readBinary(p); },
      getAllData:        function ()  { return fs.getAllData(); },
      readBinary:       function (p) { return tauriInvoke('fs_read_binary', { path: p.path }); },
      writeFile:        function (p) { return fs.writeFile(p); },
      mkdir:            function (p) { return fs.mkdir(p); },
      remove:           function (p) { return fs.remove(p); },
      rename:           function (p) { return fs.rename(p); }
    },
    device: {
      getInfo: function () { return deviceInfo.getInfo(); }
    },
    storage: {
      getUsage:     function () { return storage.getUsage(); },
      getFormatted: function () { return storage.getFormatted(); }
    },
    apps: {
      getInstalled: function () { return apps.getInstalled(); },
      getById:      function (p) { return apps.getById(p.id); }
    },
    settings: {
      get:    function (p) { return settings.getSetting(p.category); },
      update: function (p) { return settings.updateSetting(p.category, p.key, p.value); }
    },
    terminal: {
      exec: function (p) { return tauriInvoke('exec_command', { command: p.command }); }
    },
    wifi: {
      getNetworks:  function () { return tauriInvoke('get_wifi_networks'); },
      getConnected: function () {
        return tauriInvoke('get_wifi_networks').then(function (nets) {
          return (nets || []).filter(function (n) { return n.connected; });
        });
      }
    },
    bluetooth: {
      getDevices:   function () { return tauriInvoke('get_bluetooth_devices'); },
      getPaired:    function () { return tauriInvoke('get_bluetooth_devices'); },
      getConnected: function () {
        return tauriInvoke('get_bluetooth_devices').then(function (devs) {
          return (devs || []).filter(function (d) { return d.connected; });
        });
      }
    },
    /* browser: 앱 자체가 기본 데이터를 보유. 에뮬레이터는 라우팅만 제공 */
    browser: {
      getBookmarks: function () { return Promise.resolve(null); },
      getQuickLinks: function () { return Promise.resolve(null); }
    },
    /* ── 에뮬레이션 시스템 서비스 (실기기 D-Bus 서비스 대응) ── */
    notification: {
      post:     function (p) { return tauriInvoke('notif_post', { app_id: p.appId || '', title: p.title || '', body: p.body || '', icon: p.icon || '', priority: p.priority || 1 }); },
      cancel:   function (p) { return tauriInvoke('notif_cancel', { id: p.id }); },
      getActive: function () { return tauriInvoke('notif_get_active'); },
      clearAll: function () { return tauriInvoke('notif_clear_all'); }
    },
    power: {
      getState:      function () { return tauriInvoke('power_get_state'); },
      setBrightness: function (p) { return tauriInvoke('power_set_brightness', { percent: p.percent || 80 }); }
    },
    display: {
      getMode:     function () { return Promise.resolve({ width: 1080, height: 2400, refresh: 60 }); },
      getRotation: function () { return Promise.resolve(0); },
      setRotation: function (p) { return Promise.resolve(p.rotation || 0); },
      getScale:    function () { return Promise.resolve(1.0); },
      setScale:    function (p) { return Promise.resolve(p.scale || 1.0); }
    },
    input: {
      showKeyboard: function (p) { return Promise.resolve({ visible: true, layout: p.layout || 'en' }); },
      hideKeyboard: function () { return Promise.resolve({ visible: false }); },
      getState:     function () { return Promise.resolve({ visible: false, layout: 'en' }); }
    },
    sensors: {
      getLatest: function (p) {
        var type = (p && p.type) || 'accelerometer';
        var defaults = {
          accelerometer: { type: 'accelerometer', values: [0, 0, -9.8], timestamp: Date.now() },
          gyroscope:     { type: 'gyroscope', values: [0, 0, 0], timestamp: Date.now() },
          proximity:     { type: 'proximity', values: [0, 5.0], timestamp: Date.now() },
          light:         { type: 'light', values: [300], timestamp: Date.now() },
          magnetometer:  { type: 'magnetometer', values: [0, 25, -45], timestamp: Date.now() }
        };
        return Promise.resolve(defaults[type] || defaults.accelerometer);
      }
    },
    location: {
      getLastKnown:   function () { return tauriInvoke('location_get_last_known'); },
      requestUpdates: function () { return Promise.resolve(true); },
      stopUpdates:    function () { return Promise.resolve(true); }
    },
    telephony: {
      getState: function () {
        return Promise.resolve({
          simPresent: true, operator: 'Zyl Mobile', networkType: 'LTE',
          signal: 3, imei: '000000000000000', phoneNumber: '+82-10-0000-0000'
        });
      },
      getCallState: function () { return Promise.resolve({ state: 'IDLE', number: '' }); }
    },
    usb: {
      getMode:     function () { return Promise.resolve('charging'); },
      setMode:     function (p) { return Promise.resolve(p.mode || 'charging'); },
      isConnected: function () { return Promise.resolve(true); }
    },
    user: {
      getCurrent: function () { return tauriInvoke('user_get_current'); },
      listUsers:  function () { return tauriInvoke('user_list'); }
    },
    credential: {
      store:  function (p) { return tauriInvoke('credential_store', { service: p.service, account: p.account, secret: p.secret }); },
      lookup: function (p) { return tauriInvoke('credential_lookup', { service: p.service, account: p.account }); },
      delete: function (p) { return tauriInvoke('credential_delete', { service: p.service, account: p.account }); }
    },
    appstore: {
      install:   function (p) { return Promise.resolve({ success: true, appId: p.appId }); },
      uninstall: function (p) {
        var SYSTEM_APPS = [
          'com.zylos.home', 'com.zylos.lockscreen', 'com.zylos.statusbar',
          'com.zylos.oobe', 'com.zylos.settings', 'com.zylos.browser',
          'com.zylos.files', 'com.zylos.terminal', 'com.zylos.camera',
          'com.zylos.gallery', 'com.zylos.music', 'com.zylos.clock',
          'com.zylos.calc', 'com.zylos.notes', 'com.zylos.weather',
          'com.zylos.store'
        ];
        if (SYSTEM_APPS.indexOf(p.appId) !== -1) {
          return Promise.resolve({ success: false, error: 'System app cannot be uninstalled' });
        }
        return Promise.resolve({ success: true, appId: p.appId });
      },
      verify:    function (p) { return Promise.resolve({ valid: true, appId: p.appId }); },
      getAvailable: function () { return Promise.resolve([]); }
    },
    updater: {
      checkForUpdate: function () { return Promise.resolve({ available: false, currentVersion: '0.1.0' }); },
      getState:       function () { return Promise.resolve({ state: 'UP_TO_DATE', version: '0.1.0' }); },
      applyUpdate:    function () { return Promise.resolve({ success: false, message: 'No update available' }); }
    },
    sandbox: {
      getPolicy: function (p) { return Promise.resolve({ appId: p.appId || '', permissions: 0, seccompProfile: 'DEFAULT' }); },
      apply:     function ()  { return Promise.resolve(true); }
    },
    logger: {
      log:      function (p) { return Promise.resolve(true); },
      getLevel: function ()  { return Promise.resolve('INFO'); },
      setLevel: function (p) { return Promise.resolve(p.level || 'INFO'); }
    },
    accessibility: {
      getState:        function () { return Promise.resolve({ highContrast: false, fontScale: 1.0, screenReader: false }); },
      setHighContrast: function (p) { return Promise.resolve(p.enabled || false); },
      setFontScale:    function (p) { return Promise.resolve(p.scale || 1.0); }
    }
  };

  function handleRequest(service, method, params) {
    var svc = serviceMap[service];
    if (!svc) return null;
    var fn = svc[method];
    if (!fn) return null;
    return fn(params || {});
  }

  function formatBytes(bytes) {
    if (!bytes) return '0 B';
    var units = ['B', 'KB', 'MB', 'GB'];
    var i = Math.floor(Math.log(bytes) / Math.log(1024));
    if (i >= units.length) i = units.length - 1;
    return (bytes / Math.pow(1024, i)).toFixed(1) + ' ' + units[i];
  }

  return {
    handleRequest: handleRequest,
    device: deviceInfo,
    storage: storage,
    fs: fs,
    apps: apps,
    settings: settings
  };
})();
