// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: Zyl OS 네이티브 에뮬레이터 — Tauri 통합 + 프리부팅 설정 연동
// 수행범위: 앱 라우팅, 잠금 정책, 네비게이션, HAL 자동 감지
// 의존방향: hal-tauri.js | hal-browser.js, config-ui.js, boot-sequence.js
// SOLID: OCP — 디바이스 프로필 배열에 추가만으로 새 기종 지원
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
     셧다운 시 정리. 따라서 앱은 항상 상대 경로로 접근. */

  var APPS = {
    'com.zylos.lockscreen': { name: 'Lock Screen', path: 'apps/lockscreen/index.html', system: true },
    'com.zylos.home':       { name: 'Home',        path: 'apps/home/index.html',       system: true },
    'com.zylos.settings':   { name: 'Settings',    path: 'apps/settings/index.html',    system: true },
    'com.zylos.browser':    { name: 'Browser',     path: 'apps/browser/index.html',     system: true },
    'com.zylos.files':      { name: 'Files',       path: 'apps/files/index.html',       system: true },
    'com.zylos.terminal':   { name: 'Terminal',    path: 'apps/terminal/index.html',    system: true },
    'com.zylos.camera':     { name: 'Camera',      path: 'apps/camera/index.html',      system: true },
  };

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
    var app = APPS[appId];
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

    var appUrl = app.path + '?v=' + Date.now();
    syslog('Launch: ' + app.name + ' → ' + appUrl, 'app');
    viewport.classList.add('launching');
    setTimeout(function () { viewport.classList.remove('launching'); }, 300);

    state.previousApp = state.currentApp;
    appFrame.src = appUrl;
    state.currentApp = appId;

    /* iframe 로드 에러 감지 */
    appFrame.onerror = function () {
      syslog('[ERROR] Failed to load: ' + appUrl, 'warn');
    };

    /* 앱 로드 후 상태 전달 — JS 실행 완료 대기 위해 200ms 지연 */
    var loadAppId = appId;
    appFrame.onload = function () {
      syslog('[LOADED] ' + loadAppId, 'sys');
      /* 모바일 시뮬레이션: iframe 내부 텍스트 선택 방지 */
      try {
        var iframeDoc = appFrame.contentDocument || appFrame.contentWindow.document;
        var style = iframeDoc.createElement('style');
        style.textContent = '* { -webkit-user-select: none; user-select: none; -webkit-touch-callout: none; } input, textarea { -webkit-user-select: text; user-select: text; }';
        iframeDoc.head.appendChild(style);
      } catch (e) { /* cross-origin 시 무시 */ }
      setTimeout(function () {
        /* 잠금화면: 알림 + PIN */
        if (loadAppId === 'com.zylos.lockscreen') {
          notifications.forEach(function (n) {
            broadcastToCurrentApp('notification.push', n);
          });
          if (_currentPin !== '0000') {
            broadcastToCurrentApp('settings.pinChanged', { pin: _currentPin });
          }
        }
        /* 홈: 배경화면 */
        if (loadAppId === 'com.zylos.home') {
          if (_currentWallpaper !== 'default') {
            broadcastToCurrentApp('settings.wallpaperChanged', { wallpaper: _currentWallpaper });
          }
        }
      }, 200);
      appFrame.onload = null;
    };

    if (!state.runningApps.find(function (a) { return a.id === appId; })) {
      state.runningApps.push({ id: appId, name: app.name });
    }
    updateRunningApps();
  }

  function closeApp(appId) {
    state.runningApps = state.runningApps.filter(function (a) { return a.id !== appId; });
    syslog('Close: ' + (APPS[appId] ? APPS[appId].name : appId), 'sys');
    if (state.currentApp === appId) goHome();
    updateRunningApps();
  }

  /* 잠금 검사를 거치는 네비게이션 */
  function goHome() {
    if (requireUnlock('Home')) return;
    if (state.currentApp === 'com.zylos.home') return; // already on home
    launchApp('com.zylos.home');
  }

  function goBack() {
    if (requireUnlock('Back')) return;
    /* 홈에서 뒤로가기 → 무시 (홈은 루트) */
    if (state.currentApp === 'com.zylos.home') return;
    syslog('← Back', 'sys');
    try {
      var w = appFrame.contentWindow;
      if (w && w.history.length > 1) w.history.back();
      else goHome();
    } catch (e) { goHome(); }
  }

  function showRecents() {
    if (requireUnlock('Recents')) return;
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
    recentsCards.innerHTML = '';
    var apps = state.runningApps.filter(function (a) { return a.id !== 'com.zylos.lockscreen'; });
    recentsEmpty.style.display = apps.length === 0 ? 'block' : 'none';
    apps.forEach(function (app) {
      var card = document.createElement('div');
      card.className = 'recents-card';
      var name = document.createElement('span');
      name.className = 'recents-card-name';
      name.textContent = app.name;
      name.onclick = function () { launchApp(app.id); };
      var close = document.createElement('button');
      close.className = 'recents-card-close';
      close.textContent = '×';
      close.onclick = function (e) { e.stopPropagation(); closeApp(app.id); renderRecentsCards(); };
      card.appendChild(name);
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

  /* ── 제스처 바 ── */
  var gesture = { active: false, startX: 0, startY: 0, startTime: 0 };

  /* 제스처 이벤트를 nav-gesture 컨테이너 전체에서 감지 */
  var gestureStartPos = null;

  if (navGesture) navGesture.addEventListener('mousedown', function (e) {
    gestureStartPos = { x: e.clientX, y: e.clientY };
    onGStart(e);
  });
  if (navGesture) navGesture.addEventListener('touchstart', function (e) {
    gestureStartPos = { x: e.touches[0].clientX, y: e.touches[0].clientY };
    onGStart({ clientX: e.touches[0].clientX, clientY: e.touches[0].clientY });
  }, { passive: true });

  /* 클릭 폴백: mousedown 없이 click만 온 경우 또는 5px 이내면 → 홈 */
  if (navGesture) navGesture.addEventListener('click', function (e) {
    if (!gestureStartPos) {
      /* mousedown 없이 직접 click (JS .click() 호출 등) */
      goHome();
    } else {
      var dx = Math.abs(e.clientX - gestureStartPos.x);
      var dy = Math.abs(e.clientY - gestureStartPos.y);
      if (dx < 5 && dy < 5) goHome();
    }
    gestureStartPos = null;
  });
  document.addEventListener('mousemove', onGMove);
  document.addEventListener('touchmove', function (e) {
    if (gesture.active) onGMove({ clientX: e.touches[0].clientX, clientY: e.touches[0].clientY });
  }, { passive: true });
  document.addEventListener('mouseup', onGEnd);
  document.addEventListener('touchend', onGEnd);

  function onGStart(e) {
    if (!device || device.navMode !== 'gesture') return;
    gesture.active = true;
    gesture.startX = e.clientX;
    gesture.startY = e.clientY;
    gesture.startTime = Date.now();
    navGesture.classList.add('dragging');
  }

  function onGMove() { /* no-op: 제스처 위치는 onGEnd에서 start/end 좌표로 계산 */ }

  function onGEnd(e) {
    if (!gesture.active) return;
    gesture.active = false;
    navGesture.classList.remove('dragging');

    var endX = e.clientX || (e.changedTouches && e.changedTouches[0].clientX) || gesture.startX;
    var endY = e.clientY || (e.changedTouches && e.changedTouches[0].clientY) || gesture.startY;
    var dx = endX - gesture.startX;
    var dy = gesture.startY - endY;
    var elapsed = Date.now() - gesture.startTime;
    var velocity = dy / Math.max(elapsed, 1) * 1000;

    /* scale(0.85) 보정 — 실제 픽셀 이동이 작으므로 임계값 낮춤 */
    if (Math.abs(dy) > 15 && Math.abs(dy) > Math.abs(dx)) {
      if (dy > 60 || velocity > 400) goHome();
      else if (dy > 20) showRecents();
    } else if (Math.abs(dx) > 30 && Math.abs(dx) > Math.abs(dy)) {
      switchApp(dx < 0 ? 1 : -1);
    }
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
    var days = ['일요일','월요일','화요일','수요일','목요일','금요일','토요일'];
    if (qsDate) qsDate.textContent = (now.getMonth()+1) + '월 ' + now.getDate() + '일 ' + days[now.getDay()];
  }

  var qsBackdrop = document.getElementById('qs-backdrop');

  function openQsPanel() {
    if (qsOpen || isLocked()) return;
    qsOpen = true;
    updateQsClock();
    renderQsNotifications();
    qsPanel.classList.remove('qs-hidden');
    if (qsBackdrop) qsBackdrop.classList.add('qs-backdrop-show');
  }

  function closeQsPanel() {
    if (!qsOpen) return;
    qsOpen = false;
    qsPanel.classList.add('qs-hidden');
    if (qsBackdrop) qsBackdrop.classList.remove('qs-backdrop-show');
  }

  /* 상태바/패널 드래그 — 아래로 열기, 위로 닫기 */
  var qsDrag = { active: false, startY: 0, source: null, moved: false };

  function qsDragStart(y, source) {
    qsDrag.active = true;
    qsDrag.startY = y;
    qsDrag.source = source;
    qsDrag.moved = false;
  }

  function qsDragEnd(y) {
    if (!qsDrag.active) return;
    qsDrag.active = false;
    var dy = y - qsDrag.startY;
    var source = qsDrag.source;

    /* source 초기화 — click 핸들러 이전에 처리됨 */
    /* 주의: click은 mouseup 직후 동기적으로 발생하므로 source를 click에서 참조 후 초기화 */

    if (source === 'statusbar' && dy > 25) {
      openQsPanel();
      qsDrag.source = null; /* click 토글 방지 */
    } else if (source === 'panel' && dy < -25) {
      closeQsPanel();
    } else if (source === 'statusbar' && dy < -25 && qsOpen) {
      closeQsPanel();
      qsDrag.source = null;
    }
  }

  /* 상태바: 클릭으로 토글 + 드래그도 지원 */
  statusbar.addEventListener('mousedown', function (e) { qsDragStart(e.clientY, 'statusbar'); });
  statusbar.addEventListener('touchstart', function (e) { qsDragStart(e.touches[0].clientY, 'statusbar'); }, { passive: true });
  statusbar.addEventListener('click', function (e) {
    /* 드래그로 이미 열었으면 무시, 아니면 토글 */
    if (!qsDrag.moved) {
      if (qsOpen) closeQsPanel(); else openQsPanel();
    }
    qsDrag.moved = false;
    qsDrag.source = null;
  });

  /* 패널 자체 드래그 (위로 올려서 닫기) */
  qsPanel.addEventListener('mousedown', function (e) {
    if (e.target.closest('button, input, .qs-tile')) return;
    qsDragStart(e.clientY, 'panel');
  });
  qsPanel.addEventListener('touchstart', function (e) {
    if (e.target.closest('button, input, .qs-tile')) return;
    qsDragStart(e.touches[0].clientY, 'panel');
  }, { passive: true });

  /* 드래그 이동 감지 */
  document.addEventListener('mousemove', function (e) {
    if (qsDrag.active) qsDrag.moved = true;
  });
  document.addEventListener('touchmove', function () {
    if (qsDrag.active) qsDrag.moved = true;
  });

  /* 공통 드래그 종료 */
  document.addEventListener('mouseup', function (e) { qsDragEnd(e.clientY || 0); });
  document.addEventListener('touchend', function (e) {
    qsDragEnd(e.changedTouches ? e.changedTouches[0].clientY : 0);
  });

  /* 퀵설정 타일 토글 */
  document.querySelectorAll('.qs-tile').forEach(function (tile) {
    tile.addEventListener('click', function () {
      tile.classList.toggle('active');
      syslog('QS: ' + tile.dataset.qs + ' ' + (tile.classList.contains('active') ? 'ON' : 'OFF'), 'sys');
    });
  });

  /* 패널 핸들 / 백드롭 클릭으로 닫기 */
  qsPanel.querySelector('.qs-handle').addEventListener('click', closeQsPanel);
  if (qsBackdrop) qsBackdrop.addEventListener('click', closeQsPanel);

  /* ═══ 제어 패널 ═══ */
  document.getElementById('btn-power').addEventListener('click', powerToggle);
  document.getElementById('btn-volup').addEventListener('click', function () { syslog('Vol+', 'sys'); });
  document.getElementById('btn-voldown').addEventListener('click', function () { syslog('Vol-', 'sys'); });

  /* 측면 물리 버튼 (디바이스 프레임 옆) */
  function powerToggle() {
    state.screenOn = !state.screenOn;
    frameEl.classList.toggle('screen-off', !state.screenOn);
    syslog('Screen ' + (state.screenOn ? 'ON' : 'OFF'), 'sys');
    /* 화면 끌 때 열린 패널 모두 닫기 */
    if (!state.screenOn) {
      if (qsOpen) closeQsPanel();
      if (state.recentsOpen) hideRecents();
      hideToast();
    }
    if (state.screenOn && isLocked()) launchApp('com.zylos.lockscreen');
  }
  var sidePower = document.getElementById('side-power');
  var sideVolUp = document.getElementById('side-volup');
  var sideVolDown = document.getElementById('side-voldown');
  if (sidePower) sidePower.addEventListener('click', powerToggle);
  if (sideVolUp) sideVolUp.addEventListener('click', function () { syslog('Vol+', 'sys'); });
  if (sideVolDown) sideVolDown.addEventListener('click', function () { syslog('Vol-', 'sys'); });
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
      appName: data.appName || (APPS[data.appId] && APPS[data.appId].name) || 'System',
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

    // Show toast banner if screen is on and not locked
    if (state.screenOn && !isLocked()) {
      showToast(notif);
    }

    // If on lockscreen, push to lockscreen iframe
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
    toast.querySelector('.toast-icon').textContent = notif.icon;
    toast.querySelector('.toast-app').textContent = notif.appName;
    toast.querySelector('.toast-title').textContent = notif.title;
    toast.querySelector('.toast-body').textContent = notif.body;
    toast.classList.remove('toast-hidden', 'toast-show');
    void toast.offsetWidth; /* 강제 리플로우 — transition 보장 */
    toast.classList.add('toast-show');

    toast.onclick = function() {
      hideToast();
      if (notif.appId && APPS[notif.appId]) launchApp(notif.appId);
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
      list.innerHTML = '<div class="qs-notif-empty">' + esc('알림이 없습니다') + '</div>';
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
        if (notif.appId && APPS[notif.appId]) launchApp(notif.appId);
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
      clearBtn.textContent = '모두 지우기';
      clearBtn.addEventListener('click', function(e) {
        e.stopPropagation();
        clearAllNotifications();
      });
      list.appendChild(clearBtn);
    }
  }

  function formatTimeAgo(ts) {
    var diff = Math.floor((Date.now() - ts) / 1000);
    if (diff < 60) return '방금';
    if (diff < 3600) return Math.floor(diff / 60) + '분 전';
    if (diff < 86400) return Math.floor(diff / 3600) + '시간 전';
    return Math.floor(diff / 86400) + '일 전';
  }

  /* ═══ System Services IPC ═══ */
  function handleServiceRequest(msg, source) {
    if (!msg.service || !msg.method) return;
    var data = ZylServices.handleRequest(msg.service, msg.method, msg.params || {});

    /* ── settings.update side effects ── */
    if (msg.service === 'settings' && msg.method === 'update') {
      var p = msg.params || {};
      applySettingSideEffect(p.category, p.key, p.value);
    }

    var response = JSON.stringify({
      type: 'service.response',
      service: msg.service,
      method: msg.method,
      params: msg.params || {},
      requestId: msg.requestId || null,  /* H17: 요청-응답 상관 ID */
      data: data
    });
    try {
      if (source) {
        source.postMessage(response, '*');
      } else {
        appFrame.contentWindow.postMessage(response, '*');
      }
    } catch (err) { /* iframe not ready */ }
  }

  /* ── Apply real side effects when a setting changes ── */
  /* ─── 설정 변경 → 에뮬레이터에 실제 반영 ─── */
  var _currentPin = '0000';
  var _currentWallpaper = 'default';
  var _wallpaperGradients = {
    'default':         'linear-gradient(160deg, #0a0a1a 0%, #0d1b2a 40%, #1b2838 100%)',
    'gradient-blue':   'linear-gradient(135deg, #0c3547 0%, #1a6b8a 50%, #0a2a3a 100%)',
    'gradient-purple': 'linear-gradient(135deg, #1a0a2e 0%, #4a2080 50%, #1a0a2e 100%)',
    'gradient-dark':   'linear-gradient(160deg, #050505 0%, #1a1a1a 50%, #0a0a0a 100%)',
    'gradient-sunset':  'linear-gradient(135deg, #2d1b3d 0%, #b44a2a 50%, #1a0a2e 100%)',
  };

  function applySettingSideEffect(category, key, value) {
    /* ── WiFi: 상태바 아이콘 투명도 ── */
    if (category === 'wifi' && key === 'enabled') {
      var sbIcos = document.querySelectorAll('#emu-statusbar .sb-ico');
      if (sbIcos[0]) sbIcos[0].style.opacity = value ? '0.85' : '0.15';
      syslog('WiFi ' + (value ? 'ON' : 'OFF'), 'sys');
    }

    /* ── Bluetooth ── */
    if (category === 'bluetooth' && key === 'enabled') {
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

    /* ── 사운드 ── */
    if (category === 'sound') {
      syslog('Sound: ' + key + ' → ' + value, 'sys');
    }

    /* ── PIN 변경 → 저장 (잠금화면 로드 시 전달) ── */
    if (category === 'security' && key === 'pin') {
      _currentPin = String(value);
      syslog('PIN changed', 'sys');
      syslog('PIN changed via Settings', 'sys');
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
          goHome();
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
  });

  /* ═══════════════════════════════════════════════════════
     디바이스 선택 화면
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
      launchApp('com.zylos.lockscreen');

      /* HAL에서 실제 배터리 정보 가져오기 */
      if (HAL && HAL.battery) {
        var batEl = document.getElementById('emu-battery');
        function updateBattery() {
          var s = HAL.battery.getState();
          if (s && s.level >= 0 && batEl) batEl.textContent = s.level + '%';
        }
        if (HAL.battery.init) HAL.battery.init();
        updateBattery();
        if (HAL.battery.onChange) HAL.battery.onChange(updateBattery);
        var _batteryInterval = setInterval(updateBattery, 30000);
        syslog('Battery: ' + (HAL.battery.getState().level || '?') + '%', 'sys');
      }

      // System welcome notifications
      setTimeout(function() {
        pushNotification({ appId: 'com.zylos.settings', icon: '🔔', title: 'Zyl OS', body: '새로운 업데이트가 있습니다.', channel: 'system', priority: 1 });
        pushNotification({ appId: 'com.zylos.browser', icon: '💬', title: '메시지', body: '안녕하세요! Zyl OS에 오신 것을 환영합니다.', channel: 'message', priority: 2 });
        pushNotification({ appId: 'com.zylos.settings', icon: '🔐', title: '보안', body: 'PIN을 설정하여 기기를 보호하세요.', channel: 'security', priority: 1 });
      }, 100);
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
