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
    'device-class': 'settings.device_class',
    privacy: 'settings.privacy',
    datetime: 'settings.datetime',
    wallpaper: 'settings.wallpaper',
    security: 'settings.security',
    storage: 'settings.storage',
    about: 'settings.about',
    applications: 'settings.applications',
    /* FIX: Added missing placeholder sections that were in the menu but had no routing */
    battery: 'settings.battery',
    accessibility: 'settings.accessibility',
    developer: 'settings.developer_options'
  };

  /* ─── Navigation stack for multi-level pages ─── */
  var navStack = [];

  /* 서브 페이지 열기 — 메뉴 클릭과 상태 복원이 같은 경로를 쓴다 */
  function openPage(pageId) {
    var page = document.getElementById('page-' + pageId);
    if (!page) return false;

    mainMenu.classList.add('hidden');
    page.classList.remove('hidden');
    btnBack.classList.remove('hidden');
    currentPage = pageId;

    headerTitle.textContent = t(PAGE_TITLES[pageId] || 'settings.' + pageId);
    if (pageId === 'datetime') renderTimeZoneState();
    return true;
  }

  /* 메뉴 항목 클릭 → 서브 페이지 */
  document.querySelectorAll('.setting-item[data-page]').forEach(function (item) {
    addButtonKeyHandler(item);
    item.addEventListener('click', function () {
      openPage(item.dataset.page);
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

  /* ─── System Service IPC (fire-and-forget for init reads) ─── */
  function requestServiceFire(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  /* ─── Promise-based service request for user actions ─── */
  function requestService(service, method, params) {
    return ZylBridge.requestService(service, method, params || {});
  }

  /* ─── Send a setting update (Promise-based) ─── */
  function updateSetting(category, key, value) {
    return requestService('settings', 'update', { category: category, key: key, value: value });
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
  /* textContent, not innerHTML: deviceName is user-editable, and markup in it
     used to execute inside Settings — a system app with every permission */
  function infoRow(labelText, valueText) {
    var el = document.createElement('div');
    el.className = 'setting-item no-tap';
    var label = document.createElement('span');
    label.className = 'setting-label';
    label.textContent = labelText;
    var value = document.createElement('span');
    value.className = 'setting-value';
    value.textContent = (valueText === undefined || valueText === null) ? '' : String(valueText);
    el.appendChild(label);
    el.appendChild(value);
    return el;
  }

  function renderAboutInfo(info) {
    var list = document.getElementById('about-info-list');
    if (!list) return;
    list.innerHTML = '';
    var items = [
      { label: t('settings.device_name'), value: info.deviceName },
      { label: t('settings.os_version'),  value: info.osVersion },
      { label: t('settings.security_patch'), value: info.securityPatch },
      { label: t('settings.soc') || 'SoC', value: info.soc },
      { label: t('settings.ram') || 'RAM', value: info.ram },
      { label: t('settings.kernel'),      value: info.kernel },
      { label: t('settings.build'),       value: info.build }
    ];
    items.forEach(function (item) {
      list.appendChild(infoRow(item.label, item.value));
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
      list.appendChild(infoRow(item.label, item.value));
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

  /* FIX: Battery page — populate from power service response */
  handlers.power = {
    onServiceResponse: function (method, data) {
      if (method === 'getState' && data) {
        var levelEl = document.getElementById('battery-level-value');
        var chargingEl = document.getElementById('battery-charging-value');
        var statusEl = document.getElementById('battery-status');
        if (levelEl && data.batteryLevel !== undefined) {
          levelEl.textContent = data.batteryLevel + '%';
        }
        if (chargingEl) {
          chargingEl.textContent = data.charging ? t('settings.charging') : t('settings.not_charging');
        }
        if (statusEl && data.batteryLevel !== undefined) {
          statusEl.textContent = data.batteryLevel + '%';
        }
      }
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

  /* ═══ Request all service data on init (fire-and-forget reads) ═══ */
  requestServiceFire('wifi', 'getNetworks');
  requestServiceFire('bluetooth', 'getDevices');
  requestServiceFire('device', 'getInfo');
  requestServiceFire('storage', 'getFormatted');

  /* Request settings state for all categories */
  requestServiceFire('settings', 'get', { category: 'wifi' });
  requestServiceFire('settings', 'get', { category: 'bluetooth' });
  requestServiceFire('settings', 'get', { category: 'display' });
  requestServiceFire('settings', 'get', { category: 'sound' });
  requestServiceFire('settings', 'get', { category: 'security' });
  requestServiceFire('settings', 'get', { category: 'keyboard' });
  requestServiceFire('settings', 'get', { category: 'wallpaper' });
  requestServiceFire('settings', 'get', { category: 'app_permissions' });
  requestServiceFire('apps', 'getInstalled');
  /* FIX: Request battery state for battery status display */
  requestServiceFire('power', 'getState');

  /* ─── 기기 유형 (폼팩터) ───
     TV·자동차·워치·PDA 등 기하만으로 판별할 수 없는 단말의 프로비저닝 경로.
     실기기에서는 빌드 프로파일(meson -Dprofile)이 우선이고, 이 설정은
     커스텀 단말과 에뮬레이터에서 기기 유형을 명시 지정하는 수단이다.
     선택은 즉시 이 앱에 적용되고, 다른 앱은 다음 실행 시 반영된다. */
  function updateDeviceClassValue() {
    var el = document.getElementById('current-device-class');
    if (!el || typeof ZylFormFactor === 'undefined') return;
    el.textContent = t('settings.ff_' + ZylFormFactor.getId());
  }

  function renderDeviceClassList() {
    var list = document.getElementById('device-class-list');
    if (!list || typeof ZylFormFactor === 'undefined') return;

    var activeId = ZylFormFactor.getId();
    list.innerHTML = '';

    ZylFormFactor.getAllIds().forEach(function (ffId) {
      var row = document.createElement('div');
      row.className = 'setting-item ff-option';
      row.dataset.ff = ffId;
      row.setAttribute('role', 'button');
      /* Static markup only — the label is set via textContent below */
      row.innerHTML = '<span class="setting-label"></span>' +
        '<svg class="check-icon hidden" viewBox="0 0 24 24" aria-hidden="true">' +
        '<path d="M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z"/></svg>';
      row.querySelector('.setting-label').textContent = t('settings.ff_' + ffId);
      row.setAttribute('aria-label', t('settings.ff_' + ffId));
      if (ffId === activeId) {
        row.querySelector('.check-icon').classList.remove('hidden');
      }
      addButtonKeyHandler(row);
      row.addEventListener('click', function () {
        if (!ZylFormFactor.setOverride(ffId)) return;
        ZylFormFactor.reload();
        renderDeviceClassList();
        updateDeviceClassValue();
      });
      list.appendChild(row);
    });
  }

  /* ─── 날짜 및 시간: 시간대 ───
     목록은 엔진이 아는 IANA 시간대 전부(Intl.supportedValuesOf), 이름은 ICU 가
     사용자 언어로 지역화한다("한국 표준시") — 번역 문자열을 따로 두지 않는다.
     목록은 페이지를 열 때만 그린다(418개를 앱 시작 때 그리면 DOM 예산을 넘는다). */
  var TZ_FALLBACK = [
    'Pacific/Honolulu', 'America/Anchorage', 'America/Los_Angeles', 'America/Denver',
    'America/Chicago', 'America/New_York', 'America/Sao_Paulo', 'Atlantic/Azores',
    'UTC', 'Europe/London', 'Europe/Paris', 'Europe/Berlin', 'Europe/Madrid',
    'Africa/Cairo', 'Europe/Moscow', 'Asia/Dubai', 'Asia/Karachi', 'Asia/Kolkata',
    'Asia/Kathmandu', 'Asia/Dhaka', 'Asia/Bangkok', 'Asia/Shanghai', 'Asia/Singapore',
    'Asia/Taipei', 'Asia/Seoul', 'Asia/Tokyo', 'Australia/Adelaide', 'Australia/Sydney',
    'Pacific/Auckland'
  ];
  /* ICU keeps the old IANA ids as canonical for compatibility and lists only
     them: searching "kolkata" found nothing because the engine says
     "Asia/Calcutta". Show and search the current city names. */
  var TZ_CURRENT_NAME = {
    'Asia/Calcutta': 'Asia/Kolkata', 'Asia/Katmandu': 'Asia/Kathmandu',
    'Europe/Kiev': 'Europe/Kyiv', 'Asia/Saigon': 'Asia/Ho_Chi_Minh',
    'Asia/Rangoon': 'Asia/Yangon', 'America/Godthab': 'America/Nuuk',
    'Atlantic/Faeroe': 'Atlantic/Faroe', 'Pacific/Truk': 'Pacific/Chuuk',
    'Pacific/Ponape': 'Pacific/Pohnpei', 'America/Coral_Harbour': 'America/Atikokan',
    'Pacific/Enderbury': 'Pacific/Kanton'
  };
  function zoneCityName(id) {
    return (TZ_CURRENT_NAME[id] || id).replace(/_/g, ' ');
  }
  var tzToggle = document.getElementById('timezone-auto-toggle');
  var tzCurrent = document.getElementById('timezone-current');
  var tzMenuValue = document.getElementById('current-timezone');
  var tzPicker = document.getElementById('timezone-picker');
  var tzList = document.getElementById('timezone-list');
  var tzSearch = document.getElementById('timezone-search');
  var _zones = null;

  function allZones() {
    if (_zones) return _zones;
    var ids;
    try { ids = Intl.supportedValuesOf('timeZone'); } catch (e) { ids = TZ_FALLBACK; }
    if (!ids || !ids.length) ids = TZ_FALLBACK;
    var now = new Date();
    _zones = ids.map(function (id) {
      return { id: id, offset: zylI18n.zoneOffsetMinutes(id, now) };
    }).sort(function (a, b) {
      return (a.offset - b.offset) || (a.id < b.id ? -1 : 1);
    });
    return _zones;
  }

  function describeZone(id) {
    return zylI18n.zoneName(id) + ' (' + zylI18n.formatOffset(zylI18n.zoneOffsetMinutes(id)) + ')';
  }

  function saveTimeZone(value) {
    /* Apply here at once; the compositor stores it and tells every other app */
    zylI18n.setTimeZone(value);
    requestService('settings', 'update', { category: 'datetime', key: 'timezone', value: value });
  }

  function renderZoneList() {
    if (!tzList) return;
    var query = tzSearch ? tzSearch.value.toLowerCase().replace(/^\s+|\s+$/g, '') : '';
    var current = zylI18n.getTimeZone();
    var frag = document.createDocumentFragment();
    allZones().forEach(function (z) {
      var name = zylI18n.zoneName(z.id);
      var city = zoneCityName(z.id);
      if (query && city.toLowerCase().indexOf(query) === -1 &&
          z.id.toLowerCase().replace(/_/g, ' ').indexOf(query) === -1 &&
          name.toLowerCase().indexOf(query) === -1) return;
      var row = document.createElement('div');
      row.className = 'setting-item tz-item';
      row.setAttribute('role', 'button');
      row.dataset.tz = z.id;
      var text = document.createElement('div');
      text.className = 'setting-text';
      var label = document.createElement('span');
      label.className = 'setting-label';
      label.textContent = name + ' (' + zylI18n.formatOffset(z.offset) + ')';
      var value = document.createElement('span');
      value.className = 'setting-value';
      value.textContent = city;
      text.appendChild(label);
      text.appendChild(value);
      row.appendChild(text);
      if (z.id === current) {
        row.insertAdjacentHTML('beforeend', '<svg class="check-icon" viewBox="0 0 24 24" aria-hidden="true">' +
          '<path d="M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z"/></svg>');
        row.setAttribute('aria-current', 'true');
      }
      addButtonKeyHandler(row);
      row.addEventListener('click', function () {
        saveTimeZone(z.id);
        renderTimeZoneState();
      });
      frag.appendChild(row);
    });
    tzList.innerHTML = '';
    tzList.appendChild(frag);
  }

  function renderTimeZoneState() {
    var auto = zylI18n.isAutoTimeZone();
    var label = describeZone(zylI18n.getTimeZone());
    if (tzToggle) tzToggle.checked = auto;
    if (tzCurrent) tzCurrent.textContent = label;
    if (tzMenuValue) tzMenuValue.textContent = label;
    if (tzPicker) tzPicker.classList.toggle('hidden', auto);
    if (!auto && currentPage === 'datetime') renderZoneList();
  }

  if (tzToggle) {
    tzToggle.addEventListener('change', function () {
      /* Turning automatic off keeps the zone the user is in, then lets them pick */
      saveTimeZone(tzToggle.checked ? 'auto' : zylI18n.getTimeZone());
      renderTimeZoneState();
    });
  }
  if (tzSearch) tzSearch.addEventListener('input', renderZoneList);

  /* ─── Instance state: come back to the page the user was on ───
     Only top-level pages listed in the main menu are restored; a nested detail
     page depends on data that has to be fetched again. */
  ZylBridge.onSaveState(function () {
    if (!currentPage) return null;
    if (!document.querySelector('.setting-item[data-page="' + currentPage + '"]')) return null;
    return { page: currentPage };
  });
  ZylBridge.onRestoreState(function (st) {
    /* The value ends up inside a selector — accept page ids only */
    if (st && typeof st.page === 'string' && /^[a-z][a-z-]*$/.test(st.page) &&
        document.querySelector('.setting-item[data-page="' + st.page + '"]')) {
      openPage(st.page);
    }
  });

  /* ─── 개인정보 보호: 진단 데이터 동의 ───
     토글은 수집 주체의 현재 상태를 그대로 보여준다. 끄면 기기에서는 수집 데몬이
     저장된 이벤트와 기기 ID 를 지운다. 응답이 오기 전에는 꺼진 상태로 둔다 —
     확인되지 않은 동의를 켜진 것처럼 보여주지 않는다. */
  var telemetryToggle = document.getElementById('telemetry-toggle');
  if (telemetryToggle) {
    telemetryToggle.checked = false;
    requestService('telemetry', 'getEnabled').then(function (on) {
      telemetryToggle.checked = (on === true);
    }).catch(function () {
      telemetryToggle.checked = false;   /* unknown is reported as off */
    });
    telemetryToggle.addEventListener('change', function () {
      var wanted = telemetryToggle.checked;
      requestService('telemetry', 'setEnabled', { enabled: wanted }).then(function (on) {
        /* Reflect what the service actually stored, not what was asked */
        if (typeof on === 'boolean') telemetryToggle.checked = on;
      }).catch(function () {
        telemetryToggle.checked = !wanted;   /* not persisted — undo the flip */
      });
    });
  }

  /* ─── 초기화 ─── */
  applyTranslations();
  updateLangChecks();
  renderDeviceClassList();
  updateDeviceClassValue();
  if (tzMenuValue) tzMenuValue.textContent = describeZone(zylI18n.getTimeZone());

  /* Re-sync language checks when locale changes from outside (e.g. emulator inject) */
  zylI18n.onLocaleChange(function () {
    updateLangChecks();
    /* Also update main menu current language display */
    var curLangEl = document.getElementById('current-lang');
    if (curLangEl) curLangEl.textContent = LANG_NAMES[zylI18n.getLocale()] || '';
    renderDeviceClassList();
    updateDeviceClassValue();
    renderTimeZoneState();   /* zone names are localised; also fires on zone changes */
  });

})();
