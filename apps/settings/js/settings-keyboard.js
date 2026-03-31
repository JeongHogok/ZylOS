// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Keyboard Domain Module
//
// 역할: 설정 앱 키보드 도메인 — 입력 언어, 키 높이, 소리/진동/자동대문자
// 수행범위: 키보드 설정 상태 렌더링 및 사용자 입력 처리
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — 키보드 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  var LANG_LABELS = {
    en: 'English',
    ko: '\uD55C\uAD6D\uC5B4',
    ja: '\u65E5\u672C\u8A9E',
    zh: '\u4E2D\u6587',
    es: 'Espa\u00F1ol'
  };

  /* ─── Apply Keyboard State ─── */
  function applyKeyboardState(data) {
    /* Set language checkboxes */
    var langs = (data.languages || 'en').split(',');
    document.querySelectorAll('.kb-lang-check').forEach(function (cb) {
      cb.checked = langs.indexOf(cb.value) !== -1;
    });
    /* Key height */
    var heightSlider = document.getElementById('key-height-slider');
    if (heightSlider && data.keyHeight) heightSlider.value = data.keyHeight;
    /* Toggles */
    var soundT = document.getElementById('kb-sound-toggle');
    var vibT = document.getElementById('kb-vibration-toggle');
    var capT = document.getElementById('kb-autocap-toggle');
    if (soundT) soundT.checked = data.soundEnabled !== false;
    if (vibT) vibT.checked = data.vibrationEnabled !== false;
    if (capT) capT.checked = data.autoCapitalize !== false;
    /* Update main menu status */
    var statusEl = document.getElementById('keyboard-status');
    if (statusEl) {
      statusEl.textContent = langs.map(function (l) { return LANG_LABELS[l] || l; }).join(', ');
    }
  }

  /* ─── Language Toggles ─── */
  document.querySelectorAll('.kb-lang-check').forEach(function (cb) {
    cb.addEventListener('change', function () {
      var enabled = [];
      document.querySelectorAll('.kb-lang-check:checked').forEach(function (c) { enabled.push(c.value); });
      if (enabled.length === 0) { cb.checked = true; return; } /* at least 1 */
      core.updateSetting('keyboard', 'languages', enabled.join(','));
      /* Update main menu status */
      var statusEl = document.getElementById('keyboard-status');
      if (statusEl) statusEl.textContent = enabled.map(function (l) { return LANG_LABELS[l] || l; }).join(', ');
    });
  });

  /* ─── Key Height Slider ─── */
  var keyHeightSlider = document.getElementById('key-height-slider');
  if (keyHeightSlider) {
    keyHeightSlider.addEventListener('input', function () {
      core.updateSetting('keyboard', 'keyHeight', parseInt(keyHeightSlider.value, 10));
    });
  }

  /* ─── Sound Toggle ─── */
  var kbSoundToggle = document.getElementById('kb-sound-toggle');
  if (kbSoundToggle) {
    kbSoundToggle.addEventListener('change', function () {
      core.updateSetting('keyboard', 'soundEnabled', kbSoundToggle.checked);
    });
  }

  /* ─── Vibration Toggle ─── */
  var kbVibToggle = document.getElementById('kb-vibration-toggle');
  if (kbVibToggle) {
    kbVibToggle.addEventListener('change', function () {
      core.updateSetting('keyboard', 'vibrationEnabled', kbVibToggle.checked);
    });
  }

  /* ─── Auto-capitalize Toggle ─── */
  var kbAutoCapToggle = document.getElementById('kb-autocap-toggle');
  if (kbAutoCapToggle) {
    kbAutoCapToggle.addEventListener('change', function () {
      core.updateSetting('keyboard', 'autoCapitalize', kbAutoCapToggle.checked);
    });
  }

  /* ─── Register with core ─── */
  core.handlers.keyboard = {
    onSettingsGet: applyKeyboardState,
    onSettingsUpdated: null
  };

})();
