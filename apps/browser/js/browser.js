// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 브라우저 앱 UI — 탭 관리, URL 탐색, 북마크, 히스토리, 메뉴, 시크릿 모드
// 수행범위: 탭 생성/전환/닫기, URL 입력/탐색, 북마크 사이드바, 히스토리 사이드바, 메뉴 팝업, 시크릿 모드, 페이지 렌더링
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 브라우저 UI와 탐색 로직만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

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

  /* ─── ES5 helpers ─── */
  function arrFind(arr, fn) {
    for (var i = 0; i < arr.length; i++) {
      if (fn(arr[i], i, arr)) return arr[i];
    }
    return undefined;
  }
  function arrFindIndex(arr, fn) {
    for (var i = 0; i < arr.length; i++) {
      if (fn(arr[i], i, arr)) return i;
    }
    return -1;
  }

  /* ─── Constants ─── */
  var MAX_TABS = 5;
  var PRIVATE_MASK = '\uD83D\uDD76 ';

  /* ─── State ─── */
  var tabs = [
    { id: 0, title: (typeof zylI18n !== 'undefined' ? zylI18n.t('browser.new_tab') : 'New Tab'), url: '', history: [], historyIndex: -1 }
  ];
  var activeTabId = 0;
  var nextTabId = 1;
  var isPrivateMode = false;
  var globalHistory = [];

  /* ─── DOM References ─── */
  var tabsContainer = document.getElementById('tabs-container');
  var btnNewTab = document.getElementById('btn-new-tab');
  var btnBack = document.getElementById('btn-back');
  var btnForward = document.getElementById('btn-forward');
  var btnRefresh = document.getElementById('btn-refresh');
  var urlInput = document.getElementById('url-input');
  var btnBookmarks = document.getElementById('btn-bookmarks');
  var btnMenu = document.getElementById('btn-menu');
  var bookmarksSidebar = document.getElementById('bookmarks-sidebar');
  var btnCloseBookmarks = document.getElementById('btn-close-bookmarks');
  var newTabPage = document.getElementById('new-tab-page');
  var webPage = document.getElementById('web-page');
  var pageLoading = document.getElementById('page-loading');
  var pageContent = document.getElementById('page-content');
  var ntpSearchInput = document.getElementById('ntp-search-input');
  var privateIndicator = document.getElementById('private-indicator');
  var menuPopup = document.getElementById('menu-popup');
  var menuOverlay = document.getElementById('menu-overlay');
  var historySidebar = document.getElementById('history-sidebar');
  var btnCloseHistory = document.getElementById('btn-close-history');
  var btnClearHistory = document.getElementById('btn-clear-history');

  /* 북마크/빠른링크는 서비스에서 로드 — Mock 데이터 금지 (CLAUDE.md §5) */
  var bookmarksData = [];
  var quickLinksData = [];
  var pageFrame = document.getElementById('page-frame');

  /* Request browser data from central service */
  function requestBrowserData() {
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'browser', method: 'getBookmarks'
    });
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'browser', method: 'getQuickLinks'
    });
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'browser', method: 'getHistory'
    });
  }

  /* Listen for service responses */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* ── Navigation back handling ── */
      if (msg.type === 'navigation.back') {
        var tab = getActiveTab();
        if (tab && tab.historyIndex > 0) {
          if (btnBack) btnBack.click();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;
      if (msg.service === 'browser') {
        if (msg.method === 'getBookmarks' && msg.data) {
          bookmarksData = msg.data;
          renderBookmarks();
        } else if (msg.method === 'getQuickLinks' && msg.data) {
          quickLinksData = msg.data;
          renderQuickLinks();
        } else if (msg.method === 'getHistory' && msg.data) {
          globalHistory = msg.data;
          renderHistory();
        }
      }
    } catch (err) { /* ignore malformed messages */ }
  });

  requestBrowserData();

  /* ─── Helper: Get active tab ─── */
  function getActiveTab() {
    return arrFind(tabs, function (t) { return t.id === activeTabId; });
  }

  /* ─── History management ─── */
  function addHistoryEntry(url, title) {
    if (isPrivateMode) return;
    var entry = {
      url: url,
      title: title || extractDomain(url) || url,
      timestamp: Date.now()
    };
    globalHistory.unshift(entry);
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'browser', method: 'addHistory',
      data: entry
    });
  }

  function clearHistory() {
    globalHistory = [];
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'browser', method: 'clearHistory'
    });
    renderHistory();
    showToast(zylI18n.t('browser.history_cleared'));
  }

  function formatTime(ts) {
    var d = new Date(ts);
    var h = d.getHours();
    var m = d.getMinutes();
    return (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m;
  }

  function renderHistory() {
    var list = document.getElementById('history-list');
    if (!list) return;
    list.innerHTML = '';
    if (globalHistory.length === 0) {
      var emptyDiv = document.createElement('div');
      emptyDiv.style.cssText = 'padding:16px;opacity:0.5;text-align:center';
      emptyDiv.textContent = zylI18n.t('browser.no_history');
      list.appendChild(emptyDiv);
      return;
    }
    globalHistory.forEach(function (entry) {
      var el = document.createElement('div');
      el.className = 'history-item';
      var domain = extractDomain(entry.url);
      var initial = domain ? domain.charAt(0).toUpperCase() : '?';

      var iconDiv = document.createElement('div');
      iconDiv.className = 'history-icon';
      iconDiv.textContent = initial;

      var infoDiv = document.createElement('div');
      infoDiv.className = 'history-info';

      var titleSpan = document.createElement('span');
      titleSpan.className = 'history-title';
      titleSpan.textContent = entry.title || domain || entry.url;

      var timeSpan = document.createElement('span');
      timeSpan.className = 'history-time';
      timeSpan.textContent = formatTime(entry.timestamp);

      infoDiv.appendChild(titleSpan);
      infoDiv.appendChild(timeSpan);
      el.appendChild(iconDiv);
      el.appendChild(infoDiv);

      el.setAttribute('role', 'link');
      el.setAttribute('aria-label', entry.title || entry.url);
      addButtonKeyHandler(el);
      el.addEventListener('click', function () {
        urlInput.value = entry.url;
        showWebPage(entry.url);
        historySidebar.classList.add('hidden');
      });
      list.appendChild(el);
    });
  }

  /* ─── Private mode ─── */
  function togglePrivateMode() {
    isPrivateMode = !isPrivateMode;
    if (isPrivateMode) {
      document.body.classList.add('private-mode');
      privateIndicator.classList.remove('hidden');
      showToast(zylI18n.t('browser.private_on'));
    } else {
      document.body.classList.remove('private-mode');
      privateIndicator.classList.add('hidden');
      showToast(zylI18n.t('browser.private_off'));
    }
    renderTabs();
  }

  /* ─── Menu popup ─── */
  function openMenu() {
    menuPopup.classList.remove('hidden');
    menuOverlay.classList.remove('hidden');
  }

  function closeMenu() {
    menuPopup.classList.add('hidden');
    menuOverlay.classList.add('hidden');
  }

  /* ─── Render Tabs ─── */
  function renderTabs() {
    tabsContainer.innerHTML = '';
    tabs.forEach(function (tab) {
      var el = document.createElement('div');
      el.className = 'tab' + (tab.id === activeTabId ? ' active' : '');
      el.dataset.tab = tab.id;
      var titleSpan = document.createElement('span');
      titleSpan.className = 'tab-title';
      var displayTitle = tab.title;
      if (isPrivateMode) {
        displayTitle = PRIVATE_MASK + displayTitle;
      }
      titleSpan.textContent = displayTitle;
      el.appendChild(titleSpan);
      if (tabs.length > 1) {
        var closeBtn = document.createElement('button');
        closeBtn.className = 'tab-close';
        closeBtn.textContent = '\u00d7';
        el.appendChild(closeBtn);
      }
      addButtonKeyHandler(el);
      el.addEventListener('click', function (e) {
        if (e.target.classList.contains('tab-close')) {
          closeTab(tab.id);
        } else {
          switchTab(tab.id);
        }
      });
      tabsContainer.appendChild(el);
    });

    btnNewTab.style.display = tabs.length >= MAX_TABS ? 'none' : 'flex';
  }

  /* ─── Switch Tab ─── */
  function switchTab(id) {
    activeTabId = id;
    var tab = getActiveTab();
    if (!tab) return;
    renderTabs();
    if (tab.url) {
      urlInput.value = tab.url;
      showWebPage(tab.url, true);
    } else {
      urlInput.value = '';
      showNewTabPage();
    }
    updateNavButtons();
  }

  /* ─── New Tab ─── */
  function createNewTab() {
    if (tabs.length >= MAX_TABS) return;
    var tab = { id: nextTabId++, title: (typeof zylI18n !== 'undefined' ? zylI18n.t('browser.new_tab') : 'New Tab'), url: '', history: [], historyIndex: -1 };
    tabs.push(tab);
    switchTab(tab.id);
  }

  /* ─── Close Tab ─── */
  function closeTab(id) {
    if (tabs.length <= 1) return;
    var idx = arrFindIndex(tabs, function (t) { return t.id === id; });
    tabs.splice(idx, 1);
    if (activeTabId === id) {
      var newIdx = Math.min(idx, tabs.length - 1);
      switchTab(tabs[newIdx].id);
    } else {
      renderTabs();
    }
  }

  /* ─── Toast notification ─── */
  function showToast(message) {
    var existing = document.getElementById('browser-toast');
    if (existing) existing.remove();
    var toast = document.createElement('div');
    toast.id = 'browser-toast';
    toast.textContent = message;
    toast.style.cssText =
      'position:fixed;bottom:60px;left:50%;transform:translateX(-50%);' +
      'background:rgba(0,0,0,0.8);color:#fff;padding:10px 20px;' +
      'border-radius:8px;font-size:13px;z-index:9999;' +
      'animation:fadeOut 0.5s ease 2s forwards;';
    document.body.appendChild(toast);
    setTimeout(function () { toast.remove(); }, 2500);
  }

  /* ─── Show New Tab Page ─── */
  function showNewTabPage() {
    newTabPage.classList.add('active');
    webPage.classList.add('hidden');
    pageLoading.classList.remove('active');
  }

  /* ─── Show Web Page (real iframe rendering) ─── */
  function showWebPage(url, skipHistory) {
    /* Intent 연동: 외부 URL 열기 */
    if (typeof ZylIntent !== 'undefined') {
      ZylIntent.startIntent({
        action: ZylIntent.ACTION.VIEW,
        data: url,
        mimeType: 'text/uri'
      });
    }

    newTabPage.classList.remove('active');
    webPage.classList.remove('hidden');
    pageLoading.classList.add('active');
    if (pageFrame) pageFrame.style.display = 'none';

    var tab = getActiveTab();
    if (!tab) return;
    var domain = extractDomain(url);
    tab.url = url;
    tab.title = domain || zylI18n.t('browser.loading');

    if (!skipHistory) {
      tab.history = tab.history.slice(0, tab.historyIndex + 1);
      tab.history.push(url);
      tab.historyIndex = tab.history.length - 1;
      addHistoryEntry(url, domain);
    }

    renderTabs();
    updateNavButtons();

    /* 실제 URL을 iframe에 로드 */
    if (pageFrame) {
      pageFrame.onload = function () {
        pageLoading.classList.remove('active');
        pageFrame.style.display = 'block';
        /* X-Frame-Options 차단 감지 — contentDocument 접근 불가 시 */
        var blocked = false;
        try {
          var doc = pageFrame.contentDocument;
          if (doc && doc.title) {
            tab.title = doc.title;
          } else if (doc && doc.body && doc.body.innerHTML.length < 10) {
            blocked = true;
          } else {
            tab.title = domain || url;
          }
        } catch (e) {
          /* cross-origin — 정상 로드됨 (타이틀 접근 불가는 보안상 정상) */
          tab.title = domain || url;
        }
        if (blocked) {
          pageFrame.style.display = 'none';
          showBlockedPage(url, domain);
        }
        renderTabs();
        pageFrame.onload = null;
      };
      pageFrame.onerror = function () {
        pageLoading.classList.remove('active');
        pageFrame.style.display = 'none';
        showBlockedPage(url, domain);
        pageFrame.onerror = null;
      };
      pageFrame.src = url;

      /* 8초 타임아웃 */
      setTimeout(function () {
        if (pageLoading.classList.contains('active')) {
          pageLoading.classList.remove('active');
          pageFrame.style.display = 'block';
          tab.title = domain || url;
          renderTabs();
        }
      }, 8000);
    }
  }

  /* ─── Navigation ─── */
  function navigateBack() {
    var tab = getActiveTab();
    if (!tab) return;
    if (tab.historyIndex > 0) {
      tab.historyIndex--;
      var url = tab.history[tab.historyIndex];
      urlInput.value = url;
      showWebPage(url, true);
    }
  }

  function navigateForward() {
    var tab = getActiveTab();
    if (!tab) return;
    if (tab.historyIndex < tab.history.length - 1) {
      tab.historyIndex++;
      var url = tab.history[tab.historyIndex];
      urlInput.value = url;
      showWebPage(url, true);
    }
  }

  function refresh() {
    var tab = getActiveTab();
    if (!tab) return;
    if (tab.url) {
      showWebPage(tab.url, true);
    }
  }

  function updateNavButtons() {
    var tab = getActiveTab();
    if (!tab) return;
    btnBack.disabled = tab.historyIndex <= 0;
    btnForward.disabled = tab.historyIndex >= tab.history.length - 1;
  }

  /* ─── URL Processing ─── */
  function processInput(value) {
    var trimmed = value.trim();
    if (!trimmed) return;

    if (/^https?:\/\//i.test(trimmed)) {
      return trimmed;
    }
    if (/^[a-zA-Z0-9][-a-zA-Z0-9]*\.[a-zA-Z]{2,}/.test(trimmed)) {
      return 'https://' + trimmed;
    }
    /* Treat as search query */
    return 'https://duckduckgo.com/?q=' + encodeURIComponent(trimmed);
  }

  /* X-Frame-Options 차단 시 안내 페이지 표시 */
  function showBlockedPage(url, domain) {
    var webPage = document.getElementById('web-page');
    if (!webPage) return;
    /* page-frame 뒤에 안내 div 추가 */
    var existing = document.getElementById('blocked-notice');
    if (existing) existing.remove();
    var notice = document.createElement('div');
    notice.id = 'blocked-notice';
    notice.style.cssText = 'position:absolute;inset:0;background:#f8f9fa;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:32px;text-align:center;color:#333;z-index:2';
    var iconDiv = document.createElement('div');
    iconDiv.style.cssText = 'font-size:48px;margin-bottom:16px';
    iconDiv.textContent = '\uD83D\uDD12';
    var domainDiv = document.createElement('div');
    domainDiv.style.cssText = 'font-size:16px;font-weight:600;margin-bottom:8px';
    domainDiv.textContent = domain || url;
    var msgDiv = document.createElement('div');
    msgDiv.style.cssText = 'font-size:13px;color:#666;line-height:1.6;max-width:280px';
    msgDiv.textContent = zylI18n.t('browser.blocked_iframe');
    notice.appendChild(iconDiv);
    notice.appendChild(domainDiv);
    notice.appendChild(msgDiv);
    webPage.appendChild(notice);
  }

  function extractDomain(url) {
    try {
      var match = url.match(/^https?:\/\/(?:www\.)?([^\/]+)/i);
      return match ? match[1] : '';
    } catch (e) {
      return '';
    }
  }

  function escapeHtml(str) {
    var el = document.createElement('span');
    el.textContent = str;
    return el.innerHTML;
  }

  /* ─── URL Input Handler ─── */
  function handleUrlSubmit() {
    var url = processInput(urlInput.value);
    if (url) {
      urlInput.value = url;
      showWebPage(url);
      urlInput.blur();
    }
  }

  urlInput.addEventListener('keydown', function (e) {
    if (e.key === 'Enter') handleUrlSubmit();
  });

  urlInput.addEventListener('focus', function () {
    urlInput.select();
  });

  /* NTP search */
  ntpSearchInput.addEventListener('keydown', function (e) {
    if (e.key === 'Enter') {
      var url = processInput(ntpSearchInput.value);
      if (url) {
        urlInput.value = url;
        showWebPage(url);
        ntpSearchInput.value = '';
      }
    }
  });

  /* ─── Navigation Buttons ─── */
  btnBack.addEventListener('click', navigateBack);
  btnForward.addEventListener('click', navigateForward);
  btnRefresh.addEventListener('click', refresh);
  btnNewTab.addEventListener('click', createNewTab);

  /* ─── Bookmarks ─── */
  btnBookmarks.addEventListener('click', function () {
    historySidebar.classList.add('hidden');
    bookmarksSidebar.classList.toggle('hidden');
  });

  btnCloseBookmarks.addEventListener('click', function () {
    bookmarksSidebar.classList.add('hidden');
  });

  /* ─── Render Bookmarks from service data ─── */
  function renderBookmarks() {
    var list = document.getElementById('bookmarks-list');
    if (!list) return;
    list.innerHTML = '';
    if (bookmarksData.length === 0) {
      var emptyDiv = document.createElement('div');
      emptyDiv.style.cssText = 'padding:16px;opacity:0.5;text-align:center';
      emptyDiv.textContent = zylI18n.t('browser.no_bookmarks');
      list.appendChild(emptyDiv);
      return;
    }
    bookmarksData.forEach(function (bm) {
      var el = document.createElement('div');
      el.className = 'bookmark-item';
      el.dataset.url = bm.url;
      var domain = bm.url.replace(/^https?:\/\/(www\.)?/, '').replace(/\/.*$/, '');
      el.innerHTML =
        '<div class="bookmark-favicon">' + escapeHtml(bm.favicon || bm.name.charAt(0)) + '</div>' +
        '<div class="bookmark-info">' +
          '<span class="bookmark-name">' + escapeHtml(bm.name) + '</span>' +
          '<span class="bookmark-url">' + escapeHtml(domain) + '</span>' +
        '</div>';
      el.setAttribute('role', 'link');
      el.setAttribute('aria-label', bm.name);
      addButtonKeyHandler(el);
      el.addEventListener('click', function () {
        urlInput.value = bm.url;
        showWebPage(bm.url);
        bookmarksSidebar.classList.add('hidden');
      });
      list.appendChild(el);
    });
  }

  /* ─── Render Quick Links from service data ─── */
  function renderQuickLinks() {
    var container = document.getElementById('quick-links');
    if (!container) return;
    container.innerHTML = '';
    if (quickLinksData.length === 0) return;
    quickLinksData.forEach(function (ql) {
      var el = document.createElement('div');
      el.className = 'quick-link';
      el.dataset.url = ql.url;
      var fillAttr = ql.svgFill ? ' fill="' + escapeHtml(ql.svgFill) + '"' : '';
      el.innerHTML =
        '<div class="quick-link-icon" style="background: ' + escapeHtml(ql.iconBg || '') + ';">' +
          '<svg viewBox="0 0 24 24" width="24" height="24"><path' + fillAttr + ' d="' + escapeHtml(ql.svgPath || '') + '"/></svg>' +
        '</div>' +
        '<span>' + escapeHtml(ql.name || '') + '</span>';
      el.setAttribute('role', 'link');
      el.setAttribute('aria-label', ql.name);
      addButtonKeyHandler(el);
      el.addEventListener('click', function () {
        urlInput.value = ql.url;
        showWebPage(ql.url);
      });
      container.appendChild(el);
    });
  }

  /* ─── URL 복사 기능 (클립보드 연동) ─── */
  function copyCurrentUrl() {
    var tab = getActiveTab();
    if (!tab || !tab.url) return;
    if (typeof ZylBridge !== 'undefined') {
      ZylBridge.requestService('clipboard', 'copy', { text: tab.url }).catch(function () { /* ignore */ });
    }
    showToast(zylI18n.t('browser.url_copied'));
  }

  /* ─── Add bookmark for current page ─── */
  function addBookmarkForCurrentPage() {
    var tab = getActiveTab();
    if (!tab || !tab.url) return;
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'browser', method: 'addBookmark',
      data: { name: tab.title, url: tab.url }
    });
    showToast(zylI18n.t('browser.bookmark_added'));
  }

  /* URL 입력 필드 더블탭 시 복사 */
  urlInput.addEventListener('dblclick', function () {
    copyCurrentUrl();
  });

  /* ─── Menu Button ─── */
  btnMenu.addEventListener('click', function () {
    openMenu();
  });

  menuOverlay.addEventListener('click', function () {
    closeMenu();
  });

  /* Menu item handlers */
  document.getElementById('menu-copy-url').addEventListener('click', function () {
    closeMenu();
    copyCurrentUrl();
  });

  document.getElementById('menu-add-bookmark').addEventListener('click', function () {
    closeMenu();
    addBookmarkForCurrentPage();
  });

  document.getElementById('menu-history').addEventListener('click', function () {
    closeMenu();
    bookmarksSidebar.classList.add('hidden');
    historySidebar.classList.toggle('hidden');
    renderHistory();
  });

  document.getElementById('menu-private').addEventListener('click', function () {
    closeMenu();
    togglePrivateMode();
  });

  document.getElementById('menu-settings').addEventListener('click', function () {
    closeMenu();
    /* Settings navigates to system settings via intent */
    if (typeof ZylIntent !== 'undefined') {
      ZylIntent.startActivity({
        action: 'android.intent.action.VIEW',
        component: 'settings'
      });
    }
  });

  /* ─── History Sidebar ─── */
  btnCloseHistory.addEventListener('click', function () {
    historySidebar.classList.add('hidden');
  });

  btnClearHistory.addEventListener('click', function () {
    clearHistory();
  });

  /* ─── Init ─── */
  renderTabs();
  updateNavButtons();

})();
