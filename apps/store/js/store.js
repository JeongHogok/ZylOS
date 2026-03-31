// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 앱스토어 — 설치 가능한 앱 목록 조회 및 설치/제거 관리
// 수행범위: appstore.getAvailable, apps.getInstalled 서비스 조회,
//           appstore.install / appstore.uninstall 서비스 호출,
//           검색 필터링, 탭 전환, 카테고리 필터, 앱 상세 보기,
//           설치/업데이트 진행 상태, 앱 카드 렌더링
// 의존방향: appstore / apps 서비스 (postMessage IPC), ZylBridge (bridge.js)
// SOLID: SRP — 앱스토어 UI 렌더링과 사용자 인터랙션만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── DOM refs ─── */
  var appListEl = document.getElementById('app-list');
  var emptyStateEl = document.getElementById('empty-state');
  var loadingStateEl = document.getElementById('loading-state');
  var searchInput = document.getElementById('search-input');
  var tabAll = document.getElementById('tab-all');
  var tabInstalled = document.getElementById('tab-installed');
  var categoryBarEl = document.getElementById('category-bar');
  var detailOverlayEl = document.getElementById('detail-overlay');
  var detailContentEl = document.getElementById('detail-content');
  var detailBackBtn = document.getElementById('detail-back');
  var detailTitleEl = document.getElementById('detail-title');

  /* ─── State ─── */
  var availableApps = [];       // from appstore.getAvailable
  var installedSet = {};        // id -> true, from apps.getInstalled
  var installedVersions = {};   // id -> version string, from apps.getInstalled
  var activeTab = 'all';        // 'all' | 'installed'
  var searchQuery = '';
  var activeCategory = '';      // '' means all categories
  var categories = [];          // unique category strings from app data
  var pendingActions = {};      // appId -> 'installing' | 'uninstalling' | 'updating'
  var detailApp = null;         // currently viewed app in detail, or null
  var dataReady = { available: false, installed: false };

  /* ─── XSS protection ─── */
  function escapeHtml(str) {
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(str || ''));
    return div.innerHTML;
  }

  /* ─── IPC helpers ─── */
  function sendRequest(service, method, data) {
    var msg = {
      type: 'service.request',
      service: service,
      method: method
    };
    if (data !== undefined) {
      msg.data = data;
    }
    ZylBridge.sendToSystem(msg);
  }

  function requestData() {
    dataReady.available = false;
    dataReady.installed = false;
    showLoading(true);
    sendRequest('appstore', 'getAvailable');
    sendRequest('apps', 'getInstalled');
  }

  /* ─── Message handler ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    var msg;
    try {
      msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
    } catch (err) {
      // Malformed message, ignore
      return;
    }
    if (!msg) return;

    /* Navigation back handling */
    if (msg.type === 'navigation.back') {
      if (detailApp) {
        closeDetail();
        return;
      }
      ZylBridge.sendToSystem({ type: 'navigation.exit' });
      return;
    }

    if (msg.type !== 'service.response') return;

    if (msg.service === 'appstore' && msg.method === 'getAvailable' && msg.data) {
      availableApps = Array.isArray(msg.data) ? msg.data : [];
      dataReady.available = true;
      extractCategories();
      onDataLoaded();
    }

    if (msg.service === 'apps' && msg.method === 'getInstalled' && msg.data) {
      var list = Array.isArray(msg.data) ? msg.data : [];
      installedSet = {};
      installedVersions = {};
      for (var i = 0; i < list.length; i++) {
        var item = list[i];
        var id = item.id || item;
        installedSet[id] = true;
        if (item.version) {
          installedVersions[id] = item.version;
        }
      }
      dataReady.installed = true;
      onDataLoaded();
    }

    // Handle install/uninstall responses
    if (msg.service === 'appstore' && (msg.method === 'install' || msg.method === 'uninstall')) {
      // Clear pending state for the relevant app
      if (msg.data && msg.data.appId) {
        delete pendingActions[msg.data.appId];
      } else {
        // If no appId in response, clear all pending
        pendingActions = {};
      }
      requestData();
    }
  });

  function onDataLoaded() {
    if (dataReady.available && dataReady.installed) {
      showLoading(false);
      renderCategoryBar();
      render();
      // Update detail view if open
      if (detailApp) {
        var updated = findAppById(detailApp.id);
        if (updated) {
          detailApp = updated;
          renderDetailContent(updated);
        }
      }
    }
  }

  /* ─── Categories ─── */
  function extractCategories() {
    var catMap = {};
    for (var i = 0; i < availableApps.length; i++) {
      var cat = availableApps[i].category;
      if (cat && typeof cat === 'string') {
        catMap[cat] = true;
      }
    }
    categories = [];
    for (var key in catMap) {
      if (catMap.hasOwnProperty(key)) {
        categories.push(key);
      }
    }
    categories.sort();
  }

  function renderCategoryBar() {
    if (!categoryBarEl) return;
    categoryBarEl.innerHTML = '';

    if (categories.length === 0) return;

    // "All" chip
    var allChip = document.createElement('span');
    allChip.className = activeCategory === '' ? 'category-chip active' : 'category-chip';
    allChip.textContent = zylI18n.t('store.all');
    allChip.addEventListener('click', function () {
      activeCategory = '';
      renderCategoryBar();
      render();
    });
    categoryBarEl.appendChild(allChip);

    // Category chips
    for (var i = 0; i < categories.length; i++) {
      (function (cat) {
        var chip = document.createElement('span');
        chip.className = activeCategory === cat ? 'category-chip active' : 'category-chip';
        chip.textContent = cat;
        chip.addEventListener('click', function () {
          activeCategory = cat;
          renderCategoryBar();
          render();
        });
        categoryBarEl.appendChild(chip);
      })(categories[i]);
    }
  }

  /* ─── Loading / Empty states ─── */
  function showLoading(show) {
    if (loadingStateEl) {
      loadingStateEl.className = show ? '' : 'hidden';
    }
  }

  function showEmpty(show) {
    if (emptyStateEl) {
      emptyStateEl.className = show ? '' : 'hidden';
    }
  }

  /* ─── Update detection ─── */
  function hasUpdate(app) {
    if (!installedSet[app.id]) return false;
    var installedVer = installedVersions[app.id];
    if (!installedVer || !app.version) return false;
    return installedVer !== app.version;
  }

  /* ─── Find app by id ─── */
  function findAppById(id) {
    for (var i = 0; i < availableApps.length; i++) {
      if (availableApps[i].id === id) return availableApps[i];
    }
    return null;
  }

  /* ─── Filtering ─── */
  function getFilteredApps() {
    var list = [];
    var q = searchQuery.toLowerCase();

    for (var i = 0; i < availableApps.length; i++) {
      var app = availableApps[i];
      var isInstalled = !!installedSet[app.id];

      // Tab filter
      if (activeTab === 'installed' && !isInstalled) continue;

      // Category filter
      if (activeCategory && (app.category || '') !== activeCategory) continue;

      // Search filter
      if (q) {
        var name = (app.name || app.id || '').toLowerCase();
        var desc = (app.description || '').toLowerCase();
        if (name.indexOf(q) === -1 && desc.indexOf(q) === -1) continue;
      }

      list.push(app);
    }

    return list;
  }

  /* ─── Render list ─── */
  function render() {
    if (!appListEl) return;
    appListEl.innerHTML = '';

    var filtered = getFilteredApps();

    if (filtered.length === 0) {
      showEmpty(true);
      return;
    }

    showEmpty(false);

    for (var i = 0; i < filtered.length; i++) {
      var card = createAppCard(filtered[i]);
      appListEl.appendChild(card);
    }
  }

  function createAppCard(app) {
    var isInstalled = !!installedSet[app.id];
    var isSystem = !!app.system;
    var updateAvailable = hasUpdate(app);
    var pending = pendingActions[app.id] || null;

    var card = document.createElement('div');
    card.className = 'app-card';

    /* Tap to open detail */
    (function (a) {
      card.addEventListener('click', function (e) {
        // Don't open detail if button was clicked
        if (e.target && e.target.tagName === 'BUTTON') return;
        openDetail(a);
      });
    })(app);

    /* Icon placeholder */
    var iconEl = document.createElement('div');
    iconEl.className = 'app-icon';
    var initial = (app.name || app.id || '?').charAt(0).toUpperCase();
    iconEl.textContent = initial;
    card.appendChild(iconEl);

    /* Info block */
    var infoEl = document.createElement('div');
    infoEl.className = 'app-info';

    var nameRow = document.createElement('div');
    nameRow.className = 'app-name-row';

    var nameEl = document.createElement('span');
    nameEl.className = 'app-name';
    nameEl.textContent = app.name || app.id || '';
    nameRow.appendChild(nameEl);

    if (isSystem) {
      var badge = document.createElement('span');
      badge.className = 'badge-system';
      badge.setAttribute('data-i18n', 'store.system');
      badge.textContent = zylI18n.t('store.system_app');
      nameRow.appendChild(badge);
    }

    if (updateAvailable && !pending) {
      var updateBadge = document.createElement('span');
      updateBadge.className = 'badge-update';
      updateBadge.setAttribute('data-i18n', 'store.update');
      updateBadge.textContent = zylI18n.t('store.update');
      nameRow.appendChild(updateBadge);
    }

    infoEl.appendChild(nameRow);

    if (app.version) {
      var verEl = document.createElement('span');
      verEl.className = 'app-version';
      verEl.textContent = 'v' + app.version;
      infoEl.appendChild(verEl);
    }

    if (app.description) {
      var descEl = document.createElement('div');
      descEl.className = 'app-desc';
      descEl.textContent = app.description;
      infoEl.appendChild(descEl);
    }

    card.appendChild(infoEl);

    /* Action button */
    var btnEl = createActionButton(app, isInstalled, isSystem, updateAvailable, pending);
    card.appendChild(btnEl);

    return card;
  }

  function createActionButton(app, isInstalled, isSystem, updateAvailable, pending) {
    var btnEl = document.createElement('button');
    btnEl.className = 'app-action';

    if (pending) {
      btnEl.className += ' btn-progress';
      btnEl.disabled = true;
      var spinner = document.createElement('span');
      spinner.className = 'spinner';
      btnEl.appendChild(spinner);
      var label = document.createElement('span');
      if (pending === 'installing') {
        label.textContent = zylI18n.t('store.installing');
      } else if (pending === 'uninstalling') {
        label.textContent = zylI18n.t('store.uninstalling');
      } else if (pending === 'updating') {
        label.textContent = zylI18n.t('store.updating');
      }
      btnEl.appendChild(label);
      return btnEl;
    }

    if (isInstalled) {
      if (updateAvailable && !isSystem) {
        btnEl.className += ' btn-update';
        btnEl.textContent = zylI18n.t('store.update');
        btnEl.addEventListener('click', (function (appId) {
          return function (e) {
            e.stopPropagation();
            pendingActions[appId] = 'updating';
            render();
            refreshDetailIfOpen(appId);
            sendRequest('appstore', 'install', { appId: appId });
          };
        })(app.id));
      } else if (isSystem) {
        btnEl.className += ' btn-system';
        btnEl.setAttribute('data-i18n', 'store.installed');
        btnEl.textContent = zylI18n.t('store.installed');
        btnEl.disabled = true;
      } else {
        btnEl.className += ' btn-uninstall';
        btnEl.setAttribute('data-i18n', 'store.uninstall');
        btnEl.textContent = zylI18n.t('store.uninstall');
        btnEl.addEventListener('click', (function (appId) {
          return function (e) {
            e.stopPropagation();
            pendingActions[appId] = 'uninstalling';
            render();
            refreshDetailIfOpen(appId);
            sendRequest('appstore', 'uninstall', { appId: appId });
          };
        })(app.id));
      }
    } else {
      btnEl.className += ' btn-install';
      btnEl.setAttribute('data-i18n', 'store.install');
      btnEl.textContent = zylI18n.t('store.install');
      btnEl.addEventListener('click', (function (appId) {
        return function (e) {
          e.stopPropagation();
          pendingActions[appId] = 'installing';
          render();
          refreshDetailIfOpen(appId);
          sendRequest('appstore', 'install', { appId: appId });
        };
      })(app.id));
    }

    return btnEl;
  }

  /* ─── Detail view ─── */
  function openDetail(app) {
    detailApp = app;
    if (detailOverlayEl) {
      detailOverlayEl.className = '';
    }
    renderDetailContent(app);
  }

  function closeDetail() {
    detailApp = null;
    if (detailOverlayEl) {
      detailOverlayEl.className = 'hidden';
    }
  }

  function refreshDetailIfOpen(appId) {
    if (detailApp && detailApp.id === appId) {
      renderDetailContent(detailApp);
    }
  }

  function renderDetailContent(app) {
    if (!detailContentEl) return;
    detailContentEl.innerHTML = '';

    var isInstalled = !!installedSet[app.id];
    var isSystem = !!app.system;
    var updateAvailable = hasUpdate(app);
    var pending = pendingActions[app.id] || null;

    /* App header: icon + name + version */
    var headerDiv = document.createElement('div');
    headerDiv.className = 'detail-app-header';

    var iconEl = document.createElement('div');
    iconEl.className = 'detail-app-icon';
    iconEl.textContent = (app.name || app.id || '?').charAt(0).toUpperCase();
    headerDiv.appendChild(iconEl);

    var headerInfo = document.createElement('div');
    var nameEl = document.createElement('div');
    nameEl.className = 'detail-app-name';
    nameEl.textContent = app.name || app.id || '';
    headerInfo.appendChild(nameEl);

    if (app.version) {
      var verEl = document.createElement('div');
      verEl.className = 'detail-app-version';
      verEl.textContent = 'v' + app.version;
      headerInfo.appendChild(verEl);
    }
    headerDiv.appendChild(headerInfo);
    detailContentEl.appendChild(headerDiv);

    /* Metadata block */
    var metaDiv = document.createElement('div');
    metaDiv.className = 'detail-meta';

    // Version row
    if (app.version) {
      metaDiv.appendChild(createMetaRow(zylI18n.t('store.version'), app.version));
    }
    // Category row
    if (app.category) {
      metaDiv.appendChild(createMetaRow(zylI18n.t('store.category'), app.category));
    }
    // Size row
    if (app.size) {
      metaDiv.appendChild(createMetaRow(zylI18n.t('store.size'), app.size));
    }

    if (metaDiv.childNodes.length > 0) {
      detailContentEl.appendChild(metaDiv);
    }

    /* Description */
    var descLabel = document.createElement('div');
    descLabel.className = 'detail-description-label';
    descLabel.textContent = zylI18n.t('store.description');
    detailContentEl.appendChild(descLabel);

    var descText = document.createElement('div');
    descText.className = 'detail-description-text';
    descText.textContent = app.description
      ? app.description
      : zylI18n.t('store.no_description');
    detailContentEl.appendChild(descText);

    /* Action button */
    var actionArea = document.createElement('div');
    actionArea.className = 'detail-action-area';

    var btnEl = document.createElement('button');
    btnEl.className = 'detail-action-btn';

    if (pending) {
      btnEl.className += ' btn-progress';
      btnEl.disabled = true;
      var spinner = document.createElement('span');
      spinner.className = 'spinner';
      btnEl.appendChild(spinner);
      var labelSpan = document.createElement('span');
      if (pending === 'installing') {
        labelSpan.textContent = zylI18n.t('store.installing');
      } else if (pending === 'uninstalling') {
        labelSpan.textContent = zylI18n.t('store.uninstalling');
      } else if (pending === 'updating') {
        labelSpan.textContent = zylI18n.t('store.updating');
      }
      btnEl.appendChild(labelSpan);
    } else if (isInstalled) {
      if (updateAvailable && !isSystem) {
        btnEl.className += ' btn-update';
        btnEl.textContent = zylI18n.t('store.update');
        btnEl.addEventListener('click', (function (appId) {
          return function () {
            pendingActions[appId] = 'updating';
            render();
            renderDetailContent(findAppById(appId) || app);
            sendRequest('appstore', 'install', { appId: appId });
          };
        })(app.id));
      } else if (isSystem) {
        btnEl.className += ' btn-system';
        btnEl.textContent = zylI18n.t('store.installed');
        btnEl.disabled = true;
      } else {
        btnEl.className += ' btn-uninstall';
        btnEl.textContent = zylI18n.t('store.uninstall');
        btnEl.addEventListener('click', (function (appId) {
          return function () {
            pendingActions[appId] = 'uninstalling';
            render();
            renderDetailContent(findAppById(appId) || app);
            sendRequest('appstore', 'uninstall', { appId: appId });
          };
        })(app.id));
      }
    } else {
      btnEl.className += ' btn-install';
      btnEl.textContent = zylI18n.t('store.install');
      btnEl.addEventListener('click', (function (appId) {
        return function () {
          pendingActions[appId] = 'installing';
          render();
          renderDetailContent(findAppById(appId) || app);
          sendRequest('appstore', 'install', { appId: appId });
        };
      })(app.id));
    }

    actionArea.appendChild(btnEl);
    detailContentEl.appendChild(actionArea);
  }

  function createMetaRow(label, value) {
    var row = document.createElement('div');
    row.className = 'detail-meta-row';

    var labelEl = document.createElement('span');
    labelEl.className = 'detail-meta-label';
    labelEl.textContent = label;
    row.appendChild(labelEl);

    var valueEl = document.createElement('span');
    valueEl.className = 'detail-meta-value';
    valueEl.textContent = value;
    row.appendChild(valueEl);

    return row;
  }

  /* ─── Detail back button ─── */
  if (detailBackBtn) {
    detailBackBtn.addEventListener('click', function () {
      closeDetail();
    });
  }

  /* Close detail when clicking overlay background */
  if (detailOverlayEl) {
    detailOverlayEl.addEventListener('click', function (e) {
      if (e.target === detailOverlayEl) {
        closeDetail();
      }
    });
  }

  /* ─── Tab switching ─── */
  function setActiveTab(tab) {
    activeTab = tab;

    if (tabAll) {
      tabAll.className = tab === 'all' ? 'tab active' : 'tab';
    }
    if (tabInstalled) {
      tabInstalled.className = tab === 'installed' ? 'tab active' : 'tab';
    }

    render();
  }

  if (tabAll) {
    tabAll.addEventListener('click', function () { setActiveTab('all'); });
  }
  if (tabInstalled) {
    tabInstalled.addEventListener('click', function () { setActiveTab('installed'); });
  }

  /* ─── Search ─── */
  if (searchInput) {
    searchInput.addEventListener('input', function () {
      searchQuery = searchInput.value || '';
      render();
    });
  }

  /* ─── Init ─── */
  requestData();
})();
