// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Core Controller
//
// 역할: 설정 앱 코어 — 네비게이션, IPC, 공유 상태, 도메인 모듈 라우팅
// 수행범위: 메뉴 탐색, 서비스 요청/응답 라우팅, 언어 선택, i18n
// 의존방향: zylI18n (i18n.js), ZylBridge (bridge.js)
// SOLID: SRP — 코어 라우팅과 공유 인프라만 담당, 도메인 로직은 개별 모듈에 위임
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

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

  /* ─── a11y: keyboard handler for button-like elements ─── */
  function addButtonKeyHandler(el) {
    el.setAttribute('tabindex', '0');
    el.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        el.click();
      }
    });
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
    applications: 'settings.applications'
  };

  /* ─── Navigation stack for multi-level pages ─── */
  var navStack = [];

  /* 메뉴 항목 클릭 → 서브 페이지 */
  document.querySelectorAll('.setting-item[data-page]').forEach(function (item) {
    addButtonKeyHandler(item);
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
    addButtonKeyHandler(opt);
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
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  /* ─── Send a setting update ─── */
  function updateSetting(category, key, value) {
    requestService('settings', 'update', { category: category, key: key, value: value });
  }

  /* ─── Track current settings state locally for UI ─── */
  var settingsCache = {};

  /* ─── Domain handler registry ─── */
  var handlers = {};

  /* ── Handle settings.get responses ── */
  function handleSettingsGet(params, data) {
    if (!params || !data) return;
    var cat = params.category;
    settingsCache[cat] = data;

    if (handlers[cat] && handlers[cat].onSettingsGet) {
      handlers[cat].onSettingsGet(data);
    }
  }

  /* ── Handle settings.update responses ── */
  function handleSettingsUpdated(params, data) {
    if (!params || !data) return;
    var cat = params.category;
    settingsCache[cat] = data;

    if (handlers[cat] && handlers[cat].onSettingsUpdated) {
      handlers[cat].onSettingsUpdated(data);
    }
  }

  /* ─── Listen for messages from emulator ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      if (!e.data) return;
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* ── Navigation back handling ── */
      if (msg.type === 'navigation.back') {
        if (currentPage) {
          /* Sub-page open -> go back (navStack or main menu) */
          btnBack.click();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          /* Already on main menu -> exit to home */
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      /* Route to domain handlers for service responses */
      var svc = msg.service;
      var method = msg.method;

      if (svc === 'settings' && method === 'get' && msg.data) {
        handleSettingsGet(msg.params, msg.data);
      } else if (svc === 'settings' && method === 'update' && msg.data) {
        handleSettingsUpdated(msg.params, msg.data);
      } else if (handlers[svc] && handlers[svc].onServiceResponse) {
        handlers[svc].onServiceResponse(method, msg.data);
      }
    } catch (err) { /* ignore */ }
  });

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

  /* Register device and storage as core handlers */
  handlers.device = {
    onServiceResponse: function (method, data) {
      if (method === 'getInfo' && data) renderAboutInfo(data);
    }
  };

  handlers.storage = {
    onServiceResponse: function (method, data) {
      if (method === 'getFormatted' && data) renderStorageInfo(data);
    }
  };

  /* ─── Expose ZylSettingsCore namespace for domain modules ─── */
  window.ZylSettingsCore = {
    t: t,
    requestService: requestService,
    updateSetting: updateSetting,
    settingsCache: settingsCache,
    handlers: handlers,
    navStack: navStack,
    PAGE_TITLES: PAGE_TITLES,
    getHeaderTitle: function () { return headerTitle; },
    getCurrentPage: function () { return currentPage; },
    setCurrentPage: function (val) { currentPage = val; }
  };

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
