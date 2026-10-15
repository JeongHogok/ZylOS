// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Security Domain Module
//
// 역할: 설정 앱 보안 도메인 — 화면잠금, PIN 변경/제거, 지문인식
// 수행범위: 보안 설정 상태 렌더링, PIN 다이얼로그 (설정·검증은 lock 서비스), 지문 토글
// 의존방향: ZylSettingsCore (settings.js)
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
    var fpToggle = document.getElementById('fingerprint-toggle');
    if (fpToggle) fpToggle.checked = !!(data && data.fingerprint);
  }

  /* The lock service holds the PIN; Settings only asks and shows the answer */
  function showLockType(hasPin) {
    var lockVal = document.getElementById('lock-type-value');
    if (lockVal) lockVal.textContent = core.t(hasPin ? 'settings.lock_pin' : 'settings.swipe');
  }
  function refreshLockType() {
    core.requestService('lock', 'getState').then(function (r) {
      showLockType(!!(r && r.hasPin));
    }, function () { /* leave the label as it is */ });
  }
  refreshLockType();

  /* Turns a lock service refusal into the message the user sees */
  function lockMessage(r) {
    if (r && r.reason === 'locked_out') {
      return core.t('settings.pin_locked_out', { s: Math.ceil((r.retryAfterMs || 0) / 1000) });
    }
    if (r && r.reason === 'format') return core.t('settings.pin_format');
    return core.t('settings.wrong_pin');
  }

  function showPinMessage(text, color) {
    var msgEl = document.getElementById('pin-change-msg');
    if (msgEl) { msgEl.textContent = text; msgEl.style.color = color; }
  }

  function finishPinDialog() {
    var curInput = document.getElementById('current-pin-input');
    var newInput = document.getElementById('new-pin-input');
    if (curInput) curInput.value = '';
    if (newInput) newInput.value = '';
    setTimeout(function () {
      if (pinDialog) pinDialog.classList.add('hidden');
      showPinMessage('', '');
    }, PIN_DIALOG_HIDE_DELAY);
  }

  /* ─── Lock Type Item → toggle PIN change dialog ─── */
  var lockTypeItem = document.getElementById('lock-type-item');
  var pinDialog = document.getElementById('pin-change-dialog');
  if (lockTypeItem && pinDialog) {
    lockTypeItem.setAttribute('tabindex', '0');
    lockTypeItem.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); lockTypeItem.click(); }
    });
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
      var newPin = newInput ? newInput.value : '';
      /* The pad and lock screen take four digits */
      if (!/^\d{4}$/.test(newPin)) {
        showPinMessage(core.t('settings.pin_format'), '#ef4444');
        return;
      }
      core.requestService('lock', 'setPin', {
        current: curInput ? curInput.value : '', pin: newPin
      }).then(function (r) {
        if (!r || r.ok !== true) { showPinMessage(lockMessage(r), '#ef4444'); return; }
        showPinMessage(core.t('settings.pin_changed'), '#22c55e');
        showLockType(true);
        finishPinDialog();
      }, function () { showPinMessage(lockMessage(null), '#ef4444'); });
    });
  }

  /* ─── PIN Remove Button ─── */
  var pinRemoveBtn = document.getElementById('pin-remove-btn');
  if (pinRemoveBtn) {
    pinRemoveBtn.addEventListener('click', function () {
      var curInput = document.getElementById('current-pin-input');
      core.requestService('lock', 'getState').then(function (st) {
        if (!st || !st.hasPin) {
          showPinMessage(core.t('settings.no_pin_set'), '#f59e0b');
          return null;
        }
        return core.requestService('lock', 'setPin', {
          current: curInput ? curInput.value : '', pin: ''
        }).then(function (r) {
          if (!r || r.ok !== true) { showPinMessage(lockMessage(r), '#ef4444'); return; }
          showPinMessage(core.t('settings.pin_removed'), '#22c55e');
          showLockType(false);
          finishPinDialog();
        });
      }).then(null, function () { showPinMessage(lockMessage(null), '#ef4444'); });
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
