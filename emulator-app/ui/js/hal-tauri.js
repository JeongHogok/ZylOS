// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Adapter
//
// 역할: Tauri invoke() 기반 HAL 구현
// 수행범위: 배터리 (상태 표시줄)
// 의존방향: Tauri IPC (window.__TAURI__.core.invoke)
// SOLID: LSP — ZylHalBrowser와 동일 인터페이스, DIP — Tauri 백엔드에 의존
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
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

  return {
    battery: battery
  };
})();
