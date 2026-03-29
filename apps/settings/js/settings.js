// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 설정 앱 UI — 시스템 설정 관리 페이지
// 수행범위: 언어 변경, 디스플레이 설정, 시스템 정보, 개발자 옵션
// 의존방향: zylI18n (i18n.js), ZylBridge (bridge.js)
// SOLID: SRP — 설정 UI 렌더링과 설정값 관리만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
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
    keyboard: 'settings.keyboard',
    sound: 'settings.sound',
    language: 'settings.language',
    wallpaper: 'settings.wallpaper',
    security: 'settings.security',
    storage: 'settings.storage',
    about: 'settings.about',
    applications: 'settings.applications',
  };

  /* ─── Navigation stack for multi-level pages ─── */
  var navStack = [];

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

  /* 뒤로가기 (navStack 지원) */
  btnBack.addEventListener('click', function () {
    if (!currentPage) return;

    var curPageEl = document.getElementById('page-' + currentPage);
    if (curPageEl) curPageEl.classList.add('hidden');

    if (navStack.length > 0) {
      /* Pop previous page from stack */
      var prev = navStack.pop();
      var prevPageEl = document.getElementById('page-' + prev);
      if (prevPageEl) prevPageEl.classList.remove('hidden');
      currentPage = prev;
      headerTitle.textContent = t(PAGE_TITLES[prev] || 'settings.' + prev);
    } else {
      /* Back to main menu */
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

  /* Listen for messages from emulator */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      if (!e.data) return;
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* ── Navigation back handling ── */
      if (msg.type === 'navigation.back') {
        if (currentPage) {
          /* Sub-page open → go back (navStack or main menu) */
          btnBack.click();
          window.parent.postMessage(JSON.stringify({ type: 'navigation.handled' }), '*');
        } else {
          /* Already on main menu → exit to home */
          window.parent.postMessage(JSON.stringify({ type: 'navigation.exit' }), '*');
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      if (msg.service === 'wifi' && msg.method === 'getNetworks' && msg.data) {
        renderWifiNetworks(msg.data);
      } else if (msg.service === 'bluetooth' && msg.method === 'getDevices' && msg.data) {
        renderBtDevices(msg.data);
      } else if (msg.service === 'device' && msg.method === 'getInfo' && msg.data) {
        renderAboutInfo(msg.data);
      } else if (msg.service === 'storage' && msg.method === 'getFormatted' && msg.data) {
        renderStorageInfo(msg.data);
      } else if (msg.service === 'apps' && msg.method === 'getInstalled' && msg.data) {
        installedApps = msg.data;
        renderAppList(msg.data);
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
      var status = dev.connected ? t('settings.connected') : (t('settings.paired') || 'Paired');
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
      { label: t('settings.soc') || 'SoC', value: info.soc },
      { label: t('settings.ram') || 'RAM', value: info.ram },
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
      { label: t('settings.storage_used') || 'Used',         value: data.used },
      { label: t('settings.storage_available') || 'Available', value: data.available }
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
  function fontSizeLabel(size) {
    var map = { small: t('settings.font_small'), medium: t('settings.font_medium'), large: t('settings.font_large') };
    return map[size] || size;
  }

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
    } else if (cat === 'keyboard') {
      applyKeyboardState(data);
    } else if (cat === 'wallpaper') {
      renderWallpaperGrid(data);
    } else if (cat === 'app_permissions') {
      appPermissions = data || {};
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
    if (el) el.textContent = data.enabled ? t('settings.connected') : t('settings.off');
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
    if (el) el.textContent = data.enabled ? t('settings.on') : t('settings.off');
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
    if (fontVal) fontVal.textContent = fontSizeLabel(data.fontSize);
  }

  var brightnessSlider = document.getElementById('brightness-slider');
  if (brightnessSlider) {
    brightnessSlider.addEventListener('input', function () {
      var val = parseInt(brightnessSlider.value, 10);
      if (isNaN(val)) return;
      val = Math.max(10, Math.min(100, val));
      updateSetting('display', 'brightness', val);
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
      if (fontVal) fontVal.textContent = fontSizeLabel(next);
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
      var val = parseInt(mediaVol.value, 10);
      if (isNaN(val)) return;
      val = Math.max(0, Math.min(100, val));
      updateSetting('sound', 'mediaVolume', val);
      requestService('audio', 'setVolume', { stream: 'media', value: val });
    });
  }

  var notifVol = document.getElementById('notif-volume');
  if (notifVol) {
    notifVol.addEventListener('input', function () {
      var val = parseInt(notifVol.value, 10);
      if (isNaN(val)) return;
      val = Math.max(0, Math.min(100, val));
      updateSetting('sound', 'notifVolume', val);
      requestService('audio', 'setVolume', { stream: 'notif', value: val });
    });
  }

  var alarmVol = document.getElementById('alarm-volume');
  if (alarmVol) {
    alarmVol.addEventListener('input', function () {
      var val = parseInt(alarmVol.value, 10);
      if (isNaN(val)) return;
      val = Math.max(0, Math.min(100, val));
      updateSetting('sound', 'alarmVolume', val);
      requestService('audio', 'setVolume', { stream: 'alarm', value: val });
    });
  }

  var vibrationToggle = document.getElementById('vibration-toggle');
  if (vibrationToggle) {
    vibrationToggle.addEventListener('change', function () {
      updateSetting('sound', 'vibration', vibrationToggle.checked);
      requestService('audio', 'setVibration', { enabled: vibrationToggle.checked });
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
        if (msgEl) { msgEl.textContent = t('settings.wrong_pin') || 'Incorrect current PIN'; msgEl.style.color = '#ef4444'; }
        return;
      }
      if (newPin.length !== 4 || !/^\d{4}$/.test(newPin)) {
        if (msgEl) { msgEl.textContent = t('settings.pin_format') || 'PIN must be 4 digits'; msgEl.style.color = '#ef4444'; }
        return;
      }

      updateSetting('security', 'pin', newPin);
      if (settingsCache.security) settingsCache.security.pin = newPin;

      /* Broadcast to lockscreen via parent */
      window.parent.postMessage(JSON.stringify({
        type: 'settings.pinChanged',
        data: { pin: newPin }
      }), '*');

      if (msgEl) { msgEl.textContent = t('settings.pin_changed') || 'PIN changed'; msgEl.style.color = '#22c55e'; }
      if (curInput) curInput.value = '';
      if (newInput) newInput.value = '';

      /* Auto-hide dialog after success */
      setTimeout(function () {
        if (pinDialog) pinDialog.classList.add('hidden');
        if (msgEl) msgEl.textContent = '';
      }, 1500);
    });
  }

  /* PIN remove button */
  var pinRemoveBtn = document.getElementById('pin-remove-btn');
  if (pinRemoveBtn) {
    pinRemoveBtn.addEventListener('click', function () {
      var curInput = document.getElementById('current-pin-input');
      var msgEl = document.getElementById('pin-change-msg');
      var currentPin = curInput ? curInput.value : '';
      var storedPin = settingsCache.security ? settingsCache.security.pin : '';

      /* Must verify current PIN to remove */
      if (!storedPin) {
        if (msgEl) { msgEl.textContent = t('settings.no_pin_set') || 'No PIN is set'; msgEl.style.color = '#f59e0b'; }
        return;
      }
      if (currentPin !== storedPin) {
        if (msgEl) { msgEl.textContent = t('settings.wrong_pin') || 'Incorrect current PIN'; msgEl.style.color = '#ef4444'; }
        return;
      }

      /* Remove PIN — set to empty string */
      updateSetting('security', 'pin', '');
      if (settingsCache.security) settingsCache.security.pin = '';

      window.parent.postMessage(JSON.stringify({
        type: 'settings.pinChanged',
        data: { pin: '' }
      }), '*');

      if (msgEl) { msgEl.textContent = t('settings.pin_removed') || 'PIN removed'; msgEl.style.color = '#22c55e'; }
      if (curInput) curInput.value = '';
      var lockVal = document.getElementById('lock-type-value');
      if (lockVal) lockVal.textContent = t('settings.swipe') || 'Swipe';

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

  /* ═══ Keyboard Wiring ═══ */
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
      var LANG_LABELS = {en:'English',ko:'\uD55C\uAD6D\uC5B4',ja:'\u65E5\u672C\u8A9E',zh:'\u4E2D\u6587',es:'Espa\u00F1ol'};
      statusEl.textContent = langs.map(function(l){return LANG_LABELS[l]||l;}).join(', ');
    }
  }

  /* Language toggles */
  document.querySelectorAll('.kb-lang-check').forEach(function (cb) {
    cb.addEventListener('change', function () {
      var enabled = [];
      document.querySelectorAll('.kb-lang-check:checked').forEach(function (c) { enabled.push(c.value); });
      if (enabled.length === 0) { cb.checked = true; return; } /* at least 1 */
      updateSetting('keyboard', 'languages', enabled.join(','));
      /* Update main menu status */
      var LANG_LABELS = {en:'English',ko:'\uD55C\uAD6D\uC5B4',ja:'\u65E5\u672C\u8A9E',zh:'\u4E2D\u6587',es:'Espa\u00F1ol'};
      var statusEl = document.getElementById('keyboard-status');
      if (statusEl) statusEl.textContent = enabled.map(function(l){return LANG_LABELS[l]||l;}).join(', ');
    });
  });

  /* Key height slider */
  var keyHeightSlider = document.getElementById('key-height-slider');
  if (keyHeightSlider) {
    keyHeightSlider.addEventListener('input', function () {
      updateSetting('keyboard', 'keyHeight', parseInt(keyHeightSlider.value, 10));
    });
  }

  /* Sound toggle */
  var kbSoundToggle = document.getElementById('kb-sound-toggle');
  if (kbSoundToggle) {
    kbSoundToggle.addEventListener('change', function () {
      updateSetting('keyboard', 'soundEnabled', kbSoundToggle.checked);
    });
  }

  /* Vibration toggle */
  var kbVibToggle = document.getElementById('kb-vibration-toggle');
  if (kbVibToggle) {
    kbVibToggle.addEventListener('change', function () {
      updateSetting('keyboard', 'vibrationEnabled', kbVibToggle.checked);
    });
  }

  /* Auto-capitalize toggle */
  var kbAutoCapToggle = document.getElementById('kb-autocap-toggle');
  if (kbAutoCapToggle) {
    kbAutoCapToggle.addEventListener('change', function () {
      updateSetting('keyboard', 'autoCapitalize', kbAutoCapToggle.checked);
    });
  }

  /* ═══ Applications Wiring ═══ */
  var installedApps = [];
  var appPermissions = {};

  var PERM_LABELS = {
    camera:       'settings.perm_camera',
    microphone:   'settings.perm_microphone',
    location:     'settings.perm_location',
    storage:      'settings.perm_storage',
    contacts:     'settings.perm_contacts',
    notifications:'settings.perm_notifications',
    phone:        'settings.perm_phone',
    bluetooth:    'settings.perm_bluetooth',
    network:      'settings.perm_network',
    sensors:      'settings.perm_sensors'
  };

  function renderAppList(apps) {
    var list = document.getElementById('app-list');
    if (!list) return;
    list.innerHTML = '';
    if (!apps || apps.length === 0) {
      list.innerHTML = '<div class="setting-item no-tap"><span class="setting-label" style="opacity:0.5">' + (t('settings.no_apps') || 'No apps') + '</span></div>';
      return;
    }
    apps.forEach(function (app) {
      var el = document.createElement('div');
      el.className = 'setting-item';
      el.innerHTML =
        '<div class="setting-text">' +
          '<span class="setting-label">' + (app.name || app.id) + '</span>' +
          '<span class="setting-value">' + (app.version || '') + '</span>' +
        '</div>' +
        '<svg class="chevron" viewBox="0 0 24 24"><path d="M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z"/></svg>';
      el.addEventListener('click', function () {
        openAppDetail(app);
      });
      list.appendChild(el);
    });
  }

  function openAppDetail(app) {
    /* Push current page onto navStack */
    if (currentPage) navStack.push(currentPage);

    var curPageEl = document.getElementById('page-' + currentPage);
    if (curPageEl) curPageEl.classList.add('hidden');

    currentPage = 'app-detail';
    var detailPage = document.getElementById('page-app-detail');
    if (detailPage) detailPage.classList.remove('hidden');

    headerTitle.textContent = app.name || app.id;

    /* Render header info */
    var header = document.getElementById('app-detail-header');
    if (header) {
      header.innerHTML =
        '<div class="setting-item no-tap">' +
          '<span class="setting-label">' + (t('settings.app_version') || 'Version') + '</span>' +
          '<span class="setting-value">' + (app.version || '1.0.0') + '</span>' +
        '</div>' +
        '<div class="setting-item no-tap">' +
          '<span class="setting-label">' + (t('settings.app_package') || 'Package') + '</span>' +
          '<span class="setting-value" style="font-size:12px;opacity:0.7">' + (app.id || '') + '</span>' +
        '</div>';
    }

    /* Render permissions */
    var permList = document.getElementById('app-detail-permissions');
    if (!permList) return;
    var perms = app.permissions || [];
    if (perms.length === 0) {
      permList.innerHTML =
        '<div class="setting-item no-tap">' +
          '<span class="setting-label" style="opacity:0.5">' + (t('settings.no_permissions') || 'No permissions') + '</span>' +
        '</div>';
      return;
    }

    permList.innerHTML = '<div class="setting-item no-tap"><span class="setting-label" style="font-weight:600">' + (t('settings.permissions') || 'Permissions') + '</span></div>';
    var appPerms = (appPermissions[app.id]) || {};

    perms.forEach(function (perm) {
      var granted = appPerms[perm] !== false; /* default granted */
      var row = document.createElement('div');
      row.className = 'setting-item no-tap';
      var labelKey = PERM_LABELS[perm] || '';
      var label = labelKey ? t(labelKey) : perm;
      row.innerHTML =
        '<span class="setting-label">' + label + '</span>' +
        '<label class="toggle">' +
          '<input type="checkbox" class="app-perm-toggle" data-app="' + app.id + '" data-perm="' + perm + '"' + (granted ? ' checked' : '') + '>' +
          '<span class="toggle-slider"></span>' +
        '</label>';
      row.querySelector('.app-perm-toggle').addEventListener('change', function () {
        var checked = this.checked;
        if (!appPermissions[app.id]) appPermissions[app.id] = {};
        appPermissions[app.id][perm] = checked;
        updateSetting('app_permissions', app.id, appPermissions[app.id]);
      });
      permList.appendChild(row);
    });
  }

  /* ═══ Scan buttons ═══ */
  var wifiScanBtn = document.getElementById('wifi-scan-btn');
  var btScanBtn = document.getElementById('bt-scan-btn');

  if (wifiScanBtn) wifiScanBtn.addEventListener('click', function () {
    wifiScanBtn.classList.add('scanning');
    wifiScanBtn.textContent = t('settings.scanning') || 'Scanning...';
    requestService('wifi', 'getNetworks');
    setTimeout(function () {
      wifiScanBtn.classList.remove('scanning');
      wifiScanBtn.textContent = t('settings.wifi_scan') || 'Scan for Networks';
    }, 3000);
  });

  if (btScanBtn) btScanBtn.addEventListener('click', function () {
    btScanBtn.classList.add('scanning');
    btScanBtn.textContent = t('settings.scanning') || 'Scanning...';
    requestService('bluetooth', 'getDevices');
    setTimeout(function () {
      btScanBtn.classList.remove('scanning');
      btScanBtn.textContent = t('settings.bt_scan') || 'Scan for Devices';
    }, 3000);
  });

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
  requestService('settings', 'get', { category: 'keyboard' });
  requestService('settings', 'get', { category: 'wallpaper' });
  requestService('settings', 'get', { category: 'app_permissions' });
  requestService('apps', 'getInstalled');

  /* ─── 초기화 ─── */
  applyTranslations();
  updateLangChecks();

  /* Re-sync language checks when locale changes from outside (e.g. emulator inject) */
  zylI18n.onLocaleChange(function () {
    updateLangChecks();
    /* Also update main menu current language display */
    var curLangEl = document.getElementById('current-lang');
    if (curLangEl) curLangEl.textContent = LANG_NAMES[zylI18n.getLocale()] || '';
  });

})();
