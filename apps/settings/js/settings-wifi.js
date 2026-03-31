// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - WiFi Domain Module
//
// 역할: 설정 앱 WiFi 도메인 — WiFi 토글, 네트워크 목록, 스캔
// 수행범위: WiFi 상태 렌더링, 네트워크 목록 표시, 스캔 버튼
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — WiFi 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  /* ─── WiFi Networks Renderer ─── */
  function renderWifiNetworks(networks) {
    var list = document.getElementById('wifi-networks-list');
    if (!list) return;
    list.innerHTML = '';
    if (!networks || networks.length === 0) {
      list.innerHTML = '<div class="setting-item no-tap"><span class="setting-label" style="opacity:0.5">No networks found</span></div>';
      return;
    }
    networks.forEach(function (net) {
      var el = document.createElement('div');
      el.className = 'setting-item no-tap';
      var status = net.connected ? core.t('settings.connected') : net.security;
      el.innerHTML =
        '<span class="setting-label">' + net.ssid + '</span>' +
        '<span class="setting-value">' + status + '</span>';
      list.appendChild(el);
    });
  }

  /* ─── WiFi State ─── */
  function applyWifiState(data) {
    var toggle = document.getElementById('wifi-toggle');
    if (toggle) toggle.checked = data.enabled;
    updateWifiUI(data.enabled);
    updateMainMenuWifi(data);
  }

  function updateWifiUI(enabled) {
    var networksList = document.getElementById('wifi-networks-list');
    var offMsg = document.getElementById('wifi-off-msg');
    if (networksList) networksList.classList.toggle('hidden', !enabled);
    if (offMsg) offMsg.classList.toggle('hidden', enabled);
  }

  function updateMainMenuWifi(data) {
    var el = document.getElementById('wifi-status');
    if (el) el.textContent = data.enabled ? core.t('settings.connected') : core.t('settings.off');
  }

  /* ─── WiFi Toggle ─── */
  var wifiToggle = document.getElementById('wifi-toggle');
  if (wifiToggle) {
    wifiToggle.addEventListener('change', function () {
      var enabled = wifiToggle.checked;
      core.updateSetting('wifi', 'enabled', enabled);
      updateWifiUI(enabled);
    });
  }

  /* ─── Scan Button ─── */
  var wifiScanBtn = document.getElementById('wifi-scan-btn');
  if (wifiScanBtn) {
    wifiScanBtn.addEventListener('click', function () {
      wifiScanBtn.classList.add('scanning');
      wifiScanBtn.textContent = core.t('settings.scanning');
      core.requestService('wifi', 'getNetworks');
      setTimeout(function () {
        wifiScanBtn.classList.remove('scanning');
        wifiScanBtn.textContent = core.t('settings.wifi_scan');
      }, 3000);
    });
  }

  /* ─── Register with core ─── */
  core.handlers.wifi = {
    onSettingsGet: applyWifiState,
    onSettingsUpdated: function (data) {
      updateMainMenuWifi(data);
    },
    onServiceResponse: function (method, data) {
      if (method === 'getNetworks' && data) renderWifiNetworks(data);
    }
  };

})();
