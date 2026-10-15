// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Bluetooth Domain Module
//
// 역할: 설정 앱 블루투스 도메인 — BT 토글, 디바이스 목록, 스캔
// 수행범위: 블루투스 상태 렌더링, 페어링된 기기 표시, 스캔 버튼
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — 블루투스 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  /* ─── Bluetooth Devices Renderer ─── */
  function renderBtDevices(devices) {
    var list = document.getElementById('bt-devices-list');
    if (!list) return;
    list.innerHTML = '';
    var paired = (devices || []).filter(function (dev) { return dev.paired; });
    /* Device names are whatever a nearby device broadcasts: text only, never markup */
    function row(name, status, dim) {
      var el = document.createElement('div');
      el.className = 'setting-item no-tap';
      var label = document.createElement('span');
      label.className = 'setting-label';
      if (dim) label.style.opacity = '0.5';
      label.textContent = name;
      el.appendChild(label);
      if (status) {
        var value = document.createElement('span');
        value.className = 'setting-value';
        value.textContent = status;
        el.appendChild(value);
      }
      list.appendChild(el);
    }
    if (paired.length === 0) {
      row(core.t('settings.bt_none'), '', true);
      return;
    }
    paired.forEach(function (dev) {
      row(dev.name || dev.address, dev.connected ? core.t('settings.connected') : core.t('settings.paired'));
    });
  }

  /* ─── Bluetooth State — the adapter is the bluetooth service's, not a setting ─── */
  var btToggle = document.getElementById('bt-toggle');
  var btScanBtn = document.getElementById('bt-scan-btn');

  function updateBtUI(enabled) {
    var devicesList = document.getElementById('bt-devices-list');
    var offMsg = document.getElementById('bt-off-msg');
    if (devicesList) devicesList.classList.toggle('hidden', !enabled);
    if (offMsg) offMsg.classList.toggle('hidden', enabled);
  }

  function updateMainMenuBt(st) {
    var el = document.getElementById('bt-status');
    if (el) el.textContent = core.t(!st ? 'settings.unavailable' : st.enabled ? 'settings.on' : 'settings.off');
  }

  function refreshState() {
    core.requestService('bluetooth', 'getState').then(function (st) {
      if (!st) return;
      if (btToggle) btToggle.checked = !!st.enabled;
      updateBtUI(!!st.enabled);
      updateMainMenuBt(st);
    }, function () {});
  }

  if (btToggle) {
    btToggle.addEventListener('change', function () {
      var enabled = btToggle.checked;
      updateBtUI(enabled);
      core.requestService('bluetooth', 'setEnabled', { enabled: enabled }).then(function (ok) {
        if (!ok) throw new Error('not applied');
        refreshState();
      }).catch(function () {
        btToggle.checked = !enabled;
        updateBtUI(!enabled);
        if (typeof ZylToast !== 'undefined') ZylToast.error(core.t('settings.bt_toggle_error'));
      });
    });
  }

  /* ─── Scan Button ─── */
  if (btScanBtn) {
    btScanBtn.addEventListener('click', function () {
      btScanBtn.classList.add('scanning');
      btScanBtn.textContent = core.t('settings.scanning');
      core.requestService('bluetooth', 'getDevices').then(function (data) {
        if (data) renderBtDevices(data);
      }).catch(function () {
        if (typeof ZylToast !== 'undefined') ZylToast.error(core.t('settings.bt_scan_error'));
      }).then(function () {
        btScanBtn.classList.remove('scanning');
        btScanBtn.textContent = core.t('settings.bt_scan');
      });
    });
  }

  /* No adapter on this device: the controls stay, disabled */
  core.getCapabilities('bluetooth').then(function (caps) {
    if (caps.present) { refreshState(); return; }
    core.disableFor(btToggle, 'no-bluetooth');
    core.disableFor(btScanBtn, null);
    updateBtUI(false);
    updateMainMenuBt(null);
  });

  /* ─── Register with core ─── */
  core.handlers.bluetooth = {
    onServiceResponse: function (method, data) {
      if (method === 'getDevices' && data) renderBtDevices(data);
    }
  };

})();
