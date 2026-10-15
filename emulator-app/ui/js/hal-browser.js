// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: 브라우저 Web API 기반 HAL 구현
// 수행범위: 호스트 기기의 실제 배터리 정보를 브라우저 API로 조회 (상태 표시줄)
// 의존방향: 브라우저 Web API (navigator.getBattery)
// SOLID: DIP — HAL 인터페이스의 브라우저 구현. 실기기에서는 Linux HAL로 교체.
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
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

    _initialized: false,

    init: function () {
      if (this._initialized) return; /* 중복 init 방지 */
      this._initialized = true;
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

  return {
    battery: battery
  };
})();
