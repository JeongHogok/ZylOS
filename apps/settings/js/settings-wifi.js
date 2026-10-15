// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - WiFi Domain Module
//
// 역할: 설정 앱 WiFi 도메인 — WiFi 토글, 네트워크 목록, 스캔, 연결
// 수행범위: 무선 켜고 끄기(wifi 서비스), 네트워크 목록(이름은 텍스트로만), 스캔,
//          네트워크를 눌러 연결(보안 네트워크는 암호 입력), 핫스팟, 이 기기에 없는
//          Wi-Fi·AP 모드는 컨트롤을 두고 비활성
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — WiFi 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  var joinBox = document.getElementById('wifi-join');
  var joinSsid = document.getElementById('wifi-join-ssid');
  var joinPass = document.getElementById('wifi-join-pass');
  var joinMsg = document.getElementById('wifi-join-msg');
  var joining = null;                /* the network the password is for */

  function isOpen(net) { return !net.security || net.security === 'Open'; }

  function refreshNetworks() {
    return core.requestService('wifi', 'getNetworks').then(function (data) {
      if (data) renderWifiNetworks(data);
    });
  }

  function connect(net, passphrase) {
    joinMsg.textContent = core.t('settings.wifi_connecting');
    core.requestService('wifi', 'connect', { ssid: net.ssid, passphrase: passphrase })
      .then(function (ok) {
        if (!ok) throw new Error('not connected');
        joining = null;
        joinPass.value = '';
        joinMsg.textContent = '';
        joinBox.classList.add('hidden');
        refreshNetworks();
      })
      .catch(function () {
        joinSsid.textContent = net.ssid;
        joinBox.classList.remove('hidden');
        joinMsg.textContent = core.t(isOpen(net) ? 'settings.wifi_connect_failed_open' : 'settings.wifi_connect_failed');
      });
  }

  function choose(net) {
    if (net.connected) return;
    joining = net;
    joinSsid.textContent = net.ssid;
    joinPass.value = '';
    joinMsg.textContent = '';
    if (isOpen(net)) {
      joinBox.classList.add('hidden');
      connect(net, '');
      return;
    }
    joinBox.classList.remove('hidden');
    joinPass.focus();
  }

  document.getElementById('wifi-join-btn').addEventListener('click', function () {
    if (joining) connect(joining, joinPass.value);
  });

  /* ─── WiFi Networks Renderer ─── */
  /* Names come from whoever runs a nearby access point: text only, never markup */
  function renderWifiNetworks(networks) {
    var list = document.getElementById('wifi-networks-list');
    if (!list) return;
    list.innerHTML = '';
    if (!networks || networks.length === 0) {
      var empty = document.createElement('div');
      empty.className = 'setting-item no-tap';
      var emptyLabel = document.createElement('span');
      emptyLabel.className = 'setting-label';
      emptyLabel.style.opacity = '0.5';
      emptyLabel.textContent = core.t('settings.wifi_none');
      empty.appendChild(emptyLabel);
      list.appendChild(empty);
      return;
    }
    networks.forEach(function (net) {
      var el = document.createElement('div');
      el.className = 'setting-item' + (net.connected ? ' no-tap' : '');
      if (!net.connected) {
        el.setAttribute('role', 'button');
        el.setAttribute('tabindex', '0');
        el.addEventListener('click', function () { choose(net); });
      }
      var label = document.createElement('span');
      label.className = 'setting-label';
      label.textContent = net.ssid;
      var value = document.createElement('span');
      value.className = 'setting-value';
      value.textContent = net.connected ? core.t('settings.connected') : (isOpen(net) ? '' : net.security);
      el.appendChild(label);
      el.appendChild(value);
      list.appendChild(el);
    });
  }

  /* ─── WiFi State — the radio is the wifi service's, not a setting ─── */
  var wifiToggle = document.getElementById('wifi-toggle');
  var wifiScanBtn = document.getElementById('wifi-scan-btn');

  function updateWifiUI(enabled) {
    var networksList = document.getElementById('wifi-networks-list');
    var offMsg = document.getElementById('wifi-off-msg');
    if (networksList) networksList.classList.toggle('hidden', !enabled);
    if (offMsg) offMsg.classList.toggle('hidden', enabled);
    if (!enabled) joinBox.classList.add('hidden');
  }

  function updateMainMenuWifi(st) {
    var el = document.getElementById('wifi-status');
    if (!el) return;
    if (!st) el.textContent = core.t('settings.unavailable');
    else if (st.connected && st.ssid) el.textContent = st.ssid;
    else el.textContent = core.t(st.enabled ? 'settings.on' : 'settings.off');
  }

  function applyWifiState(st) {
    if (wifiToggle) wifiToggle.checked = !!st.enabled;
    updateWifiUI(!!st.enabled);
    updateMainMenuWifi(st);
  }

  function refreshState() {
    core.requestService('wifi', 'getState').then(function (st) { if (st) applyWifiState(st); }, function () {});
  }

  if (wifiToggle) {
    wifiToggle.addEventListener('change', function () {
      var enabled = wifiToggle.checked;
      updateWifiUI(enabled);
      core.requestService('wifi', 'setEnabled', { enabled: enabled }).then(function (ok) {
        if (!ok) throw new Error('not applied');
        refreshState();
        if (enabled) refreshNetworks();
      }).catch(function () {
        wifiToggle.checked = !enabled;
        updateWifiUI(!enabled);
        if (typeof ZylToast !== 'undefined') ZylToast.error(core.t('settings.wifi_toggle_error'));
      });
    });
  }

  /* ─── Scan Button ─── */
  if (wifiScanBtn) {
    wifiScanBtn.addEventListener('click', function () {
      wifiScanBtn.classList.add('scanning');
      wifiScanBtn.textContent = core.t('settings.scanning');
      refreshNetworks().catch(function () {
        if (typeof ZylToast !== 'undefined') ZylToast.error(core.t('settings.wifi_scan_error'));
      }).then(function () {
        wifiScanBtn.classList.remove('scanning');
        wifiScanBtn.textContent = core.t('settings.wifi_scan');
      });
    });
  }

  /* ─── Hotspot ─── */
  var hsToggle = document.getElementById('hotspot-toggle');
  var hsSsid = document.getElementById('hotspot-ssid');
  var hsPass = document.getElementById('hotspot-pass');
  var hsMsg = document.getElementById('hotspot-msg');
  var HOTSPOT_REASONS = {
    passphrase: 'settings.hotspot_passphrase_rule', ssid: 'settings.hotspot_ssid_rule',
    unsupported: 'settings.hotspot_unsupported', failed: 'settings.hotspot_failed'
  };

  function showHotspot(st) {
    if (!st) return;
    hsToggle.checked = !!st.active;
    if (st.ssid) hsSsid.value = st.ssid;
    if (st.passphrase) hsPass.value = st.passphrase;
    hsMsg.textContent = st.ok === false ? core.t(HOTSPOT_REASONS[st.reason] || 'settings.hotspot_failed') : '';
  }

  hsToggle.addEventListener('change', function () {
    var on = hsToggle.checked;
    hsMsg.textContent = '';
    core.requestService('wifi', 'setHotspot', { enabled: on, ssid: hsSsid.value, passphrase: hsPass.value })
      .then(showHotspot, function () { showHotspot({ ok: false, reason: 'failed', active: false }); });
  });

  /* ─── What this device has: a missing radio or AP mode leaves the controls, disabled ─── */
  core.getCapabilities('wifi').then(function (caps) {
    if (!caps.present) {
      core.disableFor(wifiToggle, 'no-wifi');
      core.disableFor(wifiScanBtn, null);
      updateWifiUI(false);
      updateMainMenuWifi(null);
    } else {
      refreshState();
    }
    if (!caps.hotspot) {
      core.disableFor(hsToggle, 'no-hotspot');
      hsSsid.disabled = hsPass.disabled = true;
    } else {
      core.requestService('wifi', 'getHotspot').then(showHotspot, function () {});
    }
  });

  /* ─── Register with core ─── */
  core.handlers.wifi = {
    onServiceResponse: function (method, data) {
      if (method === 'getNetworks' && data) renderWifiNetworks(data);
    }
  };

})();
