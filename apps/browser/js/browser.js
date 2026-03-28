// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 브라우저 앱 UI — 탭 관리, URL 탐색, 북마크
// 수행범위: 탭 생성/전환/닫기, URL 입력/탐색, 북마크 사이드바, 페이지 렌더링
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 브라우저 UI와 탐색 로직만 담당
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

  /* ─── Simulated Pages ─── */
  var simulatedPages = {
    'bpios.dev': {
      title: 'BPI-OS Official',
      html: '<div class="sim-header"><h1>BPI-OS</h1><p>A modern Linux-based mobile operating system designed for Banana Pi single-board computers with RISC-V architecture.</p></div>' +
        '<div class="sim-section"><h3>Features</h3><p>Custom UI toolkit, WebKitGTK browser, hardware-accelerated graphics, and full RISC-V support.</p></div>' +
        '<div class="sim-section"><h3>Getting Started</h3><p>Flash the BPI-OS image to your SD card and boot your Banana Pi F3 board.</p></div>'
    },
    'github.com': {
      title: 'GitHub',
      html: '<div class="sim-header"><h1>GitHub</h1><p>Where the world builds software. Millions of developers use GitHub to build, ship, and maintain their software.</p></div>' +
        '<div class="sim-section"><h3>Trending Repositories</h3><p>Explore popular open source projects and contribute to the community.</p></div>'
    },
    'riscv.org': {
      title: 'RISC-V International',
      html: '<div class="sim-header"><h1>RISC-V International</h1><p>RISC-V is a free and open ISA enabling a new era of processor innovation.</p></div>' +
        '<div class="sim-section"><h3>Open Standard</h3><p>The RISC-V ISA is provided under open source licenses with no fees.</p></div>'
    }
  };

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

  /* ─── Show New Tab Page ─── */
  function showNewTabPage() {
    newTabPage.classList.add('active');
    webPage.classList.add('hidden');
    pageLoading.classList.remove('active');
  }

  /* ─── Show Web Page ─── */
  function showWebPage(url, skipHistory) {
    newTabPage.classList.remove('active');
    webPage.classList.remove('hidden');
    pageLoading.classList.add('active');
    pageContent.innerHTML = '';

    var tab = getActiveTab();
    var domain = extractDomain(url);
    tab.url = url;
    tab.title = domain || 'Loading...';

    if (!skipHistory) {
      /* Trim forward history */
      tab.history = tab.history.slice(0, tab.historyIndex + 1);
      tab.history.push(url);
      tab.historyIndex = tab.history.length - 1;
    }

    renderTabs();
    updateNavButtons();

    /* Simulate loading delay */
    setTimeout(function () {
      pageLoading.classList.remove('active');
      var page = simulatedPages[domain];
      if (page) {
        tab.title = page.title;
        pageContent.innerHTML = page.html;
      } else {
        tab.title = domain || url;
        pageContent.innerHTML =
          '<div class="sim-header"><h1>' + escapeHtml(domain || url) + '</h1>' +
          '<p>Page loaded successfully.</p></div>' +
          '<div class="sim-block"></div><div class="sim-block"></div>' +
          '<div class="sim-block"></div><div class="sim-block"></div>' +
          '<div class="sim-block"></div><div class="sim-block"></div>' +
          '<div class="sim-block"></div><div class="sim-block"></div>';
      }
      renderTabs();
    }, 600 + Math.random() * 400);
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
    return 'https://search.bpios.dev/?q=' + encodeURIComponent(trimmed);
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

  document.querySelectorAll('.bookmark-item').forEach(function (item) {
    item.addEventListener('click', function () {
      var url = item.dataset.url;
      urlInput.value = url;
      showWebPage(url);
      bookmarksSidebar.classList.add('hidden');
    });
  });

  /* ─── Quick Links ─── */
  document.querySelectorAll('.quick-link').forEach(function (link) {
    link.addEventListener('click', function () {
      var url = link.dataset.url;
      urlInput.value = url;
      showWebPage(url);
    });
  });

  /* ─── Menu Button (placeholder) ─── */
  btnMenu.addEventListener('click', function () {
    /* Future: dropdown menu */
  });

  /* ─── Init ─── */
  renderTabs();
  updateNavButtons();

})();
