// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 브라우저 Web API 기반 HAL 구현
// 수행범위: 호스트 기기의 실제 WiFi/Battery/Storage 정보를 브라우저 API로 조회
// 의존방향: 브라우저 Web API (navigator.connection, getBattery, storage)
// SOLID: DIP — HAL 인터페이스의 브라우저 구현. 실기기에서는 Linux HAL로 교체.
// ──────────────────────────────────────────────────────────

var ZylHalBrowser = (function () {
  'use strict';

  /* ═══════════════════════════════════════════════════════
     Battery — navigator.getBattery()
     실제 호스트 기기의 배터리 정보를 가져옴
     ═══════════════════════════════════════════════════════ */
  var battery = {
    _battery: null,
    _callbacks: [],

    init: function () {
      var self = this;
      if (navigator.getBattery) {
        navigator.getBattery().then(function (b) {
          self._battery = b;
          b.addEventListener('levelchange', function () { self._notifyChange(); });
          b.addEventListener('chargingchange', function () { self._notifyChange(); });
        });
      }
    },

    getState: function () {
      if (!this._battery) {
        return { level: -1, charging: false, health: 'Unknown', temperature: 0, voltage: 0 };
      }
      return {
        level: Math.round(this._battery.level * 100),
        charging: this._battery.charging,
        health: 'Good',
        temperature: 285,  /* 센서 미지원 — 기본값 */
        voltage: 3800,
      };
    },

    onChange: function (cb) {
      this._callbacks.push(cb);
    },

    _notifyChange: function () {
      var state = this.getState();
      this._callbacks.forEach(function (cb) { cb(state); });
    },
  };

  /* ═══════════════════════════════════════════════════════
     Network — navigator.connection (NetworkInformation API)
     호스트의 실제 네트워크 상태를 가져옴
     ═══════════════════════════════════════════════════════ */
  var network = {
    getState: function () {
      var conn = navigator.connection || navigator.mozConnection || navigator.webkitConnection;
      var online = navigator.onLine;

      if (!conn) {
        return {
          online: online,
          type: online ? 'unknown' : 'none',
          effectiveType: 'unknown',
          downlinkMbps: -1,
          rtt: -1,
        };
      }

      return {
        online: online,
        type: conn.type || (online ? 'wifi' : 'none'),
        effectiveType: conn.effectiveType || 'unknown',
        downlinkMbps: conn.downlink || -1,
        rtt: conn.rtt || -1,
      };
    },

    /* WiFi 스캔은 브라우저에서 불가 — 실제 연결 정보만 제공 */
    getWifiInfo: function () {
      var state = this.getState();
      var result = {
        enabled: true,
        connected: state.online,
        networks: [],
      };

      /* 연결된 네트워크 정보 (실제 호스트 기반) */
      if (state.online) {
        result.networks.push({
          ssid: 'Host Network',
          security: state.type === 'wifi' ? 'WPA2' : state.type,
          signal: 85,
          connected: true,
          frequency: state.effectiveType === '4g' ? 5180 : 2437,
        });
      }

      return result;
    },
  };

  /* ═══════════════════════════════════════════════════════
     Bluetooth — Web Bluetooth API (navigator.bluetooth)
     매우 제한적: 페어링된 디바이스 조회 불가, 스캔만 가능
     ═══════════════════════════════════════════════════════ */
  var bluetooth = {
    isSupported: function () {
      return !!(navigator.bluetooth);
    },

    getState: function () {
      return {
        enabled: this.isSupported(),
        supported: this.isSupported(),
        /* Web Bluetooth은 사용자 제스처 없이 디바이스 열거 불가 */
        devices: [],
      };
    },

    /* 실제 스캔은 사용자 클릭 핸들러 내에서만 가능 */
    requestDevice: function () {
      if (!navigator.bluetooth) return Promise.reject('BT not supported');
      return navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
      }).then(function (device) {
        return { name: device.name || 'Unknown', id: device.id, connected: device.gatt.connected };
      });
    },
  };

  /* ═══════════════════════════════════════════════════════
     Storage — navigator.storage.estimate()
     호스트 브라우저의 실제 스토리지 사용량
     ═══════════════════════════════════════════════════════ */
  var storage = {
    getState: function () {
      if (navigator.storage && navigator.storage.estimate) {
        return navigator.storage.estimate().then(function (est) {
          return {
            total: est.quota || 0,
            used: est.usage || 0,
            available: (est.quota || 0) - (est.usage || 0),
          };
        });
      }
      return Promise.resolve({ total: 0, used: 0, available: 0 });
    },

    /* 바이트 → 사람이 읽을 수 있는 형식 */
    formatBytes: function (bytes) {
      if (bytes === 0) return '0 B';
      var k = 1024;
      var sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
      var i = Math.floor(Math.log(bytes) / Math.log(k));
      return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
    },
  };

  /* ═══════════════════════════════════════════════════════
     Display — CSS 기반 (에뮬레이터 전용)
     실기기에서는 sysfs backlight 사용
     ═══════════════════════════════════════════════════════ */
  var display = {
    _state: {
      brightness: 80,
      autoBrightness: true,
      darkMode: true,
      fontSize: 'medium',
      screenTimeout: 30,
    },

    getState: function () {
      /* 호스트의 prefers-color-scheme 감지 */
      if (window.matchMedia) {
        this._state.darkMode = window.matchMedia('(prefers-color-scheme: dark)').matches;
      }
      return Object.assign({}, this._state);
    },

    setBrightness: function (percent) {
      this._state.brightness = Math.max(10, Math.min(100, percent));
      /* 에뮬레이터에서는 CSS filter로 시뮬레이션 */
      return this._state.brightness;
    },

    setDarkMode: function (enabled) {
      this._state.darkMode = enabled;
      return enabled;
    },

    setFontSize: function (size) {
      this._state.fontSize = size; /* 'small', 'medium', 'large' */
      return size;
    },
  };

  /* ═══════════════════════════════════════════════════════
     Audio — Web Audio API 기반 상태 관리
     실기기에서는 PipeWire/ALSA
     ═══════════════════════════════════════════════════════ */
  var audio = {
    _state: {
      mediaVolume: 70,
      notifVolume: 80,
      alarmVolume: 90,
      callVolume: 70,
      vibration: true,
      silentMode: false,
    },

    getState: function () {
      return Object.assign({}, this._state);
    },

    setVolume: function (stream, percent) {
      percent = Math.max(0, Math.min(100, percent));
      switch (stream) {
        case 'media': this._state.mediaVolume = percent; break;
        case 'notification': this._state.notifVolume = percent; break;
        case 'alarm': this._state.alarmVolume = percent; break;
        case 'call': this._state.callVolume = percent; break;
      }
      return percent;
    },

    setVibration: function (enabled) {
      this._state.vibration = enabled;
      /* 호스트에서 진동 API 사용 가능한 경우 */
      if (enabled && navigator.vibrate) {
        navigator.vibrate(50); /* 피드백 진동 */
      }
      return enabled;
    },

    setSilentMode: function (enabled) {
      this._state.silentMode = enabled;
      return enabled;
    },
  };

  /* ═══════════════════════════════════════════════════════
     Device Info — 에뮬레이터 프로필 + 호스트 브라우저 정보
     ═══════════════════════════════════════════════════════ */
  var deviceInfo = {
    _profile: null,

    applyProfile: function (profile) {
      this._profile = profile;
    },

    getInfo: function () {
      var p = this._profile || {};
      return {
        deviceName: p.name || 'Zyl Phone',
        osVersion: 'Zyl OS 0.1.0',
        soc: p.soc || 'SpacemiT K1 (RISC-V)',
        ram: p.ram || '16GB',
        screen: p.screen || '1080x2400',
        kernel: 'Linux 6.6.63',
        build: 'ZylOS.' + new Date().toISOString().slice(0, 10).replace(/-/g, '') + '.dev',
        navMode: p.navMode || 'gesture',
        /* 호스트 브라우저 정보 */
        hostUserAgent: navigator.userAgent,
        hostPlatform: navigator.platform,
        hostLanguage: navigator.language,
      };
    },
  };

  /* ═══ 초기화 ═══ */
  battery.init();

  /* ═══ Public API ═══ */
  return {
    battery: battery,
    network: network,
    bluetooth: bluetooth,
    storage: storage,
    display: display,
    audio: audio,
    deviceInfo: deviceInfo,
  };
})();
