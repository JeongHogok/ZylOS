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

  /* Request all settings-relevant service data on init */
  requestService('wifi', 'getNetworks');
  requestService('bluetooth', 'getDevices');
  requestService('device', 'getInfo');
  requestService('storage', 'getFormatted');

  /* ─── 초기화 ─── */
  applyTranslations();
  updateLangChecks();

})();
