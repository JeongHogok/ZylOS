// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: Zyl OS 에뮬레이터 — 3가지 네비게이션 모드 지원
// 수행범위: 앱 라우팅, 네비게이션 전략, 제스처 바, 소프트키, 물리 버튼
// 의존방향: 없음 (standalone)
// SOLID: OCP — 네비게이션 모드를 전략으로 교체, 기존 코드 수정 없이 확장
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ═══ App Registry ═══ */
  var APPS = {
    'com.zylos.lockscreen': { name: 'Lock Screen', path: '../apps/lockscreen/index.html', system: true },
    'com.zylos.home':       { name: 'Home',        path: '../apps/home/index.html',       system: true },
    'com.zylos.settings':   { name: 'Settings',    path: '../apps/settings/index.html',    system: true },
    'com.zylos.browser':    { name: 'Browser',     path: '../apps/browser/index.html',     system: true },
    'com.zylos.files':      { name: 'Files',       path: '../apps/files/index.html',       system: true },
    'com.zylos.terminal':   { name: 'Terminal',    path: '../apps/terminal/index.html',    system: true },
    'com.zylos.camera':     { name: 'Camera',      path: '../apps/camera/index.html',      system: true },
  };

  /* ═══ State ═══ */
  var state = {
    currentApp: null,
    previousApp: null,
    runningApps: [],
    screenOn: true,
    locked: true,
    navMode: 'gesture',   /* 'gesture' | 'softkeys' | 'hardware' */
    recentsOpen: false,
  };

  /* ═══ DOM ═══ */
  var frame           = document.getElementById('device-frame');
  var appFrame        = document.getElementById('app-frame');
  var viewport        = document.getElementById('app-viewport');
  var emuTime         = document.getElementById('emu-time');
  var logEl           = document.getElementById('sys-log');
  var appsList        = document.getElementById('running-apps-list');
  var recentsOverlay  = document.getElementById('recents-overlay');
  var recentsCards    = document.getElementById('recents-cards');
  var recentsEmpty    = document.getElementById('recents-empty');

  /* 네비게이션 요소들 */
  var navSoftkeys = document.getElementById('nav-softkeys');
  var navGesture  = document.getElementById('nav-gesture');
  var navHardware = document.getElementById('nav-hardware');
  var gestureBar  = document.getElementById('gesture-bar');

  /* ═══ System Log ═══ */
  function syslog(msg, type) {
    type = type || 'info';
    var ts = new Date().toLocaleTimeString('en-US', { hour12: false });
    var line = document.createElement('div');
    line.innerHTML = '<span class="log-' + type + '">[' + ts + ']</span> ' + msg;
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
  }

  /* ═══ Clock ═══ */
  function updateClock() {
    var now = new Date();
    emuTime.textContent =
      String(now.getHours()).padStart(2, '0') + ':' +
      String(now.getMinutes()).padStart(2, '0');
  }
  updateClock();
  setInterval(updateClock, 1000);

  /* ═══ App Lifecycle ═══ */
  function launchApp(appId) {
    var app = APPS[appId];
    if (!app) { syslog('App not found: ' + appId, 'warn'); return; }
    if (state.recentsOpen) hideRecents();

    syslog('Launch: ' + app.name, 'app');
    viewport.classList.add('launching');
    setTimeout(function () { viewport.classList.remove('launching'); }, 300);

    state.previousApp = state.currentApp;
    appFrame.src = app.path;
    state.currentApp = appId;

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

  function goHome() {
    syslog('→ Home', 'sys');
    launchApp('com.zylos.home');
  }

  function goBack() {
    syslog('← Back', 'sys');
    try {
      var w = appFrame.contentWindow;
      if (w && w.history.length > 1) { w.history.back(); }
      else { goHome(); }
    } catch (e) { goHome(); }
  }

  /* ═══ Recents (최근 앱) ═══ */
  function showRecents() {
    state.recentsOpen = true;
    recentsOverlay.classList.remove('hidden');
    renderRecentsCards();
    syslog('Recents opened', 'sys');
  }

  function hideRecents() {
    state.recentsOpen = false;
    recentsOverlay.classList.add('hidden');
  }

  function toggleRecents() {
    if (state.recentsOpen) hideRecents();
    else showRecents();
  }

  function renderRecentsCards() {
    recentsCards.innerHTML = '';
    var apps = state.runningApps.filter(function (a) {
      return a.id !== 'com.zylos.lockscreen';
    });
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

  /* 오버레이 클릭으로 닫기 */
  recentsOverlay.addEventListener('click', function (e) {
    if (e.target === recentsOverlay) hideRecents();
  });

  /* ═══ Running Apps (사이드 패널) ═══ */
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
     네비게이션 모드 전환
     ═══════════════════════════════════════════════════════ */
  function setNavMode(mode) {
    state.navMode = mode;

    /* 모든 네비게이션 숨기기 */
    navSoftkeys.classList.add('hidden');
    navGesture.classList.add('hidden');
    navHardware.classList.add('hidden');
    frame.classList.remove('nav-hw');

    switch (mode) {
      case 'softkeys':
        navSoftkeys.classList.remove('hidden');
        break;
      case 'gesture':
        navGesture.classList.remove('hidden');
        break;
      case 'hardware':
        navHardware.classList.remove('hidden');
        frame.classList.add('nav-hw');
        break;
    }
    syslog('Nav mode: ' + mode, 'sys');
  }

  /* ═══════════════════════════════════════════════════════
     모드 A: 소프트키 3버튼 (Android 스타일)
     ═══════════════════════════════════════════════════════ */
  document.getElementById('sk-back').addEventListener('click', goBack);
  document.getElementById('sk-home').addEventListener('click', goHome);
  document.getElementById('sk-recents').addEventListener('click', toggleRecents);

  /* ═══════════════════════════════════════════════════════
     모드 B: 제스처 바 (iOS 스타일)
     스와이프 위로 끝까지 = 홈
     스와이프 위로 중간 + 멈춤 = 최근 앱
     좌우 스와이프 = 이전/다음 앱 전환
     ═══════════════════════════════════════════════════════ */
  var gesture = { active: false, startX: 0, startY: 0, startTime: 0 };

  gestureBar.addEventListener('mousedown', onGestureStart);
  gestureBar.addEventListener('touchstart', function (e) {
    onGestureStart({ clientX: e.touches[0].clientX, clientY: e.touches[0].clientY });
  }, { passive: true });

  document.addEventListener('mousemove', onGestureMove);
  document.addEventListener('touchmove', function (e) {
    if (gesture.active) onGestureMove({ clientX: e.touches[0].clientX, clientY: e.touches[0].clientY });
  }, { passive: true });

  document.addEventListener('mouseup', onGestureEnd);
  document.addEventListener('touchend', onGestureEnd);

  function onGestureStart(e) {
    if (state.navMode !== 'gesture') return;
    gesture.active = true;
    gesture.startX = e.clientX;
    gesture.startY = e.clientY;
    gesture.startTime = Date.now();
    gestureBar.classList.add('dragging');
  }

  function onGestureMove(e) {
    if (!gesture.active) return;
    /* 실시간 피드백은 제스처 바 색상 변화로 표현 */
  }

  function onGestureEnd(e) {
    if (!gesture.active) return;
    gesture.active = false;
    gestureBar.classList.remove('dragging');

    var endX = e.clientX || (e.changedTouches && e.changedTouches[0].clientX) || gesture.startX;
    var endY = e.clientY || (e.changedTouches && e.changedTouches[0].clientY) || gesture.startY;

    var dx = endX - gesture.startX;
    var dy = gesture.startY - endY; /* 양수 = 위로 */
    var elapsed = Date.now() - gesture.startTime;
    var velocity = dy / Math.max(elapsed, 1) * 1000; /* px/sec */

    var absDx = Math.abs(dx);
    var absDy = Math.abs(dy);

    if (absDy > 30 && absDy > absDx) {
      /* 세로 스와이프 */
      if (dy > 120 || velocity > 800) {
        /* 끝까지 빠르게 올리기 → 홈 */
        goHome();
      } else if (dy > 40) {
        /* 중간까지 올리고 멈추기 → 최근 앱 */
        showRecents();
      }
    } else if (absDx > 50 && absDx > absDy) {
      /* 좌우 스와이프 → 앱 전환 */
      var currentIdx = state.runningApps.findIndex(function (a) { return a.id === state.currentApp; });
      if (currentIdx >= 0) {
        var nextIdx;
        if (dx > 0) {
          /* 오른쪽 → 이전 앱 */
          nextIdx = currentIdx - 1;
          if (nextIdx < 0) nextIdx = state.runningApps.length - 1;
        } else {
          /* 왼쪽 → 다음 앱 */
          nextIdx = (currentIdx + 1) % state.runningApps.length;
        }
        var target = state.runningApps[nextIdx];
        if (target && target.id !== 'com.zylos.lockscreen') {
          launchApp(target.id);
          syslog('Swipe switch → ' + target.name, 'sys');
        }
      }
    }
  }

  /* ═══════════════════════════════════════════════════════
     모드 C: 물리 하드웨어 버튼
     ═══════════════════════════════════════════════════════ */
  document.getElementById('hw-back').addEventListener('click', goBack);
  document.getElementById('hw-home').addEventListener('click', goHome);
  document.getElementById('hw-recents').addEventListener('click', toggleRecents);

  /* ═══ 제어 패널 ═══ */
  document.getElementById('btn-power').addEventListener('click', function () {
    state.screenOn = !state.screenOn;
    frame.classList.toggle('screen-off', !state.screenOn);
    syslog('Screen ' + (state.screenOn ? 'ON' : 'OFF'), 'sys');
    if (state.screenOn && state.locked) launchApp('com.zylos.lockscreen');
  });

  document.getElementById('btn-volup').addEventListener('click', function () { syslog('Vol+', 'sys'); });
  document.getElementById('btn-voldown').addEventListener('click', function () { syslog('Vol-', 'sys'); });

  document.getElementById('nav-mode-select').addEventListener('change', function (e) {
    setNavMode(e.target.value);
  });

  /* ═══ iframe 메시지 수신 ═══ */
  window.addEventListener('message', function (e) {
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || !msg.type) return;
      switch (msg.type) {
        case 'app.launch':  launchApp(msg.appId); break;
        case 'app.close':   if (state.currentApp) closeApp(state.currentApp); break;
        case 'app.minimize': goHome(); break;
        case 'unlock':
          state.locked = false;
          syslog('Device unlocked', 'sys');
          goHome();
          break;
      }
    } catch (err) { /* ignore */ }
  });

  /* ═══ 키보드 단축키 ═══ */
  document.addEventListener('keydown', function (e) {
    if (document.activeElement !== document.body) return;
    if (e.key === 'h' && !e.ctrlKey && !e.metaKey) goHome();
    else if (e.key === 'Escape') goBack();
    else if (e.key === 'r' && !e.ctrlKey) toggleRecents();
    else if (e.key === 'l' && !e.ctrlKey) {
      state.locked = true;
      launchApp('com.zylos.lockscreen');
      syslog('Locked', 'sys');
    }
  });

  /* ═══ 부팅 시퀀스 ═══ */
  syslog('Zyl OS v0.1.0 booting...', 'sys');
  syslog('Kernel: Linux 6.6.63 riscv64', 'sys');
  syslog('SoC: SpacemiT K1 (8x X60)', 'sys');
  syslog('RAM: 16384 MB LPDDR4X', 'sys');
  syslog('GPU: IMG BXE-2-32 (PVR)', 'sys');

  setTimeout(function () {
    syslog('Compositor started', 'sys');
    syslog('WAM started', 'sys');
    syslog('Boot complete', 'sys');
    setNavMode('gesture');
    launchApp('com.zylos.lockscreen');
  }, 500);

})();
