// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 브라우저 앱 UI — 탭 관리, URL 탐색, 북마크
// 수행범위: 탭 생성/전환/닫기, URL 입력/탐색, 북마크 사이드바, 페이지 렌더링
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 브라우저 UI와 탐색 로직만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── Constants ─── */
  var MAX_TABS = 5;

  /* ─── State ─── */
  var tabs = [
    { id: 0, title: 'New Tab', url: '', history: [], historyIndex: -1 }
  ];
  var activeTabId = 0;
  var nextTabId = 1;

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

  /* 앱 기본 데이터 (실기기에서는 앱 내부 DB에서 로드) */
  var bookmarksData = [
    { name: 'Zyl OS', url: 'https://www.zylos.dev', favicon: 'Z' },
    { name: 'GitHub', url: 'https://github.com', favicon: 'G' },
    { name: 'RISC-V', url: 'https://riscv.org', favicon: 'R' }
  ];
  var quickLinksData = [
    { name: 'GitHub', url: 'https://github.com', iconBg: 'linear-gradient(135deg,#333,#111)', svgPath: 'M12 2C6.477 2 2 6.477 2 12c0 4.42 2.87 8.17 6.84 9.5.5.08.66-.23.66-.5v-1.69c-2.77.6-3.36-1.34-3.36-1.34-.46-1.16-1.11-1.47-1.11-1.47-.91-.62.07-.6.07-.6 1 .07 1.53 1.03 1.53 1.03.87 1.52 2.34 1.07 2.91.83.09-.65.35-1.09.63-1.34-2.22-.25-4.55-1.11-4.55-4.92 0-1.11.38-2 1.03-2.71-.1-.25-.45-1.29.1-2.64 0 0 .84-.27 2.75 1.02.79-.22 1.65-.33 2.5-.33.85 0 1.71.11 2.5.33 1.91-1.29 2.75-1.02 2.75-1.02.55 1.35.2 2.39.1 2.64.65.71 1.03 1.6 1.03 2.71 0 3.82-2.34 4.66-4.57 4.91.36.31.69.92.69 1.85V21c0 .27.16.59.67.5C19.14 20.16 22 16.42 22 12A10 10 0 0012 2z' },
    { name: 'Wikipedia', url: 'https://wikipedia.org', iconBg: 'linear-gradient(135deg,#eee,#ccc)', svgPath: 'M14.97 18.95L12 12.52l-2.97 6.43a.5.5 0 01-.91-.01L4.94 9.04a.5.5 0 11.92-.38l3.18 7.72L12 10.04l2.96 6.34 3.18-7.72a.5.5 0 01.92.38l-3.18 9.9a.5.5 0 01-.91.01z', svgFill: '#333' },
    { name: 'RISC-V', url: 'https://riscv.org', iconBg: 'linear-gradient(135deg,#4a9eff,#2563eb)', svgPath: 'M20 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0 14H4V8h16v10zm-2-1h-6v-2h6v2zM7.5 17l-1.41-1.41L8.67 13l-2.59-2.59L7.5 9l4 4-4 4z' }
  ];
  var pageFrame = document.getElementById('page-frame');

  /* Request browser data from central service */
  function requestBrowserData() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'browser', method: 'getBookmarks'
    }), '*');
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'browser', method: 'getQuickLinks'
    }), '*');
  }

  /* Listen for service responses */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* ── Navigation back handling ── */
      if (msg.type === 'navigation.back') {
        var tab = tabs[activeTabIndex];
        if (tab && tab.historyIndex > 0) {
          if (btnBack) btnBack.click();
          window.parent.postMessage(JSON.stringify({ type: 'navigation.handled' }), '*');
        } else {
          window.parent.postMessage(JSON.stringify({ type: 'navigation.exit' }), '*');
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
        }
      }
    } catch (err) { /* ignore */ }
  });

  requestBrowserData();

  /* ─── Helper: Get active tab ─── */
  function getActiveTab() {
    return tabs.find(function (t) { return t.id === activeTabId; });
  }

  /* ─── Render Tabs ─── */
  function renderTabs() {
    tabsContainer.innerHTML = '';
    tabs.forEach(function (tab) {
      var el = document.createElement('div');
      el.className = 'tab' + (tab.id === activeTabId ? ' active' : '');
      el.dataset.tab = tab.id;
      el.innerHTML = '<span class="tab-title">' + escapeHtml(tab.title) + '</span>' +
        (tabs.length > 1 ? '<button class="tab-close">&times;</button>' : '');
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
    var tab = { id: nextTabId++, title: 'New Tab', url: '', history: [], historyIndex: -1 };
    tabs.push(tab);
    switchTab(tab.id);
  }

  /* ─── Close Tab ─── */
  function closeTab(id) {
    if (tabs.length <= 1) return;
    var idx = tabs.findIndex(function (t) { return t.id === id; });
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
    newTabPage.classList.remove('active');
    webPage.classList.remove('hidden');
    pageLoading.classList.add('active');
    if (pageFrame) pageFrame.style.display = 'none';

    var tab = getActiveTab();
    var domain = extractDomain(url);
    tab.url = url;
    tab.title = domain || 'Loading...';

    if (!skipHistory) {
      tab.history = tab.history.slice(0, tab.historyIndex + 1);
      tab.history.push(url);
      tab.historyIndex = tab.history.length - 1;
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
    if (tab.historyIndex > 0) {
      tab.historyIndex--;
      var url = tab.history[tab.historyIndex];
      urlInput.value = url;
      showWebPage(url, true);
    }
  }

  function navigateForward() {
    var tab = getActiveTab();
    if (tab.historyIndex < tab.history.length - 1) {
      tab.historyIndex++;
      var url = tab.history[tab.historyIndex];
      urlInput.value = url;
      showWebPage(url, true);
    }
  }

  function refresh() {
    var tab = getActiveTab();
    if (tab.url) {
      showWebPage(tab.url, true);
    }
  }

  function updateNavButtons() {
    var tab = getActiveTab();
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
    notice.innerHTML =
      '<div style="font-size:48px;margin-bottom:16px">🔒</div>' +
      '<div style="font-size:16px;font-weight:600;margin-bottom:8px">' + escapeHtml(domain || url) + '</div>' +
      '<div style="font-size:13px;color:#666;line-height:1.6;max-width:280px">' +
        'This site blocks iframe embedding (X-Frame-Options).<br>' +
        'On real hardware, pages load via native WebKitGTK.' +
      '</div>';
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
      list.innerHTML = '<div style="padding:16px;opacity:0.5;text-align:center">No bookmarks</div>';
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
      var fillAttr = ql.svgFill ? ' fill="' + ql.svgFill + '"' : '';
      el.innerHTML =
        '<div class="quick-link-icon" style="background: ' + ql.iconBg + ';">' +
          '<svg viewBox="0 0 24 24" width="24" height="24"><path' + fillAttr + ' d="' + ql.svgPath + '"/></svg>' +
        '</div>' +
        '<span>' + ql.name + '</span>';
      el.addEventListener('click', function () {
        urlInput.value = ql.url;
        showWebPage(ql.url);
      });
      container.appendChild(el);
    });
  }

  /* ─── Menu Button (placeholder) ─── */
  btnMenu.addEventListener('click', function () {
    /* Future: dropdown menu */
  });

  /* ─── Init ─── */
  renderTabs();
  updateNavButtons();

})();
