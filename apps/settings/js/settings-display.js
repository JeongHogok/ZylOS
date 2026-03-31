// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Display Domain Module
//
// 역할: 설정 앱 디스플레이 도메인 — 밝기, 다크모드, 자동밝기, 글꼴크기
// 수행범위: 디스플레이 설정 상태 렌더링 및 사용자 입력 처리
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — 디스플레이 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  /* Font size cycle map */
  var FONT_SIZES = ['small', 'medium', 'large'];

  function fontSizeLabel(size) {
    var map = {
      small: core.t('settings.font_small'),
      medium: core.t('settings.font_medium'),
      large: core.t('settings.font_large')
    };
    return map[size] || size;
  }

  /* ─── Apply Display State ─── */
  function applyDisplayState(data) {
    var brightnessSlider = document.getElementById('brightness-slider');
    var darkToggle = document.getElementById('darkmode-toggle');
    var autoToggle = document.getElementById('autobrightness-toggle');
    var fontVal = document.getElementById('fontsize-value');

    if (brightnessSlider) brightnessSlider.value = data.brightness;
    if (darkToggle) darkToggle.checked = data.darkMode;
    if (autoToggle) autoToggle.checked = data.autoBrightness;
    if (fontVal) fontVal.textContent = fontSizeLabel(data.fontSize);
  }

  /* ─── Brightness Slider ─── */
  var brightnessSlider = document.getElementById('brightness-slider');
  if (brightnessSlider) {
    brightnessSlider.addEventListener('input', function () {
      var val = parseInt(brightnessSlider.value, 10);
      if (isNaN(val)) return;
      val = Math.max(10, Math.min(100, val));
      core.updateSetting('display', 'brightness', val);
    });
  }

  /* ─── Dark Mode Toggle ─── */
  var darkToggle = document.getElementById('darkmode-toggle');
  if (darkToggle) {
    darkToggle.addEventListener('change', function () {
      core.updateSetting('display', 'darkMode', darkToggle.checked);
    });
  }

  /* ─── Auto Brightness Toggle ─── */
  var autoToggle = document.getElementById('autobrightness-toggle');
  if (autoToggle) {
    autoToggle.addEventListener('change', function () {
      core.updateSetting('display', 'autoBrightness', autoToggle.checked);
    });
  }

  /* ─── Font Size Cycle ─── */
  var fontsizeItem = document.getElementById('fontsize-item');
  if (fontsizeItem) {
    fontsizeItem.addEventListener('click', function () {
      var current = core.settingsCache.display ? core.settingsCache.display.fontSize : 'medium';
      var idx = FONT_SIZES.indexOf(current);
      var next = FONT_SIZES[(idx + 1) % FONT_SIZES.length];
      core.updateSetting('display', 'fontSize', next);
      var fontVal = document.getElementById('fontsize-value');
      if (fontVal) fontVal.textContent = fontSizeLabel(next);
      /* Update local cache */
      if (core.settingsCache.display) core.settingsCache.display.fontSize = next;
    });
  }

  /* ─── Register with core ─── */
  core.handlers.display = {
    onSettingsGet: applyDisplayState,
    onSettingsUpdated: null
  };

})();
