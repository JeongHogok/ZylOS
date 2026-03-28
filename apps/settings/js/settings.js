// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 설정 앱 UI — 시스템 설정 관리 페이지
// 수행범위: 언어 변경, 디스플레이 설정, 시스템 정보, 개발자 옵션
// 의존방향: zylI18n (i18n.js), ZylBridge (bridge.js)
// SOLID: SRP — 설정 UI 렌더링과 설정값 관리만 담당
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── Language display names ─── */
  var LANG_NAMES = { ko: '한국어', en: 'English', ja: '日本語', zh: '中文', es: 'Español' };

  /* ─── i18n helper (delegates to shared zylI18n) ─── */
  function t(key, params) {
    return zylI18n.t(key, params);
  }

  function applyTranslations() {
    zylI18n.applyTranslations();
    document.getElementById('current-lang').textContent = LANG_NAMES[zylI18n.getLocale()];
  }

  /* ─── 네비게이션 ─── */
  var mainMenu = document.getElementById('main-menu');
  var btnBack = document.getElementById('btn-back');
  var headerTitle = document.getElementById('header-title');
  var currentPage = null;

  var PAGE_TITLES = {
    wifi: 'settings.wifi',
    bluetooth: 'settings.bluetooth',
    display: 'settings.display',
    sound: 'settings.sound',
    language: 'settings.language',
    wallpaper: 'settings.wallpaper',
    security: 'settings.security',
    storage: 'settings.storage',
    about: 'settings.about',
  };

  /* 메뉴 항목 클릭 → 서브 페이지 */
  document.querySelectorAll('.setting-item[data-page]').forEach(function (item) {
    item.addEventListener('click', function () {
      var pageId = item.dataset.page;
      var page = document.getElementById('page-' + pageId);
      if (!page) return;

      mainMenu.classList.add('hidden');
      page.classList.remove('hidden');
      btnBack.classList.remove('hidden');
      currentPage = pageId;

      headerTitle.textContent = t(PAGE_TITLES[pageId] || 'settings.' + pageId);
    });
  });

  /* 뒤로가기 */
  btnBack.addEventListener('click', function () {
    if (currentPage) {
      var page = document.getElementById('page-' + currentPage);
      if (page) page.classList.add('hidden');
      mainMenu.classList.remove('hidden');
      btnBack.classList.add('hidden');
      headerTitle.textContent = t('settings.title');
      currentPage = null;
    }
  });

  /* ─── 언어 선택 ─── */
  function updateLangChecks() {
    var locale = zylI18n.getLocale();
    document.querySelectorAll('.lang-option').forEach(function (opt) {
      var check = opt.querySelector('.check-icon');
      if (opt.dataset.lang === locale) {
        check.classList.remove('hidden');
      } else {
        check.classList.add('hidden');
      }
    });
  }

  document.querySelectorAll('.lang-option').forEach(function (opt) {
    opt.addEventListener('click', function () {
      var newLocale = opt.dataset.lang;
      zylI18n.setLocale(newLocale);
      updateLangChecks();
      applyTranslations();

      /* 시스템 전체에 언어 변경 알림 (bridge 사용) */
      ZylBridge.setLocale(newLocale);

      /* 헤더 제목도 업데이트 */
      headerTitle.textContent = t('settings.language');
    });
  });

  /* ─── System Service IPC ─── */
  function requestService(service, method, params) {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    }), '*');
  }

  /* Listen for service responses */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;

      if (msg.service === 'wifi' && msg.method === 'getNetworks' && msg.data) {
        renderWifiNetworks(msg.data);
      } else if (msg.service === 'bluetooth' && msg.method === 'getDevices' && msg.data) {
        renderBtDevices(msg.data);
      } else if (msg.service === 'device' && msg.method === 'getInfo' && msg.data) {
        renderAboutInfo(msg.data);
      } else if (msg.service === 'storage' && msg.method === 'getFormatted' && msg.data) {
        renderStorageInfo(msg.data);
      } else if (msg.service === 'settings' && msg.method === 'get' && msg.data) {
        handleSettingsGet(msg.params, msg.data);
      } else if (msg.service === 'settings' && msg.method === 'update' && msg.data) {
        handleSettingsUpdated(msg.params, msg.data);
      }
    } catch (err) { /* ignore */ }
  });

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
      var status = net.connected ? t('settings.connected') : net.security;
      el.innerHTML =
        '<span class="setting-label">' + net.ssid + '</span>' +
        '<span class="setting-value">' + status + '</span>';
      list.appendChild(el);
    });
  }

  /* ─── Bluetooth Devices Renderer ─── */
  function renderBtDevices(devices) {
    var list = document.getElementById('bt-devices-list');
    if (!list) return;
    list.innerHTML = '';
    if (!devices || devices.length === 0) {
      list.innerHTML = '<div class="setting-item no-tap"><span class="setting-label" style="opacity:0.5">No devices</span></div>';
      return;
    }
    devices.forEach(function (dev) {
      if (!dev.paired) return;
      var el = document.createElement('div');
      el.className = 'setting-item no-tap';
      var status = dev.connected ? t('settings.connected') : 'Paired';
      el.innerHTML =
        '<span class="setting-label">' + dev.name + '</span>' +
        '<span class="setting-value">' + status + '</span>';
      list.appendChild(el);
    });
  }

  /* ─── About Device Renderer ─── */
  function renderAboutInfo(info) {
    var list = document.getElementById('about-info-list');
    if (!list) return;
    list.innerHTML = '';
    var items = [
      { label: t('settings.device_name'), value: info.deviceName },
      { label: t('settings.os_version'),  value: info.osVersion },
      { label: 'SoC',                     value: info.soc },
      { label: 'RAM',                     value: info.ram },
      { label: t('settings.kernel'),      value: info.kernel },
      { label: t('settings.build'),       value: info.build }
    ];
    items.forEach(function (item) {
      var el = document.createElement('div');
      el.className = 'setting-item no-tap';
      el.innerHTML =
        '<span class="setting-label">' + item.label + '</span>' +
        '<span class="setting-value">' + item.value + '</span>';
      list.appendChild(el);
    });
  }

  /* ─── Storage Renderer ─── */
  function renderStorageInfo(data) {
    /* Update main menu summary */
    var summary = document.getElementById('storage-summary');
    if (summary) summary.textContent = data.used + ' / ' + data.total;

    /* Update sub-page */
    var list = document.getElementById('storage-info-list');
    if (!list) return;
    list.innerHTML = '';
    var items = [
      { label: t('settings.storage'), value: data.total },
      { label: '사용 중',             value: data.used },
      { label: '사용 가능',           value: data.available }
    ];
    items.forEach(function (item) {
      var el = document.createElement('div');
      el.className = 'setting-item no-tap';
      el.innerHTML =
        '<span class="setting-label">' + item.label + '</span>' +
        '<span class="setting-value">' + item.value + '</span>';
      list.appendChild(el);
    });
  }

  /* ═══════════════════════════════════════════════════════
     Settings State Handlers
     ═══════════════════════════════════════════════════════ */

  /* Font size cycle map */
  var FONT_SIZES = ['small', 'medium', 'large'];
  var FONT_SIZE_LABELS = { small: '작게', medium: '보통', large: '크게' };

  /* Wallpaper color map for swatches */
  var WALLPAPER_COLORS = {
    'default':          'linear-gradient(135deg, #1a1a2e, #16213e)',
    'gradient-blue':    'linear-gradient(135deg, #0077b6, #00b4d8)',
    'gradient-purple':  'linear-gradient(135deg, #7b2ff7, #c471f5)',
    'gradient-dark':    'linear-gradient(135deg, #0d0d0d, #333333)',
    'gradient-sunset':  'linear-gradient(135deg, #f97316, #ef4444, #ec4899)'
  };

  /* Track current settings state locally for UI */
  var settingsCache = {};

  /* ── Handle settings.get responses ── */
  function handleSettingsGet(params, data) {
    if (!params || !data) return;
    var cat = params.category;
    settingsCache[cat] = data;

    if (cat === 'wifi') {
      applyWifiState(data);
    } else if (cat === 'bluetooth') {
      applyBluetoothState(data);
    } else if (cat === 'display') {
      applyDisplayState(data);
    } else if (cat === 'sound') {
      applySoundState(data);
    } else if (cat === 'security') {
      applySecurityState(data);
    } else if (cat === 'wallpaper') {
      renderWallpaperGrid(data);
    }
  }

  /* ── Handle settings.update responses ── */
  function handleSettingsUpdated(params, data) {
    if (!params || !data) return;
    var cat = params.category;
    settingsCache[cat] = data;

    /* Update main menu status when toggling wifi/bt */
    if (cat === 'wifi') updateMainMenuWifi(data);
    if (cat === 'bluetooth') updateMainMenuBt(data);
  }

  /* ── Send a setting update ── */
  function updateSetting(category, key, value) {
    requestService('settings', 'update', { category: category, key: key, value: value });
  }

  /* ═══ WiFi Wiring ═══ */
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
    if (el) el.textContent = data.enabled ? t('settings.connected') : '꺼짐';
  }

  var wifiToggle = document.getElementById('wifi-toggle');
  if (wifiToggle) {
    wifiToggle.addEventListener('change', function () {
      var enabled = wifiToggle.checked;
      updateSetting('wifi', 'enabled', enabled);
      updateWifiUI(enabled);
    });
  }

  /* ═══ Bluetooth Wiring ═══ */
  function applyBluetoothState(data) {
    var toggle = document.getElementById('bt-toggle');
    if (toggle) toggle.checked = data.enabled;
    updateBtUI(data.enabled);
    updateMainMenuBt(data);
  }

  function updateBtUI(enabled) {
    var devicesList = document.getElementById('bt-devices-list');
    var offMsg = document.getElementById('bt-off-msg');
    if (devicesList) devicesList.classList.toggle('hidden', !enabled);
    if (offMsg) offMsg.classList.toggle('hidden', enabled);
  }

  function updateMainMenuBt(data) {
    var el = document.getElementById('bt-status');
    if (el) el.textContent = data.enabled ? '켜짐' : '꺼짐';
  }

  var btToggle = document.getElementById('bt-toggle');
  if (btToggle) {
    btToggle.addEventListener('change', function () {
      var enabled = btToggle.checked;
      updateSetting('bluetooth', 'enabled', enabled);
      updateBtUI(enabled);
    });
  }

  /* ═══ Display Wiring ═══ */
  function applyDisplayState(data) {
    var brightnessSlider = document.getElementById('brightness-slider');
    var darkToggle = document.getElementById('darkmode-toggle');
    var autoToggle = document.getElementById('autobrightness-toggle');
    var fontVal = document.getElementById('fontsize-value');

    if (brightnessSlider) brightnessSlider.value = data.brightness;
    if (darkToggle) darkToggle.checked = data.darkMode;
    if (autoToggle) autoToggle.checked = data.autoBrightness;
    if (fontVal) fontVal.textContent = FONT_SIZE_LABELS[data.fontSize] || '보통';
  }

  var brightnessSlider = document.getElementById('brightness-slider');
  if (brightnessSlider) {
    brightnessSlider.addEventListener('input', function () {
      updateSetting('display', 'brightness', parseInt(brightnessSlider.value, 10));
    });
  }

  var darkToggle = document.getElementById('darkmode-toggle');
  if (darkToggle) {
    darkToggle.addEventListener('change', function () {
      updateSetting('display', 'darkMode', darkToggle.checked);
    });
  }

  var autoToggle = document.getElementById('autobrightness-toggle');
  if (autoToggle) {
    autoToggle.addEventListener('change', function () {
      updateSetting('display', 'autoBrightness', autoToggle.checked);
    });
  }

  var fontsizeItem = document.getElementById('fontsize-item');
  if (fontsizeItem) {
    fontsizeItem.addEventListener('click', function () {
      var current = settingsCache.display ? settingsCache.display.fontSize : 'medium';
      var idx = FONT_SIZES.indexOf(current);
      var next = FONT_SIZES[(idx + 1) % FONT_SIZES.length];
      updateSetting('display', 'fontSize', next);
      var fontVal = document.getElementById('fontsize-value');
      if (fontVal) fontVal.textContent = FONT_SIZE_LABELS[next] || '보통';
      /* Update local cache */
      if (settingsCache.display) settingsCache.display.fontSize = next;
    });
  }

  /* ═══ Sound Wiring ═══ */
  function applySoundState(data) {
    var media = document.getElementById('media-volume');
    var notif = document.getElementById('notif-volume');
    var alarm = document.getElementById('alarm-volume');
    var vibration = document.getElementById('vibration-toggle');

    if (media) media.value = data.mediaVolume;
    if (notif) notif.value = data.notifVolume;
    if (alarm) alarm.value = data.alarmVolume;
    if (vibration) vibration.checked = data.vibration;
  }

  var mediaVol = document.getElementById('media-volume');
  if (mediaVol) {
    mediaVol.addEventListener('input', function () {
      updateSetting('sound', 'mediaVolume', parseInt(mediaVol.value, 10));
    });
  }

  var notifVol = document.getElementById('notif-volume');
  if (notifVol) {
    notifVol.addEventListener('input', function () {
      updateSetting('sound', 'notifVolume', parseInt(notifVol.value, 10));
    });
  }

  var alarmVol = document.getElementById('alarm-volume');
  if (alarmVol) {
    alarmVol.addEventListener('input', function () {
      updateSetting('sound', 'alarmVolume', parseInt(alarmVol.value, 10));
    });
  }

  var vibrationToggle = document.getElementById('vibration-toggle');
  if (vibrationToggle) {
    vibrationToggle.addEventListener('change', function () {
      updateSetting('sound', 'vibration', vibrationToggle.checked);
    });
  }

  /* ═══ Security Wiring ═══ */
  function applySecurityState(data) {
    var lockVal = document.getElementById('lock-type-value');
    var fpToggle = document.getElementById('fingerprint-toggle');
    if (lockVal) lockVal.textContent = data.lockType;
    if (fpToggle) fpToggle.checked = data.fingerprint;
  }

  /* Lock type item → toggle PIN change dialog */
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

  /* PIN change button */
  var pinChangeBtn = document.getElementById('pin-change-btn');
  if (pinChangeBtn) {
    pinChangeBtn.addEventListener('click', function () {
      var curInput = document.getElementById('current-pin-input');
      var newInput = document.getElementById('new-pin-input');
      var msgEl = document.getElementById('pin-change-msg');
      var currentPin = curInput ? curInput.value : '';
      var newPin = newInput ? newInput.value : '';

      /* Validate */
      var storedPin = settingsCache.security ? settingsCache.security.pin : '0000';
      if (currentPin !== storedPin) {
        if (msgEl) { msgEl.textContent = '현재 PIN이 올바르지 않습니다'; msgEl.style.color = '#ef4444'; }
        return;
      }
      if (newPin.length !== 4 || !/^\d{4}$/.test(newPin)) {
        if (msgEl) { msgEl.textContent = 'PIN은 숫자 4자리여야 합니다'; msgEl.style.color = '#ef4444'; }
        return;
      }

      updateSetting('security', 'pin', newPin);
      if (settingsCache.security) settingsCache.security.pin = newPin;

      /* Broadcast to lockscreen via parent */
      window.parent.postMessage(JSON.stringify({
        type: 'settings.pinChanged',
        pin: newPin
      }), '*');

      if (msgEl) { msgEl.textContent = 'PIN이 변경되었습니다'; msgEl.style.color = '#22c55e'; }
      if (curInput) curInput.value = '';
      if (newInput) newInput.value = '';

      /* Auto-hide dialog after success */
      setTimeout(function () {
        if (pinDialog) pinDialog.classList.add('hidden');
        if (msgEl) msgEl.textContent = '';
      }, 1500);
    });
  }

  /* Fingerprint toggle */
  var fpToggle = document.getElementById('fingerprint-toggle');
  if (fpToggle) {
    fpToggle.addEventListener('change', function () {
      updateSetting('security', 'fingerprint', fpToggle.checked);
    });
  }

  /* ═══ Wallpaper Wiring ═══ */
  function renderWallpaperGrid(data) {
    var grid = document.getElementById('wallpaper-grid');
    if (!grid || !data) return;
    grid.innerHTML = '';

    var options = data.options || [];
    var current = data.current || 'default';

    options.forEach(function (opt) {
      var swatch = document.createElement('div');
      swatch.className = 'wallpaper-swatch' + (opt === current ? ' selected' : '');
      swatch.style.cssText = 'width:100%;aspect-ratio:9/16;border-radius:12px;cursor:pointer;border:3px solid ' +
        (opt === current ? '#4a9eff' : 'transparent') +
        ';background:' + (WALLPAPER_COLORS[opt] || '#333') +
        ';display:flex;align-items:center;justify-content:center;font-size:11px;color:rgba(255,255,255,0.7);transition:border-color 0.2s;';
      swatch.textContent = opt === 'default' ? 'Default' : opt.replace('gradient-', '').charAt(0).toUpperCase() + opt.replace('gradient-', '').slice(1);
      swatch.dataset.wallpaper = opt;

      swatch.addEventListener('click', function () {
        updateSetting('wallpaper', 'current', opt);
        /* Update UI selection */
        grid.querySelectorAll('.wallpaper-swatch').forEach(function (s) {
          s.style.borderColor = 'transparent';
          s.classList.remove('selected');
        });
        swatch.style.borderColor = '#4a9eff';
        swatch.classList.add('selected');

        /* Broadcast wallpaper change to home screen via parent */
        window.parent.postMessage(JSON.stringify({
          type: 'settings.wallpaperChanged',
          wallpaper: opt
        }), '*');

        if (settingsCache.wallpaper) settingsCache.wallpaper.current = opt;
      });

      grid.appendChild(swatch);
    });
  }

  /* ═══ Request all service data on init ═══ */
  requestService('wifi', 'getNetworks');
  requestService('bluetooth', 'getDevices');
  requestService('device', 'getInfo');
  requestService('storage', 'getFormatted');

  /* Request settings state for all categories */
  requestService('settings', 'get', { category: 'wifi' });
  requestService('settings', 'get', { category: 'bluetooth' });
  requestService('settings', 'get', { category: 'display' });
  requestService('settings', 'get', { category: 'sound' });
  requestService('settings', 'get', { category: 'security' });
  requestService('settings', 'get', { category: 'wallpaper' });

  /* ─── 초기화 ─── */
  applyTranslations();
  updateLangChecks();

})();
