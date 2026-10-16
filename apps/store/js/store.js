// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 앱스토어 — 스토어 서버의 앱 목록과 설치·업데이트·삭제
// 수행범위: appstore.getCatalog(상태: ok·unconfigured·unreachable·untrusted),
//           appstore.install / uninstall 결과(reason)에 맞는 안내,
//           파일 앱이 연 앱 패키지(.zpkg, VIEW 인텐트)를 확인 후 appstore.installFile,
//           검색 필터링, 탭 전환, 카테고리 필터, 앱 상세(개발자·요구 권한·크기),
//           설치/업데이트 진행 상태, 앱 카드 렌더링
// 의존방향: appstore 서비스 (ZylBridge.requestService), ZylToast, zylI18n
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
  var availableApps = [];       // from appstore.getCatalog
  var storeState = 'ok';        // ok | unconfigured | unreachable | untrusted | invalid
  var installedSet = {};        // id -> true, installed store apps
  var installedVersions = {};   // id -> installed version
  var activeTab = 'all';        // 'all' | 'installed'
  var searchQuery = '';
  var activeCategory = '';      // '' means all categories
  var categories = [];          // unique category strings from app data
  var pendingActions = {};      // appId -> 'installing' | 'uninstalling' | 'updating'
  var detailApp = null;         // currently viewed app in detail, or null

  /* ─── Data: the store's catalog (installed state included) ─── */
  function requestData() {
    showLoading(true);
    ZylBridge.requestService('appstore', 'getCatalog', {}).then(function (c) {
      storeState = (c && c.state) || 'invalid';
      availableApps = (c && Array.isArray(c.apps)) ? c.apps : [];
    }, function () {
      storeState = 'unreachable';
      availableApps = [];
    }).then(function () {
      installedSet = {};
      installedVersions = {};
      availableApps.forEach(function (a) {
        if (a.installed) {
          installedSet[a.id] = true;
          installedVersions[a.id] = a.installedVersion;
        }
      });
      showLoading(false);
      extractCategories();
      renderCategoryBar();
      render();
      if (detailApp) {
        var updated = findAppById(detailApp.id);
        if (updated) {
          detailApp = updated;
          renderDetailContent(updated);
        } else {
          closeDetail();
        }
      }
    });
  }

  /* What the user is told when an action did not complete (reason → message) */
  var REASON_KEYS = {
    unconfigured: 'store.unconfigured', unreachable: 'store.error_network',
    download_failed: 'store.error_network', untrusted: 'store.error_untrusted',
    bad_package: 'store.error_untrusted', invalid_package: 'store.error_untrusted',
    not_provisioned: 'store.error_not_provisioned', expired: 'store.error_expired',
    revoked: 'store.error_revoked', busy: 'store.error_busy'
  };

  function act(method, appId, pending) {
    pendingActions[appId] = pending;
    render();
    refreshDetailIfOpen(appId);
    ZylBridge.requestService('appstore', method, { appId: appId }).then(function (r) {
      if (!r || !r.ok) ZylToast.error(zylI18n.t(REASON_KEYS[r && r.reason] || 'store.error_failed'));
    }, function () {
      ZylToast.error(zylI18n.t('store.error_network'));
    }).then(function () {
      delete pendingActions[appId];
      requestData();
    });
  }

  /* ─── A package from the user's files: the Files app opens a .zpkg here
     (app.json "intents"); installed only after the user confirms ─── */
  function openPackage(path) {
    detailApp = null;
    if (detailOverlayEl) detailOverlayEl.className = '';
    if (detailTitleEl) detailTitleEl.textContent = path.split('/').pop();
    if (!detailContentEl) return;
    detailContentEl.innerHTML = '';
    var prompt = document.createElement('div');
    prompt.className = 'detail-description-text';
    prompt.textContent = zylI18n.t('store.install_file_prompt');
    var area = document.createElement('div');
    area.className = 'detail-action-area';
    var btn = document.createElement('button');
    btn.className = 'detail-action-btn btn-install';
    btn.textContent = zylI18n.t('store.install');
    btn.addEventListener('click', function () {
      btn.disabled = true;
      btn.textContent = zylI18n.t('store.installing');
      ZylBridge.requestService('appstore', 'installFile', { path: path }).then(function (r) {
        if (r && r.ok) ZylToast.success(zylI18n.t('store.installed'));
        else ZylToast.error(zylI18n.t(REASON_KEYS[r && r.reason] || 'store.error_failed'));
      }, function () {
        ZylToast.error(zylI18n.t('store.error_failed'));
      }).then(function () {
        closeDetail();
        requestData();
      });
    });
    area.appendChild(btn);
    detailContentEl.appendChild(prompt);
    detailContentEl.appendChild(area);
  }

  ZylIntent.onIntent(function (intent) {
    var file = (intent.files || [])[0];
    if (intent.action === ZylIntent.ACTION.VIEW && file) openPackage(String(file));
  });

  /* ─── Navigation ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    var msg;
    try {
      msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
    } catch (err) {
      return;
    }
    if (msg && msg.type === 'navigation.back') {
      if (detailApp) {
        closeDetail();
        return;
      }
      ZylBridge.sendToSystem({ type: 'navigation.exit' });
    }
  });

  /* Another app installed or removed (device: the store's signal) */
  ZylBridge.onServiceEvent('appstore', 'AppInstalled', function () { requestData(); });
  ZylBridge.onServiceEvent('appstore', 'AppRemoved', function () { requestData(); });

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

  /* Nothing to list: say why — no store set up, unreachable, not verifiable */
  var EMPTY_KEYS = { unconfigured: 'store.unconfigured', unreachable: 'store.error_network',
                     untrusted: 'store.error_untrusted', invalid: 'store.error_untrusted' };

  function showEmpty(show) {
    if (!emptyStateEl) return;
    emptyStateEl.className = show ? '' : 'hidden';
    var text = emptyStateEl.querySelector('.empty-text');
    if (text) text.textContent = zylI18n.t(EMPTY_KEYS[storeState] || 'store.empty');
  }

  /* ─── Update detection ─── */
  function hasUpdate(app) {
    return !!app.update;
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
            act('install', appId, 'updating');
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
            act('uninstall', appId, 'uninstalling');
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
          act('install', appId, 'installing');
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
      metaDiv.appendChild(createMetaRow(zylI18n.t('store.size'), zylI18n.formatBytes(app.size)));
    }
    if (app.developer) {
      metaDiv.appendChild(createMetaRow(zylI18n.t('store.developer'), app.developer));
    }
    /* What the app asks for — known before installing */
    if (app.permissions && app.permissions.length) {
      metaDiv.appendChild(createMetaRow(zylI18n.t('store.permissions'), app.permissions.map(function (p) {
        var key = 'permission.' + p, name = zylI18n.t(key);
        return name !== key ? name : p;
      }).join(', ')));
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
            act('install', appId, 'updating');
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
            act('uninstall', appId, 'uninstalling');
          };
        })(app.id));
      }
    } else {
      btnEl.className += ' btn-install';
      btnEl.textContent = zylI18n.t('store.install');
      btnEl.addEventListener('click', (function (appId) {
        return function () {
          act('install', appId, 'installing');
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
