// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: Zyl OS 네이티브 에뮬레이터 — Tauri 통합 + 프리부팅 설정 연동
// 수행범위: 앱 라우팅, 잠금 정책, 네비게이션, HAL 자동 감지
// 의존방향: hal-tauri.js | hal-browser.js, config-ui.js, boot-sequence.js
// SOLID: OCP — 디바이스 프로필 배열에 추가만으로 새 기종 지원
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ═══ HAL 자동 감지 ═══ */
  var IS_TAURI = (typeof window !== 'undefined' && typeof window.__TAURI__ !== 'undefined');
  var HAL = IS_TAURI ? window.ZylHalTauri : window.ZylHalBrowser;

  /* ═══════════════════════════════════════════════════════
     디바이스 프로필 정의
     navMode는 하드웨어 속성 — 런타임에 변경 불가
     ═══════════════════════════════════════════════════════ */
  var DEVICE_PROFILES = [
    {
      id: 'zyl-f3-gesture',
      name: 'Zyl Phone F3',
      description: 'Gesture navigation (iOS style)',
      soc: 'SpacemiT K1', ram: '16GB', screen: '1080×2400',
      width: 393, height: 852, radius: 52,
      navMode: 'gesture',     /* 제스처 바 */
      hasNotch: true,
    },
    {
      id: 'zyl-f3-softkeys',
      name: 'Zyl Phone F3 Lite',
      description: '3-button software navigation',
      soc: 'SpacemiT K1', ram: '8GB', screen: '1080×2340',
      width: 393, height: 852, radius: 44,
      navMode: 'softkeys',    /* 소프트키 3버튼 */
      hasNotch: false,
    },
    {
      id: 'zyl-f3-hardware',
      name: 'Zyl Phone F3 Classic',
      description: 'Physical hardware buttons',
      soc: 'SpacemiT K1', ram: '4GB', screen: '720×1280',
      width: 380, height: 820, radius: 36,
      navMode: 'hardware',    /* 물리 버튼 */
      hasNotch: false,
    },
  ];

  /* ═══ 앱 경로 ═══
     부팅 시 Rust가 OS 이미지(.img)를 마운트 → apps/를 ui/apps/로 복사.
     셧다운 시 정리. 따라서 앱은 항상 상대 경로로 접근.
     앱 레지스트리는 OS 레벨 ZylAppRegistry (apps/system/app-registry.js)가 관리.
     서비스 init 시 apps.getInstalled()로 자동 등록된다. */

  /* ═══ State ═══ */
  var device = null;   /* 선택된 디바이스 프로필 */
  var state = {
    booted: false,
    currentApp: null,
    previousApp: null,
    runningApps: [],
    screenOn: true,
    locked: true,
    recentsOpen: false,
  };

  /* ═══ Notifications ═══ */
  var notifications = []; // { id, appId, appName, icon, title, body, channel, priority, timestamp, read }
  var notifIdCounter = 0;

  /* ═══ DOM ═══ */
  var emulatorScreen  = document.getElementById('emulator-screen');
  var pickerEl        = document.getElementById('device-picker') || document.getElementById('config-screen');
  var deviceListEl    = document.getElementById('device-list') || document.createElement('div');
  var frameEl         = document.getElementById('device-frame');
  var controlEl       = document.getElementById('control-panel');
  var appFrame        = document.getElementById('app-frame');
  var viewport        = document.getElementById('app-viewport');
  var emuTime         = document.getElementById('emu-time');
  var logEl           = document.getElementById('sys-log');
  var appsList        = document.getElementById('running-apps-list');
  var recentsOverlay  = document.getElementById('recents-overlay');
  var recentsCards    = document.getElementById('recents-cards');
  var recentsEmpty    = document.getElementById('recents-empty');
  var navSoftkeys     = document.getElementById('nav-softkeys');
  var navGesture      = document.getElementById('nav-gesture');
  var navHardware     = document.getElementById('nav-hardware');
  var gestureBar      = document.getElementById('gesture-bar');
  var deviceInfoEl    = document.getElementById('device-info-header');
  var notchEl         = document.getElementById('device-notch');

  /* ═══ Helpers ═══ */
  function esc(str) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(str || ''));
    return d.innerHTML;
  }

  /* ═══ Syslog ═══ */
  function syslog(msg, type) {
    if (!logEl) return;
    var ts = new Date().toLocaleTimeString('en-US', { hour12: false });
    var line = document.createElement('div');
    line.innerHTML = '<span class="log-' + esc(type || 'info') + '">[' + ts + ']</span> ' + esc(msg);
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
  }

  /* ═══ Clock ═══ */
  function updateClock() {
    if (!emuTime) return;
    var now = new Date();
    emuTime.textContent =
      String(now.getHours()).padStart(2, '0') + ':' +
      String(now.getMinutes()).padStart(2, '0');
  }
  updateClock();
  var _clockInterval = setInterval(updateClock, 1000);

  /* ═══════════════════════════════════════════════════════
     잠금 상태 정책
     잠금 중에는 모든 네비게이션을 차단한다
     ═══════════════════════════════════════════════════════ */
  function isLocked() {
    return state.locked;
  }

  function requireUnlock(action) {
    if (!isLocked()) return false;
    syslog('[BLOCKED] ' + action + ' — device is locked', 'warn');
    /* 잠금화면이 아닌 다른 곳이면 잠금화면으로 강제 */
    if (state.currentApp !== 'com.zylos.lockscreen') {
      launchApp('com.zylos.lockscreen');
    }
    return true;
  }

  /* ═══ App Lifecycle ═══ */
  function launchApp(appId) {
    var app = ZylAppRegistry.getApp(appId);
    if (!app) {
      syslog('App not found: ' + appId, 'warn');
      showToast({
        icon: '📦',
        appName: 'System',
        title: 'App not installed',
        body: appId.replace('com.zylos.', '') + ' is not available',
        appId: null
      });
      return;
    }
    if (state.recentsOpen) hideRecents();

    /* 잠금 중에는 잠금화면만 실행 가능 */
    if (isLocked() && appId !== 'com.zylos.lockscreen') {
      syslog('[BLOCKED] Cannot launch ' + app.name + ' while locked', 'warn');
      return;
    }

    var appUrl = ZylAppRegistry.getPath(appId) + '?v=' + Date.now();
    syslog('Launch: ' + app.name + ' → ' + appUrl, 'app');
    viewport.classList.add('launching');
    setTimeout(function () { viewport.classList.remove('launching'); }, 300);

    state.previousApp = state.currentApp;
    state.currentApp = appId;

    /* Background execution limit — iframe unload on app switch
     * 앱 전환 시 이전 앱의 iframe src를 about:blank으로 설정하여
     * 백그라운드 JS 실행을 중단한다. recents에서 앱 카드를 클릭하면
     * launchApp()이 다시 호출되어 iframe을 재로드한다. */
    if (state.previousApp && state.previousApp !== appId) {
      appFrame.src = 'about:blank';
    }

    /* Apply OS sandbox policy before loading app */
    if (typeof ZylSandbox !== 'undefined') {
      appFrame.setAttribute('sandbox', ZylSandbox.SANDBOX_FLAGS);
      var effectivePerms = (typeof ZylPermissions !== 'undefined')
        ? ZylPermissions.getEffectivePermissions(appId) : [];
      appFrame.setAttribute('allow', ZylSandbox.getPolicy(appId, effectivePerms));
    }

    appFrame.src = appUrl;

    /* iframe 로드 에러 감지 */
    appFrame.onerror = function () {
      syslog('[ERROR] Failed to load: ' + appUrl, 'warn');
    };

    /* 앱 로드 후 상태 전달 — JS 실행 완료 대기 위해 200ms 지연 */
    var loadAppId = appId;
    appFrame.onload = function () {
      syslog('[LOADED] ' + loadAppId, 'sys');
      /* Runtime CSS is loaded by each app via <link> in its own HTML.
         The emulator does NOT inject anything into iframes. */
      setTimeout(function () {
        /* Inject saved locale into every app — always send, even if 'en' */
        var savedLocale = ZylEmuI18n.getLocale();
        if (savedLocale) {
          broadcastToCurrentApp('system.setLocale', { locale: savedLocale });
        }

        /* Lockscreen: notifications + PIN */
        if (loadAppId === 'com.zylos.lockscreen') {
          notifications.forEach(function (n) {
            broadcastToCurrentApp('notification.push', n);
          });
          /* Always send PIN state — lockscreen decides behavior based on empty vs set */
          broadcastToCurrentApp('settings.pinChanged', { pin: _currentPin });
        }
        /* Home: wallpaper */
        if (loadAppId === 'com.zylos.home') {
          if (_currentWallpaper !== 'default') {
            broadcastToCurrentApp('settings.wallpaperChanged', { wallpaper: _currentWallpaper });
          }
        }
      }, 200);
      appFrame.onload = null;
    };

    /* System-only apps never appear in recents — policy from OS */
    var excludeFromRecents = (typeof ZylAppRegistry !== 'undefined' && ZylAppRegistry.isExcludedFromRecents)
      ? ZylAppRegistry.isExcludedFromRecents(appId) : false;
    if (!excludeFromRecents) {
      if (!state.runningApps.find(function (a) { return a.id === appId; })) {
        state.runningApps.push({ id: appId, name: app.name });
      }
    }
    updateRunningApps();
  }

  function closeApp(appId) {
    state.runningApps = state.runningApps.filter(function (a) { return a.id !== appId; });
    var closedApp = ZylAppRegistry.getApp(appId);
    syslog('Close: ' + (closedApp ? closedApp.name : appId), 'sys');
    if (state.currentApp === appId) goHome();
    updateRunningApps();
  }

  /**
   * Force-stop an app — kills its iframe, clears watchdog state,
   * and releases all resources. Used for misbehaving / hung apps.
   */
  function forceStopApp(appId) {
    if (!appId) return;
    /* Block further service calls from this app */
    if (typeof ZylSystemServices !== 'undefined' && ZylSystemServices.watchdog) {
      ZylSystemServices.watchdog.block(appId);
    }
    /* Destroy the app iframe if it's the current app */
    var appFrame = document.getElementById('app-frame');
    if (appFrame && state.currentApp === appId) {
      appFrame.src = 'about:blank';
    }
    /* Remove from running apps */
    closeApp(appId);
    /* Unblock after a brief delay (allows relaunch) */
    setTimeout(function () {
      if (typeof ZylSystemServices !== 'undefined' && ZylSystemServices.watchdog) {
        ZylSystemServices.watchdog.unblock(appId);
      }
    }, 1000);
    syslog('ForceStop: ' + appId, 'sys');
  }

  /* 잠금 검사를 거치는 네비게이션 */
  /* OOBE blocks all navigation — user must complete setup */
  function isInOobe() {
    return state.currentApp === 'com.zylos.oobe';
  }

  function goHome() {
    if (requireUnlock('Home')) return;
    if (isInOobe()) return;
    if (state.currentApp === 'com.zylos.home') return;
    launchApp('com.zylos.home');
  }

  var _backResponsePending = false;

  function goBack() {
    if (requireUnlock('Back')) return;
    if (isInOobe()) return;
    if (state.currentApp === 'com.zylos.home') return;
    if (_backResponsePending) return;

    syslog('\u2190 Back', 'sys');

    /* Ask the app to handle back navigation internally.
       The app should respond with navigation.handled (stayed in app)
       or navigation.exit (wants to leave). If no response in 300ms, go home. */
    _backResponsePending = true;
    broadcastToCurrentApp('navigation.back', {});

    var backTimer = setTimeout(function () {
      /* No response from app — treat as exit */
      _backResponsePending = false;
      goHome();
    }, 300);

    /* One-time listener for the app's response */
    function onBackResponse(e) {
      try {
        var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
        if (!msg) return;
        if (msg.type === 'navigation.handled') {
          /* App handled it internally — stay in app */
          clearTimeout(backTimer);
          _backResponsePending = false;
          window.removeEventListener('message', onBackResponse);
        } else if (msg.type === 'navigation.exit') {
          /* App says go home */
          clearTimeout(backTimer);
          _backResponsePending = false;
          window.removeEventListener('message', onBackResponse);
          goHome();
        }
      } catch (err) { /* ignore */ }
    }
    window.addEventListener('message', onBackResponse);

    /* Auto-cleanup listener after timeout */
    setTimeout(function () {
      window.removeEventListener('message', onBackResponse);
    }, 350);
  }

  function showRecents() {
    if (requireUnlock('Recents')) return;
    if (isInOobe()) return;
    state.recentsOpen = true;
    recentsOverlay.classList.remove('hidden');
    renderRecentsCards();
    syslog('Recents', 'sys');
  }

  function hideRecents() {
    state.recentsOpen = false;
    recentsOverlay.classList.add('hidden');
  }

  function toggleRecents() {
    if (state.recentsOpen) hideRecents();
    else showRecents();
  }

  function switchApp(direction) {
    if (requireUnlock('App switch')) return;
    var apps = state.runningApps.filter(function (a) {
      return a.id !== 'com.zylos.lockscreen';
    });
    if (apps.length < 2) return;
    var idx = apps.findIndex(function (a) { return a.id === state.currentApp; });
    if (idx < 0) return;
    var next = direction > 0
      ? (idx + 1) % apps.length
      : (idx - 1 + apps.length) % apps.length;
    launchApp(apps[next].id);
    syslog('Switch → ' + apps[next].name, 'sys');
  }

  /* ═══ Recents UI ═══ */
  function renderRecentsCards() {
    if (!recentsCards) return;
    recentsCards.innerHTML = '';
    var apps = state.runningApps.filter(function (a) { return a.id !== 'com.zylos.lockscreen'; });
    if (recentsEmpty) recentsEmpty.style.display = apps.length === 0 ? 'block' : 'none';
    apps.forEach(function (app) {
      var card = document.createElement('div');
      card.className = 'recents-card';
      card.style.cursor = 'pointer';
      card.onclick = function () { hideRecents(); launchApp(app.id); };
      var nameEl = document.createElement('span');
      nameEl.className = 'recents-card-name';
      nameEl.textContent = app.name;
      var close = document.createElement('button');
      close.className = 'recents-card-close';
      close.textContent = '\u00d7';
      close.onclick = function (e) { e.stopPropagation(); closeApp(app.id); renderRecentsCards(); if (state.runningApps.length === 0) hideRecents(); };
      card.appendChild(nameEl);
      if (app.id !== 'com.zylos.home') card.appendChild(close);
      recentsCards.appendChild(card);
    });
  }

  if (recentsOverlay) recentsOverlay.addEventListener('click', function (e) {
    if (e.target === recentsOverlay) hideRecents();
  });

  /* ═══ Running Apps (side panel) ═══ */
  function updateRunningApps() {
    appsList.innerHTML = '';
    if (state.runningApps.length === 0) {
      appsList.innerHTML = '<div class="no-apps">No running apps</div>';
      return;
    }
    state.runningApps.forEach(function (app) {
      var item = document.createElement('div');
      item.className = 'running-app-item' + (app.id === state.currentApp ? ' active' : '');
      var name = document.createElement('span');
      name.textContent = app.name;
      name.style.cursor = 'pointer';
      name.onclick = function () { launchApp(app.id); };
      var closeBtn = document.createElement('button');
      closeBtn.className = 'app-close-btn';
      closeBtn.textContent = '×';
      closeBtn.onclick = function (e) { e.stopPropagation(); closeApp(app.id); };
      item.appendChild(name);
      if (app.id !== 'com.zylos.home') item.appendChild(closeBtn);
      appsList.appendChild(item);
    });
  }

  /* ═══════════════════════════════════════════════════════
     네비게이션 모드 적용 (디바이스 프로필에서 결정)
     ═══════════════════════════════════════════════════════ */
  function applyNavMode(mode) {
    navSoftkeys.classList.add('hidden');
    navGesture.classList.add('hidden');
    navHardware.classList.add('hidden');
    frameEl.classList.remove('nav-hw');

    if (mode === 'softkeys')  navSoftkeys.classList.remove('hidden');
    if (mode === 'gesture')   navGesture.classList.remove('hidden');
    if (mode === 'hardware')  { navHardware.classList.remove('hidden'); frameEl.classList.add('nav-hw'); }

    /* Set keyboard position based on nav mode */
    var kb = document.getElementById('keyboard-container');
    if (kb) kb.setAttribute('data-nav', mode || 'gesture');
  }

  /* ── 소프트키 ── */
  function bindClick(id, fn) { var el = document.getElementById(id); if (el) el.addEventListener('click', fn); }
  bindClick('sk-back', goBack);
  bindClick('sk-home', goHome);
  bindClick('sk-recents', toggleRecents);

  /* ── 물리 버튼 ── */
  bindClick('hw-back', goBack);
  bindClick('hw-home', goHome);
  bindClick('hw-recents', toggleRecents);

  /* ── Gesture Bar — via shared ZylTouch engine ── */
  if (navGesture && typeof ZylTouch !== 'undefined') {
    ZylTouch.createGestureBar(navGesture, {
      onSwipeUp: goHome,
      onSwipeUpSmall: showRecents,
      onSwipeLeft: function () { switchApp(1); },
      onSwipeRight: function () { switchApp(-1); },
      upBigThreshold: 60,
      upSmallThreshold: 20,
      horizontalThreshold: 30,
      velocityThreshold: 400
    });

    /* Tap (no swipe) → home */
    navGesture.addEventListener('click', function () { goHome(); });
  }

  /* ═══════════════════════════════════════════════════════
     상태바 풀다운 → 퀵설정 패널
     ═══════════════════════════════════════════════════════ */
  var qsPanel   = document.getElementById('qs-panel');
  var statusbar = document.getElementById('emu-statusbar');
  var qsTime    = document.getElementById('qs-time');
  var qsDate    = document.getElementById('qs-date');
  var qsOpen    = false;
  var sbDrag    = { active: false, startY: 0 };

  function updateQsClock() {
    var now = new Date();
    if (qsTime) qsTime.textContent = String(now.getHours()).padStart(2, '0') + ':' + String(now.getMinutes()).padStart(2, '0');
    var emuLocale = (typeof ZylEmuI18n !== 'undefined') ? ZylEmuI18n.getLocale() : 'en';
    if (qsDate) qsDate.textContent = now.toLocaleDateString(emuLocale, { month: 'long', day: 'numeric', weekday: 'long' });
  }

  var qsBackdrop = document.getElementById('qs-backdrop');

  function openQsPanel() {
    if (qsOpen || isLocked() || isInOobe()) return;
    qsOpen = true;
    updateQsClock();
    renderQsNotifications();
    qsPanel.classList.remove('qs-hidden');
    qsPanel.style.transform = 'translateY(0)';
    qsPanel.style.opacity = '1';
    qsPanel.style.pointerEvents = '';
    if (qsBackdrop) { qsBackdrop.classList.add('qs-backdrop-show'); qsBackdrop.style.opacity = ''; }
    /* Block iframe from receiving ANY events while QS is open */
    if (appFrame) appFrame.style.pointerEvents = 'none';
  }

  function closeQsPanel() {
    if (!qsOpen) return;
    qsOpen = false;
    qsPanel.style.transform = '';
    qsPanel.style.opacity = '';
    qsPanel.classList.add('qs-hidden');
    if (qsBackdrop) { qsBackdrop.classList.remove('qs-backdrop-show'); qsBackdrop.style.opacity = ''; }
    /* Restore iframe events */
    if (appFrame) appFrame.style.pointerEvents = '';
  }

  /* ═══ QS Panel Drag — delegated to ZylTouch.createPanelDrag ═══ */
  var qsPanelDrag = null;
  var _qsDragMoved = false;

  function blockIframe(block) {
    if (appFrame) appFrame.style.pointerEvents = block ? 'none' : '';
  }

  if (typeof ZylTouch !== 'undefined' && statusbar && qsPanel) {
    qsPanelDrag = ZylTouch.createPanelDrag(statusbar, qsPanel, {
      direction: 'down',
      threshold: 40,
      getHeight: function () { return qsPanel.offsetHeight || 400; },
      onOpen: function () {
        qsOpen = true;
        updateQsClock();
        renderQsNotifications();
        qsPanel.classList.remove('qs-hidden');
        qsPanel.style.pointerEvents = '';
        if (qsBackdrop) { qsBackdrop.classList.add('qs-backdrop-show'); qsBackdrop.style.opacity = ''; }
      },
      onClose: function () {
        qsOpen = false;
        qsPanel.classList.add('qs-hidden');
        if (qsBackdrop) { qsBackdrop.classList.remove('qs-backdrop-show'); qsBackdrop.style.opacity = ''; }
      }
    });
  }

  /* Statusbar: drag down to open, click to toggle */
  statusbar.addEventListener('mousedown', function (e) {
    _qsDragMoved = false;
    if (qsPanelDrag && !isLocked() && !isInOobe()) qsPanelDrag.start(e.clientY, blockIframe);
  });
  statusbar.addEventListener('touchstart', function (e) {
    _qsDragMoved = false;
    if (qsPanelDrag && !isLocked() && !isInOobe()) qsPanelDrag.start(e.touches[0].clientY, blockIframe);
  }, { passive: true });

  statusbar.addEventListener('click', function () {
    if (_qsDragMoved) return;
    if (qsOpen) closeQsPanel(); else openQsPanel();
  });

  /* Block click-through on QS panel */
  qsPanel.addEventListener('click', function (e) { e.stopPropagation(); });

  /* Panel: drag up to close */
  qsPanel.addEventListener('mousedown', function (e) {
    if (e.target.closest('button, input, .qs-tile, .qs-notif-card')) return;
    _qsDragMoved = false;
    if (qsPanelDrag && !isLocked() && !isInOobe()) qsPanelDrag.start(e.clientY, blockIframe);
  });
  qsPanel.addEventListener('touchstart', function (e) {
    if (e.target.closest('button, input, .qs-tile, .qs-notif-card')) return;
    _qsDragMoved = false;
    if (qsPanelDrag && !isLocked() && !isInOobe()) qsPanelDrag.start(e.touches[0].clientY, blockIframe);
  }, { passive: true });

  /* Global move/end listeners */
  document.addEventListener('mousemove', function (e) {
    if (qsPanelDrag && qsPanelDrag.isActive()) { qsPanelDrag.move(e.clientY); _qsDragMoved = true; }
  });
  document.addEventListener('touchmove', function (e) {
    if (qsPanelDrag && qsPanelDrag.isActive()) { qsPanelDrag.move(e.touches[0].clientY); _qsDragMoved = true; }
  }, { passive: true });
  document.addEventListener('mouseup', function () {
    if (qsPanelDrag && qsPanelDrag.isActive()) qsPanelDrag.end(blockIframe);
  });
  document.addEventListener('touchend', function () {
    if (qsPanelDrag && qsPanelDrag.isActive()) qsPanelDrag.end(blockIframe);
  });

  /* 퀵설정 타일 토글 + 서비스 호출 */
  document.querySelectorAll('.qs-tile').forEach(function (tile) {
    tile.addEventListener('click', function () {
      tile.classList.toggle('active');
      var qs = tile.dataset.qs;
      var on = tile.classList.contains('active');
      syslog('QS: ' + qs + ' ' + (on ? 'ON' : 'OFF'), 'sys');
      /* Route ALL QS toggles through services + apply side effects */
      var categoryMap = { wifi: 'wifi', bt: 'bluetooth', airplane: 'network', rotate: 'display', flashlight: 'display' };
      var keyMap = { wifi: 'enabled', bt: 'enabled', airplane: 'airplaneMode', rotate: 'autoRotate', flashlight: 'flashlight' };
      if (qs === 'silent') {
        ZylServices.handleRequest('audio', 'setSilentMode', { enabled: on });
        _systemSilentMode = on;
        applySettingSideEffect('sound', 'silentMode', on);
      } else if (categoryMap[qs]) {
        ZylServices.handleRequest('settings', 'update', { category: categoryMap[qs], key: keyMap[qs], value: on });
        applySettingSideEffect(categoryMap[qs], keyMap[qs], on);
      }
    });
  });

  /* 밝기 슬라이더 → 화면 밝기 반영 */
  var qsBrightness = document.getElementById('qs-brightness');
  if (qsBrightness) {
    qsBrightness.addEventListener('input', function () {
      var pct = parseInt(qsBrightness.value, 10);
      var screen = document.getElementById('device-screen');
      if (screen) screen.style.filter = 'brightness(' + (pct / 100) + ')';
      syslog('Brightness: ' + pct + '%', 'sys');
    });
  }

  /* 패널 핸들 / 백드롭 클릭으로 닫기 */
  if (qsPanel && qsPanel.querySelector('.qs-handle')) qsPanel.querySelector('.qs-handle').addEventListener('click', closeQsPanel);
  if (qsBackdrop) {
    qsBackdrop.addEventListener('click', function (e) {
      e.stopPropagation();
      closeQsPanel();
    });
    /* Block ALL touch/mouse events from reaching app below */
    qsBackdrop.addEventListener('touchstart', function (e) { e.stopPropagation(); }, { passive: false });
    qsBackdrop.addEventListener('mousedown', function (e) { e.stopPropagation(); });
  }

  /* ═══ 제어 패널 ═══ */
  document.getElementById('btn-power').addEventListener('click', powerToggle);
  document.getElementById('btn-volup').addEventListener('click', function () { adjustVolume(5); });
  document.getElementById('btn-voldown').addEventListener('click', function () { adjustVolume(-5); });

  /* 측면 물리 버튼 (디바이스 프레임 옆) */
  function powerToggle() {
    state.screenOn = !state.screenOn;
    frameEl.classList.toggle('screen-off', !state.screenOn);
    syslog('Screen ' + (state.screenOn ? 'ON' : 'OFF'), 'sys');
    /* Screen off: close panels + lock (skip lock during OOBE) */
    if (!state.screenOn) {
      if (qsOpen) closeQsPanel();
      if (state.recentsOpen) hideRecents();
      hideToast();
      if (!isInOobe()) state.locked = true;
    }
    /* Screen on: show lockscreen (or resume OOBE if in setup) */
    if (state.screenOn) {
      if (isInOobe()) {
        launchApp('com.zylos.oobe');
      } else {
        launchApp('com.zylos.lockscreen');
      }
    }
  }
  var sidePower = document.getElementById('side-power');
  var sideVolUp = document.getElementById('side-volup');
  var sideVolDown = document.getElementById('side-voldown');
  if (sidePower) sidePower.addEventListener('click', powerToggle);
  if (sideVolUp) sideVolUp.addEventListener('click', function () { adjustVolume(5); });
  if (sideVolDown) sideVolDown.addEventListener('click', function () { adjustVolume(-5); });
  document.getElementById('btn-reboot').addEventListener('click', function () {
    syslog('Rebooting...', 'warn');
    state.locked = true;
    state.runningApps = [];
    state.currentApp = null;
    updateRunningApps();
    bootDevice(device);
  });

  /* ═══════════════════════════════════════════════════════
     알림 시스템
     ═══════════════════════════════════════════════════════ */
  function pushNotification(data) {
    var notif = {
      id: ++notifIdCounter,
      appId: data.appId || 'com.zylos.system',
      appName: data.appName || (ZylAppRegistry.getApp(data.appId) && ZylAppRegistry.getApp(data.appId).name) || 'System',
      icon: data.icon || '🔔',
      title: data.title || '',
      body: data.body || '',
      channel: data.channel || 'default',
      priority: data.priority || 1,
      timestamp: Date.now(),
      read: false,
    };
    notifications.unshift(notif);
    updateNotifBadge();
    renderQsNotifications();

    /* Play notification sound + vibrate */
    playNotifSound();
    triggerVibrate();

    /* Show toast banner if screen is on and not locked */
    if (state.screenOn && !isLocked()) {
      showToast(notif);
    }

    /* If on lockscreen, push to lockscreen iframe */
    if (state.currentApp === 'com.zylos.lockscreen') {
      broadcastToCurrentApp('notification.push', notif);
    }

    syslog('Notif: ' + notif.title, 'app');
    return notif.id;
  }

  function dismissNotification(id) {
    notifications = notifications.filter(function(n) { return n.id !== id; });
    updateNotifBadge();
    renderQsNotifications();
  }

  function clearAllNotifications() {
    /* 카드 제거 애니메이션 후 실제 삭제 */
    var list = document.getElementById('qs-notif-list');
    var cards = list ? list.querySelectorAll('.qs-notif-card') : [];
    if (cards.length === 0) {
      notifications = notifications.filter(function(n) { return n.persistent; });
      updateNotifBadge();
      renderQsNotifications();
      return;
    }
    cards.forEach(function(card, i) {
      card.style.animation = 'none'; /* CSS animation 제거하여 transition과 충돌 방지 */
      card.style.transition = 'transform 0.3s ease, opacity 0.3s ease';
      card.style.transitionDelay = (i * 50) + 'ms';
      /* 강제 리플로우 후 transform 적용 */
      void card.offsetWidth;
      card.style.transform = 'translateX(100%)';
      card.style.opacity = '0';
    });
    setTimeout(function() {
      notifications = notifications.filter(function(n) { return n.persistent; });
      updateNotifBadge();
      renderQsNotifications();
      syslog('Notifications cleared', 'sys');
    }, cards.length * 50 + 350);
  }

  function updateNotifBadge() {
    var unread = notifications.filter(function(n) { return !n.read; }).length;
    var badge = document.getElementById('notif-badge');
    if (badge) {
      badge.textContent = unread;
      badge.classList.toggle('hidden', unread === 0);
    }
  }

  function broadcastToCurrentApp(type, data) {
    try {
      appFrame.contentWindow.postMessage(JSON.stringify({ type: type, data: data }), '*');
    } catch(e) {}
  }

  /* ═══ Toast Banner ═══ */
  function showToast(notif) {
    var toast = document.getElementById('notif-toast');
    if (!toast) return;
    toast.querySelector('.toast-icon').textContent = notif.icon;
    toast.querySelector('.toast-app').textContent = notif.appName;
    toast.querySelector('.toast-title').textContent = notif.title;
    toast.querySelector('.toast-body').textContent = notif.body;
    toast.classList.remove('toast-hidden', 'toast-show');
    void toast.offsetWidth; /* 강제 리플로우 — transition 보장 */
    toast.classList.add('toast-show');

    toast.onclick = function() {
      hideToast();
      if (notif.appId && ZylAppRegistry.getApp(notif.appId)) launchApp(notif.appId);
    };

    clearTimeout(toast._timer);
    toast._timer = setTimeout(hideToast, 3000);
  }

  function hideToast() {
    var toast = document.getElementById('notif-toast');
    toast.classList.remove('toast-show');
    toast.classList.add('toast-hidden');
  }

  /* ═══ QS Notification List ═══ */
  function renderQsNotifications() {
    var list = document.getElementById('qs-notif-list');
    if (!list) return;
    list.innerHTML = '';

    var visible = notifications.filter(function(n) { return !n.read; });

    if (visible.length === 0) {
      list.innerHTML = '<div class="qs-notif-empty">' + esc(ZylEmuI18n.t('qs.no_notifications')) + '</div>';
      return;
    }

    visible.forEach(function(notif) {
      var card = document.createElement('div');
      card.className = 'qs-notif-card';
      card.innerHTML =
        '<div class="qs-notif-icon">' + esc(notif.icon) + '</div>' +
        '<div class="qs-notif-content">' +
          '<div class="qs-notif-app">' + esc(notif.appName) + '</div>' +
          '<div class="qs-notif-title">' + esc(notif.title) + '</div>' +
          '<div class="qs-notif-body">' + esc(notif.body) + '</div>' +
        '</div>' +
        '<div class="qs-notif-time">' + esc(formatTimeAgo(notif.timestamp)) + '</div>';

      card.addEventListener('click', function() {
        closeQsPanel();
        if (notif.appId && ZylAppRegistry.getApp(notif.appId)) launchApp(notif.appId);
      });

      // Swipe to dismiss
      var sx = 0;
      card.addEventListener('mousedown', function(e) { sx = e.clientX; });
      card.addEventListener('mouseup', function(e) {
        if (Math.abs(e.clientX - sx) > 80) {
          card.style.transform = 'translateX(' + (e.clientX > sx ? '100%' : '-100%') + ')';
          card.style.opacity = '0';
          setTimeout(function() { dismissNotification(notif.id); }, 300);
        }
      });

      list.appendChild(card);
    });

    // Clear all button
    if (visible.length > 0) {
      var clearBtn = document.createElement('button');
      clearBtn.className = 'qs-notif-clear';
      clearBtn.textContent = ZylEmuI18n.t('qs.clear_all');
      clearBtn.addEventListener('click', function(e) {
        e.stopPropagation();
        clearAllNotifications();
      });
      list.appendChild(clearBtn);
    }
  }

  function formatTimeAgo(ts) {
    return ZylEmuI18n.formatTimeAgo(ts);
  }

  /* ═══ System Services IPC ═══ */
  function handleServiceRequest(msg, source) {
    if (!msg.service || !msg.method) return;
    var callerAppId = state.currentApp || '';

    /* Camera access: dynamically grant iframe permissions */
    if (msg.service === 'camera' && msg.method === 'requestAccess') {
      var hasAccess = (typeof ZylPermissions !== 'undefined')
        ? ZylPermissions.hasPermission(callerAppId, 'camera') : false;
      if (hasAccess && typeof ZylSandbox !== 'undefined') {
        var perms = ZylPermissions.getEffectivePermissions(callerAppId);
        appFrame.setAttribute('allow', ZylSandbox.getPolicy(callerAppId, perms));
      }
      var camResponse = JSON.stringify({
        type: 'service.response',
        service: msg.service,
        method: msg.method,
        params: msg.params || {},
        requestId: msg.requestId || null,
        data: { granted: hasAccess }
      });
      try {
        if (source) {
          source.postMessage(camResponse, '*');
        } else if (appFrame && appFrame.contentWindow) {
          appFrame.contentWindow.postMessage(camResponse, '*');
        }
      } catch (err) { /* iframe not ready */ }
      return;
    }

    var result = ZylServices.handleRequest(msg.service, msg.method, msg.params || {}, callerAppId);

    /* ── settings.update side effects ── */
    if (msg.service === 'settings' && msg.method === 'update') {
      var p = msg.params || {};
      applySettingSideEffect(p.category, p.key, p.value);
    }

    function sendResponse(data) {
      var response = JSON.stringify({
        type: 'service.response',
        service: msg.service,
        method: msg.method,
        params: msg.params || {},
        requestId: msg.requestId || null,
        data: data
      });
      try {
        if (source) {
          source.postMessage(response, '*');
        } else if (appFrame && appFrame.contentWindow) {
          appFrame.contentWindow.postMessage(response, '*');
        }
      } catch (err) { /* iframe not ready */ }
    }

    /* Promise 또는 동기 데이터 모두 처리 */
    if (result && typeof result.then === 'function') {
      result.then(function (data) { sendResponse(data); })
            .catch(function () { sendResponse(null); });
    } else {
      sendResponse(result);
    }
  }

  /* ── Apply real side effects when a setting changes ── */
  /* ─── 설정 변경 → 에뮬레이터에 실제 반영 ─── */
  var _currentPin = '';
  var _systemSilentMode = false;
  var _currentWallpaper = 'default';
  var _wallpaperGradients = {
    'default':         'linear-gradient(160deg, #0a0a1a 0%, #0d1b2a 40%, #1b2838 100%)',
    'gradient-blue':   'linear-gradient(135deg, #0c3547 0%, #1a6b8a 50%, #0a2a3a 100%)',
    'gradient-purple': 'linear-gradient(135deg, #1a0a2e 0%, #4a2080 50%, #1a0a2e 100%)',
    'gradient-dark':   'linear-gradient(160deg, #050505 0%, #1a1a1a 50%, #0a0a0a 100%)',
    'gradient-sunset':  'linear-gradient(135deg, #2d1b3d 0%, #b44a2a 50%, #1a0a2e 100%)',
  };

  function applySettingSideEffect(category, key, value) {
    /* ── WiFi: statusbar icon ── */
    if (category === 'wifi' && key === 'enabled') {
      var wifiIcon = document.getElementById('sb-wifi');
      if (wifiIcon) wifiIcon.style.opacity = value ? '0.85' : '0.15';
      syslog('WiFi ' + (value ? 'ON' : 'OFF'), 'sys');
    }

    /* ── Bluetooth: statusbar icon visibility ── */
    if (category === 'bluetooth' && key === 'enabled') {
      var btIcon = document.getElementById('sb-bt');
      if (btIcon) btIcon.classList.toggle('hidden', !value);
      syslog('Bluetooth ' + (value ? 'ON' : 'OFF'), 'sys');
    }

    /* ── 밝기: CSS filter로 화면 밝기 시뮬레이션 ── */
    if (category === 'display' && key === 'brightness') {
      var pct = parseInt(value, 10);
      if (!isNaN(pct)) {
        var b = Math.max(0.1, pct / 100);
        var screen = document.getElementById('device-screen');
        if (screen) screen.style.filter = 'brightness(' + b + ')';
      }
      syslog('Brightness: ' + value + '%', 'sys');
    }

    /* ── 다크모드 ── */
    if (category === 'display' && key === 'darkMode') {
      syslog('Dark mode ' + (value ? 'ON' : 'OFF'), 'sys');
    }

    /* ── 글꼴 크기 ── */
    if (category === 'display' && key === 'fontSize') {
      syslog('Font size: ' + value, 'sys');
    }

    /* ── Sound: statusbar silent/vibrate icons ── */
    if (category === 'sound') {
      if (key === 'silentMode') {
        _systemSilentMode = !!value;
        var silentIcon = document.getElementById('sb-silent');
        var vibrateIcon = document.getElementById('sb-vibrate');
        if (silentIcon) silentIcon.classList.toggle('hidden', !value);
        /* If silent and vibration is on, show vibrate icon */
        if (vibrateIcon) vibrateIcon.classList.toggle('hidden', !value);
      }
      if (key === 'vibration') {
        var vibrateIcon2 = document.getElementById('sb-vibrate');
        /* Show vibrate icon only when vibration is ON and silent mode is ON */
        if (vibrateIcon2) vibrateIcon2.classList.toggle('hidden', !(_systemSilentMode && !!value));
      }
      syslog('Sound: ' + key + ' \u2192 ' + value, 'sys');
    }

    /* ── Keyboard settings → forward to ZylKeyboard ── */
    if (category === 'keyboard' && typeof ZylKeyboard !== 'undefined') {
      if (key === 'languages') {
        var langs = String(value).split(',').filter(Boolean);
        ZylKeyboard.setEnabledLanguages(langs);
        if (langs.length > 0 && langs.indexOf(ZylKeyboard.getLanguage()) === -1) {
          ZylKeyboard.setLanguage(langs[0]);
        }
        syslog('Keyboard languages: ' + value, 'sys');
      }
      if (key === 'keyHeight') {
        ZylKeyboard.setKeyHeight(parseInt(value, 10) || 36);
        syslog('Key height: ' + value + 'px', 'sys');
      }
      if (key === 'soundEnabled') {
        ZylKeyboard.setSoundEnabled(!!value);
      }
      if (key === 'vibrationEnabled') {
        ZylKeyboard.setVibrationEnabled(!!value);
      }
    }

    /* ── Locale change → update compositor i18n ── */
    if (category === 'language' && key === 'locale') {
      ZylEmuI18n.setLocale(String(value));
      syslog('Locale: ' + value, 'sys');
    }

    /* ── PIN changed → save (sent to lockscreen on load) ── */
    if (category === 'security' && key === 'pin') {
      _currentPin = String(value);
      syslog('PIN changed', 'sys');
      syslog('PIN changed via Settings', 'sys');
    }

    /* ── 앱 권한 변경 → 권한 시스템 업데이트 ── */
    if (category === 'app_permissions') {
      syslog('App permissions updated: ' + key, 'sys');
      if (typeof ZylPermissions !== 'undefined' && ZylPermissions.setAppOverride) {
        var revoked = String(value || '').split(',').filter(Boolean);
        ZylPermissions.setAppOverride(key, revoked);
      }
    }

    /* ── 배경화면 변경 → 홈 앱 배경 실제 변경 ── */
    if (category === 'wallpaper' && key === 'current') {
      _currentWallpaper = value;
      syslog('Wallpaper → ' + value, 'sys');
      /* 현재 앱(설정)이 아닌 홈 앱에 전달해야 하므로 저장만.
         홈으로 돌아갈 때 launchApp에서 iframe 로드 후 전파 */
    }
  }

  /* ═══ iframe 메시지 ═══ */
  window.addEventListener('message', function (e) {
    /* S7: Origin verification — only accept messages from our own app iframe
       or the window itself. Reject messages from unknown origins. */
    if (e.source !== appFrame.contentWindow && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || !msg.type) return;
      switch (msg.type) {
        case 'app.launch':
          if (!isLocked()) launchApp(msg.appId);
          break;
        case 'app.close':
          if (state.currentApp && !isLocked()) closeApp(state.currentApp);
          break;
        case 'app.minimize':
          goHome();
          break;
        case 'unlock':
          state.locked = false;
          syslog('Device unlocked', 'sys');
          /* Ensure PIN is loaded from settings before proceeding */
          var pinReload = ZylServices.handleRequest('settings', 'get', { category: 'security' });
          if (pinReload && typeof pinReload.then === 'function') {
            pinReload.then(function (sec) {
              if (sec && sec.pin) _currentPin = String(sec.pin);
              goHome();
            }).catch(function () { goHome(); });
          } else {
            goHome();
          }
          break;
        case 'notification.create':
          pushNotification(msg);
          break;
        case 'notification.dismiss':
          dismissNotification(msg.id);
          break;
        case 'notification.clearAll':
          clearAllNotifications();
          break;
        case 'service.request':
          handleServiceRequest(msg, e.source);
          break;
        case 'system.setLocale':
          if (msg.locale) {
            ZylEmuI18n.setLocale(String(msg.locale));
            syslog('Locale: ' + msg.locale, 'sys');
          }
          break;
        case 'settings.pinChanged':
          /* Forward PIN change to lockscreen if it's running */
          syslog('PIN changed via Settings', 'sys');
          if (state.currentApp !== 'com.zylos.lockscreen') {
            /* Store for when lockscreen loads */
          }
          break;
        case 'settings.wallpaperChanged':
          syslog('Wallpaper → ' + (msg.wallpaper || ''), 'sys');
          break;
      }
    } catch (err) { /* ignore */ }
  });

  /* ═══ Volume Control + OSD ═══ */
  var volumeOsd = document.getElementById('volume-osd');
  var volumeOsdBar = document.getElementById('volume-osd-bar');
  var volumeOsdValue = document.getElementById('volume-osd-value');
  var volumeOsdIcon = document.getElementById('volume-osd-icon');
  var _volumeOsdTimer = null;

  function adjustVolume(delta) {
    var result = ZylServices.handleRequest('audio', 'adjustVolume', { stream: 'media', delta: delta });
    if (result && typeof result.then === 'function') {
      result.then(function (r) {
        if (r) showVolumeOsd(r.value);
        broadcastToCurrentApp('audio.volumeChanged', { stream: 'media', value: r ? r.value : 0 });
      });
    }
    syslog('Vol ' + (delta > 0 ? '+' : '') + delta, 'sys');
  }

  function showVolumeOsd(value) {
    if (!volumeOsd) return;
    volumeOsd.classList.add('visible');
    if (volumeOsdBar) volumeOsdBar.style.height = value + '%';
    if (volumeOsdValue) volumeOsdValue.textContent = value;
    if (volumeOsdIcon) volumeOsdIcon.textContent = value === 0 ? '\uD83D\uDD07' : value < 50 ? '\uD83D\uDD09' : '\uD83D\uDD0A';
    clearTimeout(_volumeOsdTimer);
    _volumeOsdTimer = setTimeout(function () {
      volumeOsd.classList.remove('visible');
    }, 2000);
  }

  /* Play notification sound on toast */
  function playNotifSound() {
    ZylServices.handleRequest('audio', 'playNotificationSound', {});
  }

  function triggerVibrate() {
    ZylServices.handleRequest('audio', 'vibrate', { pattern: [200] });
  }

  /* ═══ 키보드 ═══ */
  document.addEventListener('keydown', function (e) {
    if (!state.booted || document.activeElement !== document.body) return;
    if (e.key === 'h' && !e.ctrlKey && !e.metaKey) goHome();
    else if (e.key === 'Escape') goBack();
    else if (e.key === 'r' && !e.ctrlKey) toggleRecents();
    else if (e.key === 'l' && !e.ctrlKey) {
      state.locked = true;
      launchApp('com.zylos.lockscreen');
      syslog('Locked', 'sys');
    }
    else if (e.key === 'p' && !e.ctrlKey) powerToggle();
    else if (e.key === 'ArrowUp') { e.preventDefault(); adjustVolume(5); }
    else if (e.key === 'ArrowDown') { e.preventDefault(); adjustVolume(-5); }
  });

  /* ═══ Virtual Keyboard (OS keyboard app, compositor-managed) ═══ */
  var kbContainer = document.getElementById('keyboard-container');
  if (typeof ZylKeyboard !== 'undefined' && kbContainer) {
    ZylKeyboard.init(kbContainer, function (key) {
      /* Forward key to current app iframe */
      broadcastToCurrentApp('input.key', { key: key });
      /* Keypress feedback — ALL through audio service (clean architecture).
         The service handles silentMode check internally. */
      var kbSoundOn = ZylKeyboard.getSoundEnabled ? ZylKeyboard.getSoundEnabled() : true;
      var kbVibOn = ZylKeyboard.getVibrationEnabled ? ZylKeyboard.getVibrationEnabled() : true;
      if (kbSoundOn) {
        ZylServices.handleRequest('audio', 'playKeyClick', {});
      }
      if (kbVibOn) {
        ZylServices.handleRequest('audio', 'vibrate', { pattern: [8] });
      }
    });

    /* Prevent keyboard area touches from stealing focus from iframe input */
    var _kbTouching = false;
    kbContainer.addEventListener('mousedown', function (e) {
      _kbTouching = true;
      e.preventDefault(); /* Prevent iframe blur */
      setTimeout(function () { _kbTouching = false; }, 500);
    });
    kbContainer.addEventListener('touchstart', function (e) {
      _kbTouching = true;
      /* Don't preventDefault on touchstart — it blocks the key button click.
         Instead, just set flag to suppress hide during polling. */
      setTimeout(function () { _kbTouching = false; }, 500);
    }, { passive: true });

    /* Detect input focus inside app iframe */
    setInterval(function () {
      if (!appFrame || !appFrame.contentWindow) return;
      /* If user is touching keyboard area, don't hide */
      if (_kbTouching) return;
      try {
        var doc = appFrame.contentDocument || appFrame.contentWindow.document;
        var active = doc.activeElement;
        var isInput = active && (active.tagName === 'INPUT' || active.tagName === 'TEXTAREA');
        var inputType = active ? (active.type || '').toLowerCase() : '';
        var excludeTypes = ['checkbox', 'radio', 'range', 'file', 'color', 'hidden'];
        if (isInput && excludeTypes.indexOf(inputType) === -1) {
          if (!ZylKeyboard.isVisible()) {
            ZylKeyboard.show();
            var kbH = kbContainer.offsetHeight || 0;
            broadcastToCurrentApp('keyboard.show', { height: kbH });
          }
        } else {
          if (ZylKeyboard.isVisible()) {
            ZylKeyboard.hide();
            broadcastToCurrentApp('keyboard.hide', {});
          }
        }
      } catch (e) {
        if (ZylKeyboard.isVisible()) ZylKeyboard.hide();
      }
    }, 300);
  }

  /* ═══════════════════════════════════════════════════════
     Device Selection Screen
     ═══════════════════════════════════════════════════════ */
  DEVICE_PROFILES.forEach(function (profile) {
    var card = document.createElement('div');
    card.className = 'picker-card';
    card.innerHTML =
      '<div class="picker-card-name">' + esc(profile.name) + '</div>' +
      '<div class="picker-card-desc">' + esc(profile.description) + '</div>' +
      '<div class="picker-card-specs">' +
        esc(profile.soc) + ' · ' + esc(profile.ram) + ' · ' + esc(profile.screen) +
        ' · Nav: ' + esc(profile.navMode) +
      '</div>';
    card.addEventListener('click', function () {
      selectDevice(profile);
    });
    deviceListEl.appendChild(card);
  });

  function selectDevice(profile) {
    device = profile;

    /* 설정 화면 숨기고 에뮬레이터 화면 표시 */
    var configScreen = document.getElementById('config-screen');
    var bootScreen = document.getElementById('boot-screen');
    if (configScreen) configScreen.classList.add('hidden');
    if (bootScreen) bootScreen.classList.add('hidden');
    if (emulatorScreen) emulatorScreen.classList.remove('hidden');
    if (pickerEl && pickerEl !== configScreen) pickerEl.classList.add('hidden');

    frameEl.classList.remove('hidden');
    controlEl.classList.remove('hidden');

    /* 디바이스 프레임 크기 적용 */
    frameEl.style.width = profile.width + 'px';
    frameEl.style.minWidth = profile.width + 'px';
    frameEl.style.height = profile.height + 'px';
    frameEl.style.borderRadius = profile.radius + 'px';

    var screen = document.getElementById('device-screen');
    screen.style.borderRadius = (profile.radius - 2) + 'px';

    /* 노치 */
    notchEl.style.display = profile.hasNotch ? '' : 'none';

    /* 디바이스 정보 헤더 */
    deviceInfoEl.innerHTML =
      '<h1>' + esc(profile.name) + '</h1>' +
      '<p class="subtitle">' + esc(profile.soc) + ' · ' + esc(profile.ram) +
      ' · ' + esc(profile.navMode) + ' nav</p>';

    /* 네비게이션 모드 (하드웨어 속성 — 고정) */
    applyNavMode(profile.navMode);

    bootDevice(profile);
  }

  /* ═══ 부팅 시퀀스 ═══ */
  function bootDevice(profile) {
    logEl.innerHTML = '';
    state.booted = false;

    /* 서비스에 디바이스 프로필 적용 */
    if (typeof ZylServices !== 'undefined') {
      ZylServices.device.applyProfile(profile);
      if (ZylServices.storage && ZylServices.storage.prefetch) {
        ZylServices.storage.prefetch();
      }
    }

    syslog('Zyl OS v0.1.0 booting...', 'sys');
    syslog('Device: ' + profile.name, 'sys');
    syslog('SoC: ' + profile.soc + ' (RISC-V)', 'sys');
    syslog('RAM: ' + profile.ram + ' LPDDR4X', 'sys');
    syslog('Nav: ' + profile.navMode + ' (hardware config)', 'sys');
    syslog('GPU: IMG BXE-2-32 (PVR)', 'sys');

    setTimeout(function () {
      syslog('Compositor started', 'sys');
      syslog('WAM started', 'sys');
      syslog('Boot complete', 'sys');
      state.booted = true;
      state.locked = true;

      /* Load persisted PIN from settings (survives emulator restart) */
      var pinLoad = ZylServices.handleRequest('settings', 'get', { category: 'security' });
      if (pinLoad && typeof pinLoad.then === 'function') {
        pinLoad.then(function (sec) {
          if (sec && sec.pin) _currentPin = String(sec.pin);
        });
      }

      /* Load persisted locale */
      var localeLoad = ZylServices.handleRequest('settings', 'get', { category: 'language' });
      if (localeLoad && typeof localeLoad.then === 'function') {
        localeLoad.then(function (lang) {
          if (lang && lang.locale) ZylEmuI18n.setLocale(String(lang.locale));
        });
      }

      /* Load persisted keyboard settings */
      if (typeof ZylKeyboard !== 'undefined') {
        var kbLoad = ZylServices.handleRequest('settings', 'get', { category: 'keyboard' });
        if (kbLoad && typeof kbLoad.then === 'function') {
          kbLoad.then(function (kb) {
            if (!kb) return;
            if (kb.languages) {
              var langs = String(kb.languages).split(',').filter(Boolean);
              ZylKeyboard.setEnabledLanguages(langs);
              if (langs.length > 0) ZylKeyboard.setLanguage(langs[0]);
            }
            if (kb.keyHeight) ZylKeyboard.setKeyHeight(parseInt(kb.keyHeight, 10) || 36);
            if (kb.soundEnabled !== undefined) ZylKeyboard.setSoundEnabled(kb.soundEnabled !== false);
            if (kb.vibrationEnabled !== undefined) ZylKeyboard.setVibrationEnabled(kb.vibrationEnabled !== false);
          });
        }
      }

      /* Check if OOBE was completed; if not, show OOBE first */
      var oobeCheck = ZylServices.handleRequest('settings', 'get', { category: 'system' });
      if (oobeCheck && typeof oobeCheck.then === 'function') {
        oobeCheck.then(function (sys) {
          if (sys && sys.oobe_completed) {
            launchApp('com.zylos.lockscreen');
          } else {
            state.locked = false;
            launchApp('com.zylos.oobe');
          }
        }).catch(function () {
          launchApp('com.zylos.lockscreen');
        });
      } else {
        launchApp('com.zylos.lockscreen');
      }

      /* HAL에서 실제 배터리 정보 가져오기 (중복 init 방지) */
      if (HAL && HAL.battery && !state._batteryStarted) {
        state._batteryStarted = true;
        var batEl = document.getElementById('emu-battery');
        function updateBattery() {
          var s = HAL.battery.getState();
          if (s && s.level >= 0 && batEl) batEl.textContent = s.level + '%';
        }
        if (HAL.battery.init) HAL.battery.init();
        updateBattery();
        if (HAL.battery.onChange) HAL.battery.onChange(updateBattery);
        setInterval(updateBattery, 30000);
        syslog('Battery: ' + (HAL.battery.getState().level || '?') + '%', 'sys');
      }

      /* 환영 알림은 OS 이미지의 앱이 담당 (에뮬레이터는 콘텐츠 생성 안 함) */
    }, 600);
  }

  /* ═══════════════════════════════════════════════════════
     Tauri 설정 화면 통합
     config-ui.js → boot-sequence.js → emulator 전환
     ═══════════════════════════════════════════════════════ */
  function initFromConfig(config, bootInfo) {
    /* config-ui 또는 boot-sequence에서 호출됨 */
    var navMode = String(config.nav_mode || config.navMode || 'gesture').toLowerCase();
    var profile = {
      id: config.profile_id,
      name: config.name,
      soc: config.soc || 'SpacemiT K1',
      ram: config.ram_mb ? (config.ram_mb >= 1024 ? (config.ram_mb / 1024) + 'GB' : config.ram_mb + 'MB') : '2GB',
      screen: config.screen_width + '×' + config.screen_height,
      width: config.frame_width || 393,
      height: config.frame_height || 852,
      radius: config.frame_radius || 52,
      navMode: navMode,
      hasNotch: config.has_notch !== false,
    };

    /* 부팅 정보 로그 (앱은 Rust에서 ui/apps/로 복사됨) */
    if (bootInfo && bootInfo.os_image_mount) {
      syslog('OS image: ' + esc(bootInfo.os_image_mount), 'sys');
    }

    /* 에뮬레이터 화면 표시 */
    if (emulatorScreen) emulatorScreen.classList.remove('hidden');

    /* HAL에 프로필 적용 */
    if (HAL && HAL.deviceInfo && HAL.deviceInfo.applyProfile) {
      HAL.deviceInfo.applyProfile(config);
    }

    /* 리소스 정보 표시 */
    if (bootInfo) {
      var resEl = document.getElementById('resource-info');
      if (resEl) {
        resEl.innerHTML =
          '<div style="font-size:12px;color:#888;line-height:1.6">' +
          'Storage: ' + config.storage_gb + ' GB<br>' +
          'RAM: ' + (config.ram_mb >= 1024 ? (config.ram_mb / 1024) + ' GB' : config.ram_mb + ' MB') + '<br>' +
          'OS: ' + esc(bootInfo.os_image_mount || 'bundled') + '<br>' +
          'Data: ' + esc(bootInfo.mount_point || 'fallback') +
          '</div>';
      }
    }

    selectDevice(profile);
  }

  /* Tauri 모드: config-ui → boot-sequence → emulator 흐름 */
  if (typeof ZylConfigUI !== 'undefined') {
    ZylConfigUI.init(function onBoot(config) {
      if (typeof ZylBootSequence !== 'undefined') {
        ZylBootSequence.start(config, function (cfg, bootInfo) {
          initFromConfig(cfg, bootInfo);
        });
      } else {
        initFromConfig(config, null);
      }
    });
  }

})();
