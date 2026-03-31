// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Security Domain Module
//
// 역할: 설정 앱 보안 도메인 — 화면잠금, PIN 변경/제거, 지문인식
// 수행범위: 보안 설정 상태 렌더링, PIN 다이얼로그, 지문 토글
// 의존방향: ZylSettingsCore (settings.js), ZylBridge (bridge.js)
// SOLID: SRP — 보안 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;
  var PIN_DIALOG_HIDE_DELAY = 1500;

  /* ─── Apply Security State ─── */
  function applySecurityState(data) {
    var lockVal = document.getElementById('lock-type-value');
    var fpToggle = document.getElementById('fingerprint-toggle');
    if (lockVal) lockVal.textContent = data.lockType;
    if (fpToggle) fpToggle.checked = data.fingerprint;
  }

  /* ─── Lock Type Item → toggle PIN change dialog ─── */
  var lockTypeItem = document.getElementById('lock-type-item');
  var pinDialog = document.getElementById('pin-change-dialog');
  if (lockTypeItem && pinDialog) {
    lockTypeItem.addEventListener('click', function () {
      pinDialog.classList.toggle('hidden');
      /* Clear inputs on open */
      var curInput = document.getElementById('current-pin-input');
      var newInput = document.getElementById('new-pin-input');
      var msgEl = document.getElementById('pin-change-msg');
      if (curInput) curInput.value = '';
      if (newInput) newInput.value = '';
      if (msgEl) { msgEl.textContent = ''; msgEl.style.color = ''; }
    });
  }

  /* ─── PIN Change Button ─── */
  var pinChangeBtn = document.getElementById('pin-change-btn');
  if (pinChangeBtn) {
    pinChangeBtn.addEventListener('click', function () {
      var curInput = document.getElementById('current-pin-input');
      var newInput = document.getElementById('new-pin-input');
      var msgEl = document.getElementById('pin-change-msg');
      var currentPin = curInput ? curInput.value : '';
      var newPin = newInput ? newInput.value : '';

      /* Validate */
      var storedPin = core.settingsCache.security ? core.settingsCache.security.pin : '0000';
      if (currentPin !== storedPin) {
        if (msgEl) { msgEl.textContent = core.t('settings.wrong_pin'); msgEl.style.color = '#ef4444'; }
        return;
      }
      if (newPin.length !== 4 || !/^\d{4}$/.test(newPin)) {
        if (msgEl) { msgEl.textContent = core.t('settings.pin_format') || 'PIN must be 4 digits'; msgEl.style.color = '#ef4444'; }
        return;
      }

      core.updateSetting('security', 'pin', newPin);
      if (core.settingsCache.security) core.settingsCache.security.pin = newPin;

      /* Broadcast to lockscreen via parent */
      ZylBridge.sendToSystem({
        type: 'settings.pinChanged',
        data: { pin: newPin }
      });

      if (msgEl) { msgEl.textContent = core.t('settings.pin_changed') || 'PIN changed'; msgEl.style.color = '#22c55e'; }
      if (curInput) curInput.value = '';
      if (newInput) newInput.value = '';

      /* Auto-hide dialog after success */
      setTimeout(function () {
        if (pinDialog) pinDialog.classList.add('hidden');
        if (msgEl) msgEl.textContent = '';
      }, PIN_DIALOG_HIDE_DELAY);
    });
  }

  /* ─── PIN Remove Button ─── */
  var pinRemoveBtn = document.getElementById('pin-remove-btn');
  if (pinRemoveBtn) {
    pinRemoveBtn.addEventListener('click', function () {
      var curInput = document.getElementById('current-pin-input');
      var msgEl = document.getElementById('pin-change-msg');
      var currentPin = curInput ? curInput.value : '';
      var storedPin = core.settingsCache.security ? core.settingsCache.security.pin : '';

      /* Must verify current PIN to remove */
      if (!storedPin) {
        if (msgEl) { msgEl.textContent = core.t('settings.no_pin_set'); msgEl.style.color = '#f59e0b'; }
        return;
      }
      if (currentPin !== storedPin) {
        if (msgEl) { msgEl.textContent = core.t('settings.wrong_pin'); msgEl.style.color = '#ef4444'; }
        return;
      }

      /* Remove PIN — set to empty string */
      core.updateSetting('security', 'pin', '');
      if (core.settingsCache.security) core.settingsCache.security.pin = '';

      ZylBridge.sendToSystem({
        type: 'settings.pinChanged',
        data: { pin: '' }
      });

      if (msgEl) { msgEl.textContent = core.t('settings.pin_removed') || 'PIN removed'; msgEl.style.color = '#22c55e'; }
      if (curInput) curInput.value = '';
      var lockVal = document.getElementById('lock-type-value');
      if (lockVal) lockVal.textContent = core.t('settings.swipe');

      setTimeout(function () {
        if (pinDialog) pinDialog.classList.add('hidden');
        if (msgEl) msgEl.textContent = '';
      }, PIN_DIALOG_HIDE_DELAY);
    });
  }

  /* ─── Fingerprint Toggle ─── */
  var fpToggle = document.getElementById('fingerprint-toggle');
  if (fpToggle) {
    fpToggle.addEventListener('change', function () {
      core.updateSetting('security', 'fingerprint', fpToggle.checked);
    });
  }

  /* ─── Register with core ─── */
  core.handlers.security = {
    onSettingsGet: applySecurityState,
    onSettingsUpdated: null
  };

})();
