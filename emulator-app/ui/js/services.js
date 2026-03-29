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
      getAllData:        function ()  { return fs.getAllData(); },
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
    /* browser 서비스: 실기기에서는 앱 내부 DB에서 로드.
       에뮬레이터에서는 기본 북마크를 제공 (앱 초기 데이터 역할) */
    browser: {
      getBookmarks: function () {
        return Promise.resolve([
          { name: 'Zyl OS', url: 'https://www.zylos.dev', favicon: 'Z' },
          { name: 'GitHub', url: 'https://github.com', favicon: 'G' },
          { name: 'RISC-V', url: 'https://riscv.org', favicon: 'R' }
        ]);
      },
      getQuickLinks: function () {
        return Promise.resolve([
          { name: 'GitHub', url: 'https://github.com', iconBg: 'linear-gradient(135deg,#333,#111)', svgPath: 'M12 2C6.477 2 2 6.477 2 12c0 4.42 2.87 8.17 6.84 9.5.5.08.66-.23.66-.5v-1.69c-2.77.6-3.36-1.34-3.36-1.34-.46-1.16-1.11-1.47-1.11-1.47-.91-.62.07-.6.07-.6 1 .07 1.53 1.03 1.53 1.03.87 1.52 2.34 1.07 2.91.83.09-.65.35-1.09.63-1.34-2.22-.25-4.55-1.11-4.55-4.92 0-1.11.38-2 1.03-2.71-.1-.25-.45-1.29.1-2.64 0 0 .84-.27 2.75 1.02.79-.22 1.65-.33 2.5-.33.85 0 1.71.11 2.5.33 1.91-1.29 2.75-1.02 2.75-1.02.55 1.35.2 2.39.1 2.64.65.71 1.03 1.6 1.03 2.71 0 3.82-2.34 4.66-4.57 4.91.36.31.69.92.69 1.85V21c0 .27.16.59.67.5C19.14 20.16 22 16.42 22 12A10 10 0 0012 2z' },
          { name: 'Wikipedia', url: 'https://wikipedia.org', iconBg: 'linear-gradient(135deg,#eee,#ccc)', svgPath: 'M14.97 18.95L12 12.52l-2.97 6.43a.5.5 0 01-.91-.01L4.94 9.04a.5.5 0 11.92-.38l3.18 7.72L12 10.04l2.96 6.34 3.18-7.72a.5.5 0 01.92.38l-3.18 9.9a.5.5 0 01-.91.01z', svgFill: '#333' },
          { name: 'RISC-V', url: 'https://riscv.org', iconBg: 'linear-gradient(135deg,#4a9eff,#2563eb)', svgPath: 'M20 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0 14H4V8h16v10zm-2-1h-6v-2h6v2zM7.5 17l-1.41-1.41L8.67 13l-2.59-2.59L7.5 9l4 4-4 4z' }
        ]);
      }
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
