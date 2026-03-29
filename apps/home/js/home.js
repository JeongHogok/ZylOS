// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Home screen UI — app grid with page swipe, dynamic dock,
//       search, clock, drag-and-drop between dock and grid
// Scope: App icon rendering, multi-page swipe with drag feedback,
//        search filtering, edit mode (long press) with app deletion,
//        dock ↔ grid drag-and-drop reordering, settings persistence
// Dependency: zylI18n (i18n.js), ZylClock (clock.js), ZylBridge (bridge.js)
// SOLID: SRP — home screen UI rendering and interaction only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── SVG Icons ─── */
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

  /* ─── Data Model ─── */
  var DEFAULT_DOCK = ['com.zylos.browser', 'com.zylos.files', 'com.zylos.terminal', 'com.zylos.settings'];
  var dockApps = DEFAULT_DOCK.slice();
  var defaultApps = [];
  var appListReceived = false;
  var dockSettingsLoaded = false;

  var SYSTEM_ONLY = ['com.zylos.lockscreen', 'com.zylos.statusbar', 'com.zylos.oobe', 'com.zylos.home', 'com.zylos.keyboard'];

  var UNDELETABLE = [
    'com.zylos.settings', 'com.zylos.browser', 'com.zylos.files',
    'com.zylos.terminal', 'com.zylos.camera', 'com.zylos.gallery',
    'com.zylos.music', 'com.zylos.clock', 'com.zylos.calc',
    'com.zylos.notes', 'com.zylos.weather', 'com.zylos.store'
  ];

  /* Wallpaper gradients */
  var wallpaperGradients = {
    'default':         'radial-gradient(ellipse at 20% 50%, rgba(72,52,160,0.4) 0%, transparent 50%), radial-gradient(ellipse at 80% 20%, rgba(29,78,137,0.5) 0%, transparent 50%), linear-gradient(160deg, #0a0a1a 0%, #0d1b2a 40%, #1b2838 100%)',
    'gradient-blue':   'linear-gradient(135deg, #0c3547 0%, #1a6b8a 50%, #0a2a3a 100%)',
    'gradient-purple': 'linear-gradient(135deg, #1a0a2e 0%, #4a2080 50%, #1a0a2e 100%)',
    'gradient-dark':   'linear-gradient(160deg, #050505 0%, #1a1a1a 50%, #0a0a0a 100%)',
    'gradient-sunset': 'linear-gradient(135deg, #2d1b3d 0%, #b44a2a 50%, #1a0a2e 100%)',
  };

  /* ─── Helpers ─── */
  function findApp(appId) {
    for (var i = 0; i < defaultApps.length; i++) {
      if (defaultApps[i].id === appId) return defaultApps[i];
    }
    return null;
  }

  function getGridApps() {
    return defaultApps.filter(function (app) {
      return dockApps.indexOf(app.id) === -1;
    });
  }

  /* ─── Service Requests ─── */
  function requestAppList() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'apps', method: 'getInstalled'
    }), '*');
  }

  function requestDockSettings() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'settings', method: 'get', params: { category: 'home' }
    }), '*');
  }

  function saveDockSettings() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'settings', method: 'update',
      params: { category: 'home', key: 'dock', value: dockApps.join(',') }
    }), '*');
  }

  /* ═══════════════════════════════════════════════════════
     Dock — dynamic rendering
     ═══════════════════════════════════════════════════════ */
  function createDockItem(app) {
    var el = document.createElement('div');
    el.className = 'dock-app';
    el.dataset.app = app.id;

    var iconSvg = ICONS[app.icon] || ICONS.browser;

    var iconWrap = document.createElement('div');
    iconWrap.className = 'dock-icon ' + app.color;
    iconWrap.innerHTML = iconSvg;

    var nameEl = document.createElement('span');
    nameEl.setAttribute('data-i18n', app.nameKey);
    nameEl.textContent = zylI18n.t(app.nameKey);

    el.appendChild(iconWrap);
    el.appendChild(nameEl);

    el.addEventListener('click', function () {
      if (editMode || appDrag.active) return;
      launchApp(app.id, el);
    });

    return el;
  }

  function renderDock() {
    var dock = document.getElementById('dock');
    dock.innerHTML = '';
    dockApps.forEach(function (appId) {
      var app = findApp(appId);
      if (!app) return;
      var el = createDockItem(app);
      dock.appendChild(el);
    });
    if (editMode) {
      dock.classList.add('edit-mode');
    }
  }

  /* ═══════════════════════════════════════════════════════
     Message Handler
     ═══════════════════════════════════════════════════════ */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* Navigation back -> home is root, always exit */
      if (msg.type === 'navigation.back') {
        window.parent.postMessage(JSON.stringify({ type: 'navigation.exit' }), '*');
        return;
      }

      /* App list response */
      if (msg.type === 'service.response' && msg.service === 'apps' && msg.method === 'getInstalled' && msg.data) {
        appListReceived = true;
        defaultApps = msg.data.filter(function (app) {
          return SYSTEM_ONLY.indexOf(app.id) === -1;
        });
        renderDock();
        renderAppGrid(getGridApps());
      }

      /* Settings response — dock config */
      if (msg.type === 'service.response' && msg.service === 'settings' && msg.data && msg.data.dock) {
        var saved = msg.data.dock.split(',').filter(Boolean);
        if (saved.length > 0) {
          dockApps = saved;
        }
        dockSettingsLoaded = true;
        if (appListReceived) {
          renderDock();
          renderAppGrid(getGridApps());
        }
      }

      /* Wallpaper change */
      if (msg.type === 'settings.wallpaperChanged' && msg.data && msg.data.wallpaper) {
        var wpEl = document.getElementById('wallpaper');
        var grad = wallpaperGradients[msg.data.wallpaper];
        if (wpEl && grad) wpEl.style.background = grad;
      }
    } catch (err) { /* ignore */ }
  });

  requestDockSettings();
  requestAppList();

  /* ─── Clock (shared ZylClock) ─── */
  var clockTime = document.getElementById('clock-time');
  var clockDate = document.getElementById('clock-date');
  ZylClock.create(clockTime, clockDate, { showDate: true, dateFormat: 'long' });

  /* ═══════════════════════════════════════════════════════
     App Grid — multi-page with real-time swipe drag
     ═══════════════════════════════════════════════════════ */
  var pagesTrack = document.getElementById('app-pages-track');
  var pageIndicator = document.getElementById('page-indicator');
  var viewport = document.getElementById('app-pages-viewport');
  var APPS_PER_PAGE = 16; /* 4 columns x 4 rows */
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

    if (editMode) {
      applyEditModeToGrid();
    }

    updatePageIndicator();
    snapToPage(currentPage, false);
  }

  function snapToPage(idx, animate) {
    currentPage = Math.max(0, Math.min(idx, totalPages - 1));
    if (animate !== false) {
      pagesTrack.style.transition = 'transform 0.3s cubic-bezier(0.25, 0.46, 0.45, 0.94)';
    } else {
      pagesTrack.style.transition = 'none';
    }
    pagesTrack.style.transform = 'translateX(-' + (currentPage * 100) + '%)';
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

  /* ═══════════════════════════════════════════════════════
     Swipe — real-time drag feedback + momentum snap
     ═══════════════════════════════════════════════════════ */
  var drag = {
    active: false,
    startX: 0,
    startTime: 0,
    currentX: 0,
    moved: false
  };

  var SWIPE_THRESHOLD = 40;
  var VELOCITY_THRESHOLD = 0.3;

  function onDragStart(x) {
    if (editMode) return;
    drag.active = true;
    drag.startX = x;
    drag.currentX = x;
    drag.startTime = Date.now();
    drag.moved = false;
    pagesTrack.style.transition = 'none';
  }

  function onDragMove(x) {
    if (!drag.active) return;
    drag.currentX = x;
    var dx = x - drag.startX;
    if (Math.abs(dx) > 5) drag.moved = true;

    /* Translate track by drag delta, offset from current page */
    var baseOffset = -(currentPage * 100);
    var viewportWidth = viewport ? viewport.offsetWidth : 393;
    var pxPercent = (dx / viewportWidth) * 100;

    /* Rubber-band at edges */
    if ((currentPage === 0 && dx > 0) || (currentPage >= totalPages - 1 && dx < 0)) {
      pxPercent *= 0.3;
    }

    pagesTrack.style.transform = 'translateX(' + (baseOffset + pxPercent) + '%)';
  }

  function onDragEnd(x) {
    if (!drag.active) return;
    drag.active = false;

    var dx = x - drag.startX;
    var dt = Date.now() - drag.startTime;
    var velocity = Math.abs(dx) / Math.max(dt, 1);

    var nextPage = currentPage;
    if (Math.abs(dx) > SWIPE_THRESHOLD || velocity > VELOCITY_THRESHOLD) {
      if (dx < 0 && currentPage < totalPages - 1) nextPage = currentPage + 1;
      else if (dx > 0 && currentPage > 0) nextPage = currentPage - 1;
    }

    snapToPage(nextPage, true);
  }

  if (viewport) {
    /* Touch events */
    viewport.addEventListener('touchstart', function (e) {
      if (appDrag.active) return;
      onDragStart(e.touches[0].clientX);
    }, { passive: true });

    viewport.addEventListener('touchmove', function (e) {
      if (appDrag.active) return;
      onDragMove(e.touches[0].clientX);
    }, { passive: true });

    viewport.addEventListener('touchend', function (e) {
      if (appDrag.active) return;
      onDragEnd(e.changedTouches[0].clientX);
    });

    /* Mouse events */
    viewport.addEventListener('mousedown', function (e) {
      if (appDrag.active) return;
      onDragStart(e.clientX);
    });

    document.addEventListener('mousemove', function (e) {
      if (drag.active && !appDrag.active) onDragMove(e.clientX);
    });

    document.addEventListener('mouseup', function (e) {
      if (drag.active && !appDrag.active) onDragEnd(e.clientX);
    });
  }

  /* ═══════════════════════════════════════════════════════
     Edit Mode — long press to enter, with delete + drag
     ═══════════════════════════════════════════════════════ */
  var editMode = false;
  var longPressTimer = null;
  var longPressTarget = null;
  var longPressStartPos = { x: 0, y: 0 };

  function applyEditModeToGrid() {
    pagesTrack.classList.add('edit-mode');
    pagesTrack.querySelectorAll('.app-item').forEach(function (el) {
      if (el.querySelector('.app-delete')) return;
      var appId = el.dataset.appId;
      if (UNDELETABLE.indexOf(appId) !== -1) return;
      var delBtn = document.createElement('div');
      delBtn.className = 'app-delete';
      delBtn.textContent = '\u00d7';
      delBtn.addEventListener('click', function (ev) {
        ev.stopPropagation();
        if (UNDELETABLE.indexOf(appId) !== -1) return;
        window.parent.postMessage(JSON.stringify({
          type: 'service.request', service: 'appstore',
          method: 'uninstall', params: { appId: appId }
        }), '*');
        defaultApps = defaultApps.filter(function (a) { return a.id !== appId; });
        renderAppGrid(getGridApps());
      });
      el.appendChild(delBtn);
    });
  }

  function enterEditMode() {
    editMode = true;
    var dock = document.getElementById('dock');
    dock.classList.add('edit-mode');
    applyEditModeToGrid();
  }

  function exitEditMode() {
    editMode = false;
    var dock = document.getElementById('dock');
    dock.classList.remove('edit-mode');
    pagesTrack.classList.remove('edit-mode');
    pagesTrack.querySelectorAll('.app-delete').forEach(function (el) { el.remove(); });
  }

  /* Long press detection — unified for grid and dock */
  function startLongPress(target, x, y) {
    longPressTarget = target;
    longPressStartPos = { x: x, y: y };
    longPressTimer = setTimeout(function () {
      longPressTimer = null;
      if (!editMode) {
        enterEditMode();
      }
      /* Start drag on the long-pressed element */
      startAppDrag(longPressTarget, longPressStartPos.x, longPressStartPos.y);
    }, 800);
  }

  function cancelLongPress() {
    if (longPressTimer) {
      clearTimeout(longPressTimer);
      longPressTimer = null;
    }
  }

  /* Grid long press: mouse */
  pagesTrack.addEventListener('mousedown', function (e) {
    var appItem = e.target.closest('.app-item');
    if (!appItem) return;
    startLongPress(appItem, e.clientX, e.clientY);
  });
  pagesTrack.addEventListener('mouseup', cancelLongPress);
  pagesTrack.addEventListener('mouseleave', cancelLongPress);

  /* Grid long press: touch */
  pagesTrack.addEventListener('touchstart', function (e) {
    var appItem = e.target.closest('.app-item');
    if (!appItem) return;
    startLongPress(appItem, e.touches[0].clientX, e.touches[0].clientY);
  }, { passive: true });
  pagesTrack.addEventListener('touchmove', function (e) {
    if (longPressTimer && e.touches.length > 0) {
      var dx = Math.abs(e.touches[0].clientX - longPressStartPos.x);
      var dy = Math.abs(e.touches[0].clientY - longPressStartPos.y);
      if (dx > 10 || dy > 10) cancelLongPress();
    }
  }, { passive: true });
  pagesTrack.addEventListener('touchend', cancelLongPress);

  /* Dock long press: mouse */
  var dockEl = document.getElementById('dock');
  dockEl.addEventListener('mousedown', function (e) {
    var dockApp = e.target.closest('.dock-app');
    if (!dockApp) return;
    startLongPress(dockApp, e.clientX, e.clientY);
  });
  dockEl.addEventListener('mouseup', cancelLongPress);
  dockEl.addEventListener('mouseleave', cancelLongPress);

  /* Dock long press: touch */
  dockEl.addEventListener('touchstart', function (e) {
    var dockApp = e.target.closest('.dock-app');
    if (!dockApp) return;
    startLongPress(dockApp, e.touches[0].clientX, e.touches[0].clientY);
  }, { passive: true });
  dockEl.addEventListener('touchmove', function (e) {
    if (longPressTimer && e.touches.length > 0) {
      var dx = Math.abs(e.touches[0].clientX - longPressStartPos.x);
      var dy = Math.abs(e.touches[0].clientY - longPressStartPos.y);
      if (dx > 10 || dy > 10) cancelLongPress();
    }
  }, { passive: true });
  dockEl.addEventListener('touchend', cancelLongPress);

  /* Exit edit mode on background tap */
  document.addEventListener('click', function (e) {
    if (editMode && !appDrag.active &&
        !e.target.closest('.app-item') && !e.target.closest('.dock-app') &&
        !e.target.closest('.app-delete')) {
      exitEditMode();
    }
  });

  /* ═══════════════════════════════════════════════════════
     App Drag — drag apps between dock and grid
     ═══════════════════════════════════════════════════════ */
  var appDrag = {
    active: false,
    appId: null,
    source: null,       /* 'grid' or 'dock' */
    clone: null,
    origEl: null,
    offsetX: 0,
    offsetY: 0
  };

  function startAppDrag(targetEl, x, y) {
    var appId = targetEl.dataset.appId || targetEl.dataset.app;
    if (!appId) return;

    var isDock = !!targetEl.closest('#dock');
    appDrag.active = true;
    appDrag.appId = appId;
    appDrag.source = isDock ? 'dock' : 'grid';
    appDrag.origEl = targetEl;

    /* Cancel any page swipe in progress */
    drag.active = false;

    /* Dim original */
    targetEl.classList.add('app-drag-placeholder');

    /* Create clone */
    var rect = targetEl.getBoundingClientRect();
    var clone = targetEl.cloneNode(true);
    clone.className = (isDock ? 'dock-app' : 'app-item') + ' app-drag-clone';
    clone.style.width = rect.width + 'px';
    clone.style.left = rect.left + 'px';
    clone.style.top = rect.top + 'px';

    appDrag.offsetX = x - rect.left;
    appDrag.offsetY = y - rect.top;

    document.body.appendChild(clone);
    appDrag.clone = clone;

    /* Remove delete buttons from clone */
    var delInClone = clone.querySelector('.app-delete');
    if (delInClone) delInClone.remove();
  }

  function moveAppDrag(x, y) {
    if (!appDrag.active || !appDrag.clone) return;
    appDrag.clone.style.left = (x - appDrag.offsetX) + 'px';
    appDrag.clone.style.top = (y - appDrag.offsetY) + 'px';

    /* Check if over dock */
    var dock = document.getElementById('dock');
    var dockRect = dock.getBoundingClientRect();
    var overDock = (x >= dockRect.left && x <= dockRect.right &&
                   y >= dockRect.top && y <= dockRect.bottom);

    if (overDock) {
      dock.classList.add('drag-over');
    } else {
      dock.classList.remove('drag-over');
    }
  }

  function endAppDrag(x, y) {
    if (!appDrag.active) return;

    var dock = document.getElementById('dock');
    dock.classList.remove('drag-over');

    var dockRect = dock.getBoundingClientRect();
    var overDock = (x >= dockRect.left && x <= dockRect.right &&
                   y >= dockRect.top && y <= dockRect.bottom);

    var changed = false;

    if (appDrag.source === 'grid' && overDock) {
      /* Grid -> Dock: add to dock if under limit */
      if (dockApps.length < 5) {
        dockApps.push(appDrag.appId);
        saveDockSettings();
        changed = true;
      }
    } else if (appDrag.source === 'dock' && !overDock) {
      /* Dock -> Grid: remove from dock if above minimum */
      if (dockApps.length > 1) {
        dockApps = dockApps.filter(function (id) { return id !== appDrag.appId; });
        saveDockSettings();
        changed = true;
      }
    }

    /* Clean up */
    if (appDrag.clone) {
      if (changed) {
        appDrag.clone.remove();
      } else {
        /* Animate back to original position */
        var origRect = appDrag.origEl.getBoundingClientRect();
        appDrag.clone.style.transition = 'left 0.25s ease, top 0.25s ease, transform 0.25s ease';
        appDrag.clone.style.left = origRect.left + 'px';
        appDrag.clone.style.top = origRect.top + 'px';
        appDrag.clone.style.transform = 'scale(1)';
        var cloneRef = appDrag.clone;
        setTimeout(function () {
          cloneRef.remove();
        }, 260);
      }
    }

    if (appDrag.origEl) {
      appDrag.origEl.classList.remove('app-drag-placeholder');
    }

    appDrag.active = false;
    appDrag.appId = null;
    appDrag.source = null;
    appDrag.clone = null;
    appDrag.origEl = null;

    if (changed) {
      renderDock();
      renderAppGrid(getGridApps());
    }
  }

  /* Global mouse/touch move & end for drag */
  document.addEventListener('mousemove', function (e) {
    if (appDrag.active) {
      e.preventDefault();
      moveAppDrag(e.clientX, e.clientY);
    }
  });

  document.addEventListener('mouseup', function (e) {
    if (appDrag.active) {
      endAppDrag(e.clientX, e.clientY);
    }
  });

  document.addEventListener('touchmove', function (e) {
    if (appDrag.active) {
      e.preventDefault();
      moveAppDrag(e.touches[0].clientX, e.touches[0].clientY);
    }
  }, { passive: false });

  document.addEventListener('touchend', function (e) {
    if (appDrag.active) {
      var touch = e.changedTouches[0];
      endAppDrag(touch.clientX, touch.clientY);
    }
  });

  /* ─── App launch (click, not drag) ─── */
  pagesTrack.addEventListener('click', function (e) {
    if (editMode) return;
    if (drag.moved) return; /* Prevent launch after swipe */
    var appItem = e.target.closest('.app-item');
    if (!appItem) return;
    var appId = appItem.dataset.appId;
    if (appId) launchApp(appId, appItem);
  });

  function launchApp(appId, el) {
    if (el) {
      el.classList.add('launching');
      setTimeout(function () { el.classList.remove('launching'); }, 500);
    }
    ZylBridge.launch(appId);
  }

  /* ─── Search ─── */
  var searchInput = document.getElementById('search-input');
  if (searchInput) {
    searchInput.addEventListener('input', function () {
      var query = searchInput.value.toLowerCase().trim();
      if (!query) { renderAppGrid(getGridApps()); return; }
      var filtered = getGridApps().filter(function (app) {
        var name = zylI18n.t(app.nameKey).toLowerCase();
        return name.indexOf(query) !== -1 || app.id.toLowerCase().indexOf(query) !== -1;
      });
      renderAppGrid(filtered);
    });
  }

  /* ─── Re-render on locale change ─── */
  zylI18n.onLocaleChange(function () {
    renderDock();
    renderAppGrid(getGridApps());
  });

  /* ─── Initial loading state ─── */
  if (pagesTrack && defaultApps.length === 0) {
    pagesTrack.innerHTML = '<div class="app-page"><div style="text-align:center;opacity:0.5;padding:32px;grid-column:1/-1">Loading...</div></div>';
  }

})();
