// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 앱스토어 — 설치 가능한 앱 목록 조회 및 설치/제거 관리
// 수행범위: appstore.getAvailable, apps.getInstalled 서비스 조회,
//           appstore.install / appstore.uninstall 서비스 호출,
//           검색 필터링, 탭 전환, 앱 카드 렌더링
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

  /* ─── State ─── */
  var availableApps = [];   // from appstore.getAvailable
  var installedSet = {};    // id -> true, from apps.getInstalled
  var activeTab = 'all';    // 'all' | 'installed'
  var searchQuery = '';
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
    window.parent.postMessage(JSON.stringify(msg), '*');
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
      return;
    }
    if (!msg || msg.type !== 'service.response') return;

    if (msg.service === 'appstore' && msg.method === 'getAvailable' && msg.data) {
      availableApps = Array.isArray(msg.data) ? msg.data : [];
      dataReady.available = true;
      onDataLoaded();
    }

    if (msg.service === 'apps' && msg.method === 'getInstalled' && msg.data) {
      var list = Array.isArray(msg.data) ? msg.data : [];
      installedSet = {};
      for (var i = 0; i < list.length; i++) {
        var id = list[i].id || list[i];
        installedSet[id] = true;
      }
      dataReady.installed = true;
      onDataLoaded();
    }

    // Handle install/uninstall responses by refreshing data
    if (msg.service === 'appstore' && (msg.method === 'install' || msg.method === 'uninstall')) {
      requestData();
    }
  });

  function onDataLoaded() {
    if (dataReady.available && dataReady.installed) {
      showLoading(false);
      render();
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

  /* ─── Filtering ─── */
  function getFilteredApps() {
    var list = [];
    var q = searchQuery.toLowerCase();

    for (var i = 0; i < availableApps.length; i++) {
      var app = availableApps[i];
      var isInstalled = !!installedSet[app.id];

      // Tab filter
      if (activeTab === 'installed' && !isInstalled) continue;

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

  /* ─── Render ─── */
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

    var card = document.createElement('div');
    card.className = 'app-card';

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
    nameEl.textContent = escapeHtml(app.name || app.id || '');
    nameRow.appendChild(nameEl);

    if (isSystem) {
      var badge = document.createElement('span');
      badge.className = 'badge-system';
      badge.setAttribute('data-i18n', 'store.system');
      badge.textContent = 'System';
      nameRow.appendChild(badge);
    }

    infoEl.appendChild(nameRow);

    if (app.version) {
      var verEl = document.createElement('span');
      verEl.className = 'app-version';
      verEl.textContent = 'v' + escapeHtml(app.version);
      infoEl.appendChild(verEl);
    }

    if (app.description) {
      var descEl = document.createElement('div');
      descEl.className = 'app-desc';
      descEl.textContent = escapeHtml(app.description);
      infoEl.appendChild(descEl);
    }

    card.appendChild(infoEl);

    /* Action button */
    var btnEl = document.createElement('button');
    btnEl.className = 'app-action';

    if (isInstalled) {
      if (isSystem) {
        btnEl.className += ' btn-system';
        btnEl.setAttribute('data-i18n', 'store.installed');
        btnEl.textContent = 'Installed';
        btnEl.disabled = true;
      } else {
        btnEl.className += ' btn-uninstall';
        btnEl.setAttribute('data-i18n', 'store.uninstall');
        btnEl.textContent = 'Uninstall';
        btnEl.addEventListener('click', (function (appId) {
          return function () {
            sendRequest('appstore', 'uninstall', { appId: appId });
          };
        })(app.id));
      }
    } else {
      btnEl.className += ' btn-install';
      btnEl.setAttribute('data-i18n', 'store.install');
      btnEl.textContent = 'Install';
      btnEl.addEventListener('click', (function (appId) {
        return function () {
          sendRequest('appstore', 'install', { appId: appId });
        };
      })(app.id));
    }

    card.appendChild(btnEl);

    return card;
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
