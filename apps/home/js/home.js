// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 홈스크린 UI — 앱 그리드, 독, 검색, 시계 표시
// 수행범위: 앱 아이콘 렌더링, 검색 필터링, 페이지 인디케이터, 앱 실행
// 의존방향: bpiI18n (i18n.js), BpiClock (clock.js), BpiBridge (bridge.js)
// SOLID: SRP — 홈스크린 UI 렌더링과 인터랙션만 담당
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

  /* ─── 앱 정의 (아이콘 + 색상) ─── */
  var defaultApps = [
    { id: 'com.bpios.camera',   nameKey: 'app.camera',   icon: 'camera',   color: 'icon-red'     },
    { id: 'com.bpios.gallery',  nameKey: 'app.gallery',  icon: 'gallery',  color: 'icon-pink'    },
    { id: 'com.bpios.music',    nameKey: 'app.music',    icon: 'music',    color: 'icon-red'     },
    { id: 'com.bpios.clock',    nameKey: 'app.clock',    icon: 'clock',    color: 'icon-indigo'  },
    { id: 'com.bpios.calc',     nameKey: 'app.calc',     icon: 'calc',     color: 'icon-orange'  },
    { id: 'com.bpios.notes',    nameKey: 'app.notes',    icon: 'notes',    color: 'icon-amber'   },
    { id: 'com.bpios.weather',  nameKey: 'app.weather',  icon: 'weather',  color: 'icon-cyan'    },
    { id: 'com.bpios.store',    nameKey: 'app.store',    icon: 'store',    color: 'icon-emerald' },
  ];

  /* ─── 시계 (shared BpiClock 사용) ─── */
  var clockTime = document.getElementById('clock-time');
  var clockDate = document.getElementById('clock-date');
  var clock = BpiClock.create(clockTime, clockDate, { showDate: true, dateFormat: 'long' });

  /* ─── 앱 그리드 렌더링 ─── */
  var appGrid = document.getElementById('app-grid');

  function renderAppGrid(apps) {
    appGrid.innerHTML = '';
    apps.forEach(function (app) {
      var el = document.createElement('div');
      el.className = 'app-item';
      el.dataset.appId = app.id;

      var iconSvg = ICONS[app.icon] || ICONS.browser;
      var name = bpiI18n.t(app.nameKey);

      el.innerHTML =
        '<div class="app-icon-wrap ' + app.color + '">' + iconSvg + '</div>' +
        '<div class="app-name">' + name + '</div>';

      el.addEventListener('click', function () {
        launchApp(app.id, el);
      });
      appGrid.appendChild(el);
    });
  }

  /* ─── 앱 실행 (shared BpiBridge 사용) ─── */
  function launchApp(appId, el) {
    if (el) {
      el.classList.add('launching');
      setTimeout(function () { el.classList.remove('launching'); }, 500);
    }
    BpiBridge.launch(appId);
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
      var name = bpiI18n.t(app.nameKey).toLowerCase();
      return name.includes(query) || app.id.toLowerCase().includes(query);
    });
    renderAppGrid(filtered);
  });

  /* ─── Re-render on locale change ─── */
  bpiI18n.onLocaleChange(function () {
    renderAppGrid(defaultApps);
  });

  /* ─── 초기 렌더링 ─── */
  renderAppGrid(defaultApps);

})();
