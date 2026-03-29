// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: Tauri invoke() 기반 HAL 구현
// 수행범위: 배터리, 스토리지, 디스플레이, 오디오, 네트워크, 디바이스 정보
// 의존방향: Tauri IPC (window.__TAURI__.core.invoke)
// SOLID: LSP — ZylHalBrowser와 동일 인터페이스, DIP — Tauri 백엔드에 의존
// ──────────────────────────────────────────────────────────

/* global ZylHalTauri */
/* eslint-disable no-unused-vars */

var ZylHalTauri = (function () {
  'use strict';

  var _invoke = null;

  function getInvoke() {
    if (_invoke) return _invoke;
    if (typeof window !== 'undefined' && window.__TAURI__ && window.__TAURI__.core) {
      _invoke = window.__TAURI__.core.invoke;
    }
    return _invoke;
  }

  function invoke(cmd, args) {
    var fn = getInvoke();
    if (!fn) {
      return Promise.reject('Tauri not available');
    }
    return fn(cmd, args || {});
  }

  // ── Battery ──
  var battery = {
    _state: null,
    _interval: null,

    init: function () {
      if (battery._interval) return; /* 중복 init 방지 */
      battery.refresh();
      battery._interval = setInterval(function () { battery.refresh(); }, 30000);
    },

    refresh: function () {
      invoke('get_battery_state').then(function (s) {
        battery._state = s;
      }).catch(function () {
        battery._state = { level: 100, charging: true, health: 'Good', temperature: 25.0, voltage: 4200 };
      });
    },

    getState: function () {
      return battery._state || { level: 100, charging: true, health: 'Good', temperature: 25.0, voltage: 4200 };
    },

    onChange: function () {
      // Tauri에서는 폴링으로 처리 (이벤트 미지원)
    }
  };

  // ── Network ──
  var network = {
    _cache: null,
    _cacheTime: 0,

    getState: function () {
      return {
        online: navigator.onLine,
        type: 'wifi',
        effectiveType: '4g'
      };
    },

    getWifiInfo: function () {
      /* Tauri: 호스트의 실제 WiFi 네트워크 스캔 */
      var now = Date.now();
      if (network._cache && (now - network._cacheTime) < 10000) {
        return { online: navigator.onLine, networks: network._cache };
      }

      invoke('get_wifi_networks').then(function (nets) {
        network._cache = nets || [];
        network._cacheTime = Date.now();
      }).catch(function () {
        network._cache = [];
      });

      return {
        online: navigator.onLine,
        networks: network._cache || []
      };
    }
  };

  // ── Bluetooth ──
  var bluetooth = {
    _cache: null,

    isSupported: function () { return true; },
    getState: function () { return { enabled: true, scanning: false }; },
    getDevices: function () {
      /* Tauri: 호스트의 실제 BT 디바이스 조회 */
      invoke('get_bluetooth_devices').then(function (devs) {
        bluetooth._cache = (devs || []).map(function (d) {
          return {
            name: d.name,
            type: d.device_type || 'unknown',
            paired: d.paired,
            connected: d.connected,
            battery: -1
          };
        });
      }).catch(function () {
        bluetooth._cache = [];
      });

      return bluetooth._cache || [];
    }
  };

  // ── Storage ──
  var storage = {
    _cache: null,
    _cacheTime: 0,

    getUsage: function () {
      var now = Date.now();
      if (storage._cache && (now - storage._cacheTime) < 30000) {
        return storage._cache;
      }

      invoke('fs_get_usage').then(function (data) {
        storage._cache = data;
        storage._cacheTime = Date.now();
      }).catch(function () {
        storage._cache = { total: 0, used: 0, available: 0, percent: 0 };
      });

      return storage._cache || { total: 0, used: 0, available: 0, percent: 0 };
    },

    getFormatted: function () {
      var u = storage.getUsage();
      return {
        total: formatBytes(u.total),
        used: formatBytes(u.used),
        available: formatBytes(u.available),
        percent: Math.round(u.percent || 0)
      };
    },

    prefetch: function () {
      storage.getUsage();
    }
  };

  // ── Display ──
  var display = {
    _state: { brightness: 80, autoBrightness: false, darkMode: true, fontSize: 'medium', screenTimeout: 30 },

    getState: function () { return display._state; },
    setBrightness: function (v) { display._state.brightness = v; },
    setDarkMode: function (v) { display._state.darkMode = v; },
    setFontSize: function (v) { display._state.fontSize = v; }
  };

  // ── Audio ──
  var audio = {
    _state: { mediaVolume: 70, notifVolume: 80, alarmVolume: 90, callVolume: 80, vibration: true, silentMode: false },

    getState: function () { return audio._state; },
    setVolume: function (stream, val) { audio._state[stream + 'Volume'] = val; },
    setSilent: function (v) { audio._state.silentMode = v; }
  };

  // ── Device Info ──
  var deviceInfo = {
    _info: {
      deviceName: 'Zyl OS Emulator',
      osVersion: '0.1.0',
      soc: 'Host CPU',
      ram: 'Configured',
      kernel: 'Emulated',
      build: 'tauri-dev',
      hostname: 'zylos',
      username: 'user'
    },

    getInfo: function () { return deviceInfo._info; },

    applyProfile: function (profile) {
      if (profile) {
        deviceInfo._info.soc = profile.soc || deviceInfo._info.soc;
        deviceInfo._info.ram = profile.ram_mb ? (profile.ram_mb + ' MB') : deviceInfo._info.ram;
        deviceInfo._info.deviceName = profile.name || deviceInfo._info.deviceName;
      }
    }
  };

  // ── Helpers ──
  function formatBytes(bytes) {
    if (!bytes || bytes === 0) return '0 B';
    var units = ['B', 'KB', 'MB', 'GB', 'TB'];
    var i = Math.floor(Math.log(bytes) / Math.log(1024));
    return (bytes / Math.pow(1024, i)).toFixed(1) + ' ' + units[i];
  }

  return {
    battery: battery,
    network: network,
    bluetooth: bluetooth,
    storage: storage,
    display: display,
    audio: audio,
    deviceInfo: deviceInfo
  };
})();
