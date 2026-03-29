// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 홈스크린 UI — 앱 그리드, 독, 검색, 시계 표시
// 수행범위: 앱 아이콘 렌더링, 검색 필터링, 페이지 인디케이터, 앱 실행
// 의존방향: zylI18n (i18n.js), ZylClock (clock.js), ZylBridge (bridge.js)
// SOLID: SRP — 홈스크린 UI 렌더링과 인터랙션만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── SVG 아이콘 정의 ─── */
  var ICONS = {
    browser:  '<svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-1 17.93c-3.95-.49-7-3.85-7-7.93 0-.62.08-1.21.21-1.79L9 15v1c0 1.1.9 2 2 2v1.93zm6.9-2.54c-.26-.81-1-1.39-1.9-1.39h-1v-3c0-.55-.45-1-1-1H8v-2h2c.55 0 1-.45 1-1V7h2c1.1 0 2-.9 2-2v-.41c2.93 1.19 5 4.06 5 7.41 0 2.08-.8 3.97-2.1 5.39z"/></svg>',
    files:    '<svg viewBox="0 0 24 24"><path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg>',
    terminal: '<svg viewBox="0 0 24 24"><path d="M20 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0 14H4V8h16v10zm-2-1h-6v-2h6v2zM7.5 17l-1.41-1.41L8.67 13l-2.59-2.59L7.5 9l4 4-4 4z"/></svg>',
    settings: '<svg viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.07.62-.07.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6A3.6 3.6 0 1115.6 12 3.6 3.6 0 0112 15.6z"/></svg>',
    camera:   '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3.2"/><path d="M9 2L7.17 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2h-3.17L15 2H9zm3 15c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5z"/></svg>',
    gallery:  '<svg viewBox="0 0 24 24"><path d="M21 19V5c0-1.1-.9-2-2-2H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2zM8.5 13.5l2.5 3.01L14.5 12l4.5 6H5l3.5-4.5z"/></svg>',
    music:    '<svg viewBox="0 0 24 24"><path d="M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z"/></svg>',
    clock:    '<svg viewBox="0 0 24 24"><path d="M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zM12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z"/></svg>',
    calc:     '<svg viewBox="0 0 24 24"><path d="M19 3H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zm-7.5 4.5h2V9h-2V7.5zm0 3h2V12h-2v-1.5zM7.5 7.5h2V9h-2V7.5zm0 3h2V12h-2v-1.5zM6 17.5v-2h3v2H6zm1.5-4h2V15h-2v-1.5zm3 4v-2h3v2h-3zm1.5-4h2V15h-2v-1.5zm4.5 4h-3v-2h3v2zm0-3.5h-2V12h2v1.5zm0-3h-2V9h2v2.5z"/></svg>',
    notes:    '<svg viewBox="0 0 24 24"><path d="M3 18h12v-2H3v2zM3 6v2h18V6H3zm0 7h18v-2H3v2z"/></svg>',
    weather:  '<svg viewBox="0 0 24 24"><path d="M19.35 10.04A7.49 7.49 0 0012 4C9.11 4 6.6 5.64 5.35 8.04A5.994 5.994 0 000 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96z"/></svg>',
    store:    '<svg viewBox="0 0 24 24"><path d="M18.36 9l.6 3H5.04l.6-3h12.72M20 4H4v2h16V4zm0 3H4l-1 5v2h1v6h10v-6h4v6h2v-6h1v-2l-1-5zM6 18v-4h6v4H6z"/></svg>',
  };

  /* ─── 앱 정의 (서비스에서 로드) ─── */
  var defaultApps = [];

  /* Request app list from central service */
  var appListReceived = false;
  var appListTimeoutId = null;

  function requestAppList() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request',
      service: 'apps',
      method: 'getInstalled'
    }), '*');

    /* 5-second timeout: show empty grid if no response */
    appListTimeoutId = setTimeout(function () {
      if (!appListReceived) {
        appGrid.innerHTML = '<div style="text-align:center;opacity:0.5;padding:32px;grid-column:1/-1">No apps</div>';
      }
    }, 5000);
  }

  /* 배경화면 그라데이션 매핑 */
  var wallpaperGradients = {
    'default':         'radial-gradient(ellipse at 20% 50%, rgba(72,52,160,0.4) 0%, transparent 50%), radial-gradient(ellipse at 80% 20%, rgba(29,78,137,0.5) 0%, transparent 50%), linear-gradient(160deg, #0a0a1a 0%, #0d1b2a 40%, #1b2838 100%)',
    'gradient-blue':   'linear-gradient(135deg, #0c3547 0%, #1a6b8a 50%, #0a2a3a 100%)',
    'gradient-purple': 'linear-gradient(135deg, #1a0a2e 0%, #4a2080 50%, #1a0a2e 100%)',
    'gradient-dark':   'linear-gradient(160deg, #050505 0%, #1a1a1a 50%, #0a0a0a 100%)',
    'gradient-sunset':  'linear-gradient(135deg, #2d1b3d 0%, #b44a2a 50%, #1a0a2e 100%)',
  };

  /* Listen for service responses + settings changes */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* 앱 목록 응답 — 시스템 전용 앱은 홈 그리드에서 제외 */
      if (msg.type === 'service.response' && msg.service === 'apps' && msg.method === 'getInstalled' && msg.data) {
        appListReceived = true;
        if (appListTimeoutId) { clearTimeout(appListTimeoutId); appListTimeoutId = null; }
        var SYSTEM_ONLY = ['com.zylos.lockscreen', 'com.zylos.statusbar', 'com.zylos.oobe', 'com.zylos.home'];
        defaultApps = msg.data.filter(function (app) {
          return SYSTEM_ONLY.indexOf(app.id) === -1;
        });
        renderAppGrid(defaultApps);
      }

      /* 배경화면 변경 수신 → 실제 배경 CSS 변경 */
      if (msg.type === 'settings.wallpaperChanged' && msg.data && msg.data.wallpaper) {
        var wpEl = document.getElementById('wallpaper');
        var grad = wallpaperGradients[msg.data.wallpaper];
        if (wpEl && grad) {
          wpEl.style.background = grad;
        }
      }
    } catch (err) { /* ignore */ }
  });

  requestAppList();

  /* ─── 시계 (shared ZylClock 사용) ─── */
  var clockTime = document.getElementById('clock-time');
  var clockDate = document.getElementById('clock-date');
  var clock = ZylClock.create(clockTime, clockDate, { showDate: true, dateFormat: 'long' });

  /* ─── 앱 그리드 렌더링 (페이지 분할) ─── */
  var pagesTrack = document.getElementById('app-pages-track');
  var pageIndicator = document.getElementById('page-indicator');
  var APPS_PER_PAGE = 8; /* 4열 × 2행 */
  var currentPage = 0;
  var totalPages = 1;

  function renderAppGrid(apps) {
    if (!pagesTrack) return;
    pagesTrack.innerHTML = '';
    totalPages = Math.max(1, Math.ceil(apps.length / APPS_PER_PAGE));
    currentPage = Math.min(currentPage, totalPages - 1);

    for (var p = 0; p < totalPages; p++) {
      var page = document.createElement('div');
      page.className = 'app-page';
      var start = p * APPS_PER_PAGE;
      var end = Math.min(start + APPS_PER_PAGE, apps.length);

      for (var i = start; i < end; i++) {
        var app = apps[i];
        var el = document.createElement('div');
        el.className = 'app-item';
        el.dataset.appId = app.id;

        var iconSvg = ICONS[app.icon] || ICONS.browser;
        var name = zylI18n.t(app.nameKey);

        var iconWrap = document.createElement('div');
        iconWrap.className = 'app-icon-wrap ' + app.color;
        iconWrap.innerHTML = iconSvg;

        var nameEl = document.createElement('div');
        nameEl.className = 'app-name';
        nameEl.textContent = name;

        el.appendChild(iconWrap);
        el.appendChild(nameEl);
        page.appendChild(el);
      }
      pagesTrack.appendChild(page);
    }
    updatePageIndicator();
    goToPage(currentPage);
  }

  function goToPage(idx) {
    currentPage = Math.max(0, Math.min(idx, totalPages - 1));
    if (pagesTrack) pagesTrack.style.transform = 'translateX(-' + (currentPage * 100) + '%)';
    updatePageIndicator();
  }

  function updatePageIndicator() {
    if (!pageIndicator) return;
    pageIndicator.innerHTML = '';
    for (var i = 0; i < totalPages; i++) {
      var dot = document.createElement('span');
      dot.className = 'dot' + (i === currentPage ? ' active' : '');
      pageIndicator.appendChild(dot);
    }
  }

  /* ─── 좌우 스와이프 ─── */
  var swipe = { active: false, startX: 0 };
  var viewport = document.getElementById('app-pages-viewport');

  if (viewport) {
    viewport.addEventListener('touchstart', function (e) {
      swipe.active = true;
      swipe.startX = e.touches[0].clientX;
    }, { passive: true });

    viewport.addEventListener('touchend', function (e) {
      if (!swipe.active) return;
      swipe.active = false;
      var dx = e.changedTouches[0].clientX - swipe.startX;
      if (Math.abs(dx) > 50) {
        goToPage(currentPage + (dx < 0 ? 1 : -1));
      }
    });

    viewport.addEventListener('mousedown', function (e) {
      swipe.active = true;
      swipe.startX = e.clientX;
    });

    viewport.addEventListener('mouseup', function (e) {
      if (!swipe.active) return;
      swipe.active = false;
      var dx = e.clientX - swipe.startX;
      if (Math.abs(dx) > 50) {
        goToPage(currentPage + (dx < 0 ? 1 : -1));
      }
    });
  }

  /* ─── 편집 모드 ─── */
  var editMode = false;
  var longPressTimer = null;

  pagesTrack.addEventListener('mousedown', function (e) {
    var appItem = e.target.closest('.app-item');
    if (!appItem) return;
    longPressTimer = setTimeout(function () {
      enterEditMode();
      longPressTimer = null;
    }, 800);
  });

  pagesTrack.addEventListener('mouseup', function () {
    if (longPressTimer) { clearTimeout(longPressTimer); longPressTimer = null; }
  });

  pagesTrack.addEventListener('mouseleave', function () {
    if (longPressTimer) { clearTimeout(longPressTimer); longPressTimer = null; }
  });

  /* 삭제 불가 앱 (기본 앱 + 시스템 서비스) */
  var UNDELETABLE = [
    'com.zylos.settings', 'com.zylos.browser', 'com.zylos.files',
    'com.zylos.terminal', 'com.zylos.camera', 'com.zylos.gallery',
    'com.zylos.music', 'com.zylos.clock', 'com.zylos.calc',
    'com.zylos.notes', 'com.zylos.weather', 'com.zylos.store'
  ];

  function enterEditMode() {
    editMode = true;
    pagesTrack.classList.add('edit-mode');
    pagesTrack.querySelectorAll('.app-item').forEach(function (el) {
      if (el.querySelector('.app-delete')) return;
      var appId = el.dataset.appId;
      /* 시스템/기본 앱은 삭제 버튼 없음 */
      if (UNDELETABLE.indexOf(appId) !== -1) return;
      var delBtn = document.createElement('div');
      delBtn.className = 'app-delete';
      delBtn.textContent = '\u00d7';
      delBtn.addEventListener('click', function (e) {
        e.stopPropagation();
        /* 이중 가드: UNDELETABLE 앱은 삭제 불가 */
        if (UNDELETABLE.indexOf(appId) !== -1) return;
        defaultApps = defaultApps.filter(function (a) { return a.id !== appId; });
        el.remove();
      });
      el.appendChild(delBtn);
    });
  }

  function exitEditMode() {
    editMode = false;
    pagesTrack.classList.remove('edit-mode');
    pagesTrack.querySelectorAll('.app-delete').forEach(function (el) { el.remove(); });
  }

  /* 빈 영역 클릭 시 편집 모드 종료 */
  document.addEventListener('click', function (e) {
    if (editMode && !e.target.closest('.app-item') && !e.target.closest('.app-delete')) {
      exitEditMode();
    }
  });

  /* Event delegation: single click handler on the pages track */
  pagesTrack.addEventListener('click', function (e) {
    if (editMode) return;
    var appItem = e.target.closest('.app-item');
    if (!appItem) return;
    var appId = appItem.dataset.appId;
    if (appId) launchApp(appId, appItem);
  });

  /* ─── 앱 실행 (shared ZylBridge 사용) ─── */
  function launchApp(appId, el) {
    if (el) {
      el.classList.add('launching');
      setTimeout(function () { el.classList.remove('launching'); }, 500);
    }
    ZylBridge.launch(appId);
  }

  /* ─── 독 클릭 ─── */
  document.querySelectorAll('.dock-app').forEach(function (el) {
    el.addEventListener('click', function () {
      var appId = el.dataset.app;
      if (appId) launchApp(appId, el);
    });
  });

  /* ─── 검색 ─── */
  var searchInput = document.getElementById('search-input');
  searchInput.addEventListener('input', function () {
    var query = searchInput.value.toLowerCase().trim();
    if (!query) {
      renderAppGrid(defaultApps);
      return;
    }
    var filtered = defaultApps.filter(function (app) {
      var name = zylI18n.t(app.nameKey).toLowerCase();
      return name.includes(query) || app.id.toLowerCase().includes(query);
    });
    renderAppGrid(filtered);
  });

  /* ─── Re-render on locale change ─── */
  zylI18n.onLocaleChange(function () {
    renderAppGrid(defaultApps);
  });

  /* ─── 초기 렌더링 (서비스 응답 전 로딩 상태) ─── */
  if (defaultApps.length === 0) {
    appGrid.innerHTML = '<div style="text-align:center;opacity:0.5;padding:32px;grid-column:1/-1">Loading...</div>';
  } else {
    renderAppGrid(defaultApps);
  }

})();
