// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Service
//
// 역할: Zyl OS 시스템 서비스 — Tauri 백엔드를 통해 앱에 데이터 제공
// 수행범위: 파일시스템, 디바이스정보, 저장공간, 앱목록, 설정
// 의존방향: Tauri invoke (백엔드 커맨드), emulator.js (postMessage 라우팅)
// SOLID: SRP — 서비스 라우팅만 담당, DIP — 백엔드에 의존
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
      getAllData:        function ()  { return fs.getAllData(); }
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
