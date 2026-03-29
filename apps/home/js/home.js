// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Home screen UI — app grid with page swipe, dynamic dock,
//       search, clock, drag-and-drop between dock and grid
// Scope: App icon rendering, multi-page swipe with drag feedback,
//        search filtering, edit mode (long press) with app deletion,
//        position-aware dock ↔ grid drag-and-drop with insert indicators,
//        dock reorder, grid reorder with order persistence, settings persistence
// Dependency: zylI18n (i18n.js), ZylClock (clock.js), ZylBridge (bridge.js)
// SOLID: SRP — home screen UI rendering and interaction only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── SVG Icon Helper ─── */
  /* Icons are loaded from app.iconSvg (sourced from each app's app.json).
     OS policy (default dock, hidden-from-grid, undeletable) comes from ZylAppRegistry. */
  var _FALLBACK_ICON = '<svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2z"/></svg>';

  function getIconSvg(app) {
    if (app && app.iconSvg) {
      return '<svg viewBox="0 0 24 24"><path d="' + app.iconSvg + '"/></svg>';
    }
    return _FALLBACK_ICON;
  }

  /* ─── Registry Access ─── */
  /* Home runs inside an iframe; ZylAppRegistry lives in the parent (emulator) window.
     Access via window.parent for same-origin iframe context. */
  function _registry() {
    try {
      if (typeof window.parent !== 'undefined' && window.parent.ZylAppRegistry) return window.parent.ZylAppRegistry;
    } catch (e) { /* cross-origin guard */ }
    return null;
  }

  /* ─── Data Model ─── */
  var reg = _registry();
  var dockApps = (reg && reg.getDefaultDock)
    ? reg.getDefaultDock()
    : ['com.zylos.phone', 'com.zylos.contacts', 'com.zylos.messages', 'com.zylos.settings'];
  var defaultApps = [];
  var gridOrder = []; // saved order of grid app IDs
  var appListReceived = false;
  var dockSettingsLoaded = false;

  /* OS policy queries — delegated to ZylAppRegistry (apps/system/app-registry.js).
     Home screen does not define these policies; it only consumes them. */
  function isHiddenFromGrid(appId) {
    var r = _registry();
    return r ? r.isHiddenFromGrid(appId) : false;
  }
  function isUndeletable(appId) {
    var r = _registry();
    return r ? r.isUndeletable(appId) : true;
  }

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
    var grid = defaultApps.filter(function (app) {
      return dockApps.indexOf(app.id) === -1;
    });
    if (gridOrder.length > 0) {
      grid.sort(function (a, b) {
        var ia = gridOrder.indexOf(a.id);
        var ib = gridOrder.indexOf(b.id);
        if (ia === -1) ia = 9999;
        if (ib === -1) ib = 9999;
        return ia - ib;
      });
    }
    return grid;
  }

  function updateGridOrder() {
    gridOrder = getGridApps().map(function (a) { return a.id; });
    saveGridSettings();
  }

  function saveGridSettings() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'settings', method: 'update',
      params: { category: 'home', key: 'gridOrder', value: gridOrder.join(',') }
    }), '*');
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

    var iconWrap = document.createElement('div');
    iconWrap.className = 'dock-icon ' + app.color;
    iconWrap.innerHTML = getIconSvg(app);

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
          return !isHiddenFromGrid(app.id);
        });
        renderDock();
        renderAppGrid(getGridApps());
      }

      /* Settings response — dock + grid order config */
      if (msg.type === 'service.response' && msg.service === 'settings' && msg.data) {
        if (msg.data.dock) {
          var saved = msg.data.dock.split(',').filter(Boolean);
          if (saved.length > 0) {
            dockApps = saved;
          }
        }
        if (msg.data.gridOrder) {
          var savedGrid = msg.data.gridOrder.split(',').filter(Boolean);
          if (savedGrid.length > 0) {
            gridOrder = savedGrid;
          }
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

        var name = zylI18n.t(app.nameKey);

        var iconWrap = document.createElement('div');
        iconWrap.className = 'app-icon-wrap ' + app.color;
        iconWrap.innerHTML = getIconSvg(app);

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

    if (viewport && typeof ZylTouch !== 'undefined' && !pageSwipe) {
      pageSwipe = ZylTouch.createPageSwipe(viewport, pagesTrack, {
        totalPages: totalPages,
        threshold: 40,
        velocityThreshold: 0.3,
        rubberBand: true,
        onPageChange: function (page) {
          currentPage = page;
          updatePageIndicator();
        }
      });
    }
    if (pageSwipe) {
      pageSwipe.setTotalPages(totalPages);
      pageSwipe.snapTo(currentPage, false);
    }
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
     Swipe — delegated to ZylTouch.createPageSwipe
     ═══════════════════════════════════════════════════════ */
  var pageSwipe = null;

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
      if (isUndeletable(appId)) return;
      var delBtn = document.createElement('div');
      delBtn.className = 'app-delete';
      delBtn.textContent = '\u00d7';
      delBtn.addEventListener('click', function (ev) {
        ev.stopPropagation();
        if (isUndeletable(appId)) return;
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

    /* Cancel any page swipe in progress — pageSwipe handles its own state */

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

  /* ─── Insert line indicator ─── */
  var insertLine = document.createElement('div');
  insertLine.id = 'drag-insert-line';
  insertLine.style.cssText = 'position:fixed;width:3px;background:#4a9eff;border-radius:2px;z-index:10000;pointer-events:none;display:none;transition:left 0.1s,top 0.1s';
  document.body.appendChild(insertLine);

  function getDockInsertIndex(x) {
    var dockItems = document.querySelectorAll('#dock .dock-app:not(.app-drag-placeholder)');
    for (var i = 0; i < dockItems.length; i++) {
      var rect = dockItems[i].getBoundingClientRect();
      var midX = rect.left + rect.width / 2;
      if (x < midX) return i;
    }
    return dockItems.length;
  }

  function getGridInsertIndex(x, y) {
    var gridItems = pagesTrack.querySelectorAll('.app-item:not(.app-drag-placeholder)');
    for (var i = 0; i < gridItems.length; i++) {
      var rect = gridItems[i].getBoundingClientRect();
      var midX = rect.left + rect.width / 2;
      var midY = rect.top + rect.height / 2;
      if (y < midY + rect.height / 2 && x < midX) return i;
    }
    return gridItems.length;
  }

  function showDockInsertLine(x) {
    var dockItems = document.querySelectorAll('#dock .dock-app:not(.app-drag-placeholder)');
    var dockEl = document.getElementById('dock');
    var dockRect = dockEl.getBoundingClientRect();
    var lineX;
    var idx = getDockInsertIndex(x);

    if (dockItems.length === 0) {
      lineX = dockRect.left + dockRect.width / 2;
    } else if (idx >= dockItems.length) {
      var lastRect = dockItems[dockItems.length - 1].getBoundingClientRect();
      lineX = lastRect.right + 2;
    } else {
      var itemRect = dockItems[idx].getBoundingClientRect();
      lineX = itemRect.left - 2;
    }

    insertLine.style.display = 'block';
    insertLine.style.left = (lineX - 1) + 'px';
    insertLine.style.top = dockRect.top + 4 + 'px';
    insertLine.style.height = (dockRect.height - 8) + 'px';
  }

  function hideInsertLine() {
    insertLine.style.display = 'none';
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
      showDockInsertLine(x);
    } else {
      dock.classList.remove('drag-over');
      hideInsertLine();
    }
  }

  function endAppDrag(x, y) {
    if (!appDrag.active) return;

    var dock = document.getElementById('dock');
    dock.classList.remove('drag-over');
    hideInsertLine();

    var dockRect = dock.getBoundingClientRect();
    var overDock = (x >= dockRect.left && x <= dockRect.right &&
                   y >= dockRect.top && y <= dockRect.bottom);

    var changed = false;

    if (appDrag.source === 'grid' && overDock) {
      /* Grid -> Dock: insert at position if under limit */
      if (dockApps.length < 5) {
        var dockIdx = getDockInsertIndex(x);
        dockApps.splice(dockIdx, 0, appDrag.appId);
        saveDockSettings();
        updateGridOrder();
        changed = true;
      }
    } else if (appDrag.source === 'dock' && overDock) {
      /* Dock -> Dock: reorder within dock */
      var oldIdx = dockApps.indexOf(appDrag.appId);
      if (oldIdx !== -1) {
        dockApps.splice(oldIdx, 1);
        var newIdx = getDockInsertIndex(x);
        dockApps.splice(newIdx, 0, appDrag.appId);
        saveDockSettings();
        changed = true;
      }
    } else if (appDrag.source === 'dock' && !overDock) {
      /* Dock -> Grid: remove from dock, insert at grid position */
      if (dockApps.length > 1) {
        dockApps = dockApps.filter(function (id) { return id !== appDrag.appId; });
        saveDockSettings();
        var gridIdx = getGridInsertIndex(x, y);
        /* Insert into gridOrder at position */
        var currentGrid = getGridApps().map(function (a) { return a.id; });
        if (gridIdx > currentGrid.length) gridIdx = currentGrid.length;
        currentGrid.splice(gridIdx, 0, appDrag.appId);
        gridOrder = currentGrid;
        saveGridSettings();
        changed = true;
      }
    } else if (appDrag.source === 'grid' && !overDock) {
      /* Grid -> Grid: reorder within grid */
      var currentGridApps = getGridApps().map(function (a) { return a.id; });
      var fromIdx = currentGridApps.indexOf(appDrag.appId);
      if (fromIdx !== -1) {
        currentGridApps.splice(fromIdx, 1);
        var toIdx = getGridInsertIndex(x, y);
        if (toIdx > currentGridApps.length) toIdx = currentGridApps.length;
        currentGridApps.splice(toIdx, 0, appDrag.appId);
        gridOrder = currentGridApps;
        saveGridSettings();
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
    if (pageSwipe && pageSwipe.hasMoved()) return; /* Prevent launch after swipe */
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
