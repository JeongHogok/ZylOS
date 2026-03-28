// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: Zyl OS 디바이스 에뮬레이터 메인 로직
// 수행범위: 앱 라우팅, 제스처 시뮬레이션, 상태바, 시스템 로그
// 의존방향: 없음 (standalone)
// SOLID: SRP — 에뮬레이터 셸 로직만 담당
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── App Registry ─── */
  var APPS = {
    'com.zylos.lockscreen': { name: 'Lock Screen', path: '../apps/lockscreen/index.html', system: true },
    'com.zylos.home':       { name: 'Home',        path: '../apps/home/index.html',       system: true },
    'com.zylos.settings':   { name: 'Settings',    path: '../apps/settings/index.html',    system: true },
    'com.zylos.browser':    { name: 'Browser',     path: '../apps/browser/index.html',     system: true },
    'com.zylos.files':      { name: 'Files',       path: '../apps/files/index.html',       system: true },
    'com.zylos.terminal':   { name: 'Terminal',     path: '../apps/terminal/index.html',    system: true },
    'com.zylos.camera':     { name: 'Camera',      path: '../apps/camera/index.html',      system: true },
  };

  /* ─── State ─── */
  var state = {
    currentApp: null,
    runningApps: [],    // [{id, name}]
    screenOn: true,
    locked: true,
  };

  /* ─── DOM ─── */
  var frame       = document.getElementById('device-frame');
  var appFrame    = document.getElementById('app-frame');
  var viewport    = document.getElementById('app-viewport');
  var emuTime     = document.getElementById('emu-time');
  var emuBattery  = document.getElementById('emu-battery');
  var logEl       = document.getElementById('sys-log');
  var appsList    = document.getElementById('running-apps-list');

  /* ─── System Log ─── */
  function syslog(msg, type) {
    type = type || 'info';
    var ts = new Date().toLocaleTimeString('en-US', { hour12: false });
    var cls = 'log-' + type;
    var line = document.createElement('div');
    line.innerHTML = '<span class="' + cls + '">[' + ts + ']</span> ' + msg;
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
  }

  /* ─── Statusbar Clock ─── */
  function updateClock() {
    var now = new Date();
    emuTime.textContent =
      String(now.getHours()).padStart(2, '0') + ':' +
      String(now.getMinutes()).padStart(2, '0');
  }
  updateClock();
  setInterval(updateClock, 1000);

  /* ─── Launch App ─── */
  function launchApp(appId) {
    var app = APPS[appId];
    if (!app) {
      syslog('App not found: ' + appId, 'warn');
      return;
    }

    syslog('Launching: ' + app.name, 'app');

    viewport.classList.add('launching');
    setTimeout(function () { viewport.classList.remove('launching'); }, 300);

    appFrame.src = app.path;
    state.currentApp = appId;

    /* 실행 중 목록에 추가 */
    if (!state.runningApps.find(function (a) { return a.id === appId; })) {
      state.runningApps.push({ id: appId, name: app.name });
    }
    updateRunningApps();
  }

  /* ─── Close App ─── */
  function closeApp(appId) {
    state.runningApps = state.runningApps.filter(function (a) { return a.id !== appId; });
    syslog('Closed: ' + (APPS[appId] ? APPS[appId].name : appId), 'sys');

    if (state.currentApp === appId) {
      goHome();
    }
    updateRunningApps();
  }

  /* ─── Go Home ─── */
  function goHome() {
    syslog('→ Home', 'sys');
    launchApp('com.zylos.home');
  }

  /* ─── Go Back ─── */
  function goBack() {
    syslog('← Back', 'sys');
    try {
      var iframeWin = appFrame.contentWindow;
      if (iframeWin && iframeWin.history.length > 1) {
        iframeWin.history.back();
      } else {
        goHome();
      }
    } catch (e) {
      goHome();
    }
  }

  /* ─── Show Recents ─── */
  function showRecents() {
    syslog('Recents: ' + state.runningApps.length + ' apps', 'sys');
    /* 간단히 로그만 표시 — 향후 오버레이 구현 */
  }

  /* ─── Update Running Apps UI ─── */
  function updateRunningApps() {
    appsList.innerHTML = '';
    if (state.runningApps.length === 0) {
      appsList.innerHTML = '<div class="no-apps">No running apps</div>';
      return;
    }
    state.runningApps.forEach(function (app) {
      var item = document.createElement('div');
      item.className = 'running-app-item' + (app.id === state.currentApp ? ' active' : '');

      var nameSpan = document.createElement('span');
      nameSpan.textContent = app.name;
      nameSpan.style.cursor = 'pointer';
      nameSpan.onclick = function () { launchApp(app.id); };

      var closeBtn = document.createElement('button');
      closeBtn.className = 'app-close-btn';
      closeBtn.textContent = '×';
      closeBtn.onclick = function (e) { e.stopPropagation(); closeApp(app.id); };

      item.appendChild(nameSpan);
      if (!APPS[app.id] || !APPS[app.id].system || app.id !== 'com.zylos.home') {
        item.appendChild(closeBtn);
      }
      appsList.appendChild(item);
    });
  }

  /* ─── Power Button ─── */
  document.getElementById('btn-power').addEventListener('click', function () {
    state.screenOn = !state.screenOn;
    frame.classList.toggle('screen-off', !state.screenOn);
    syslog('Screen ' + (state.screenOn ? 'ON' : 'OFF'), 'sys');

    if (state.screenOn && state.locked) {
      launchApp('com.zylos.lockscreen');
    }
  });

  /* ─── Home Indicator (swipe up) ─── */
  var indicatorEl = document.getElementById('home-indicator');
  var touchStartY = 0;

  indicatorEl.addEventListener('mousedown', function (e) {
    touchStartY = e.clientY;
  });
  indicatorEl.addEventListener('mouseup', function (e) {
    var dy = touchStartY - e.clientY;
    if (dy > 20 || touchStartY === 0) {
      goHome();
    }
  });
  indicatorEl.addEventListener('click', function () {
    goHome();
  });

  /* ─── Control Buttons ─── */
  document.getElementById('btn-home').addEventListener('click', goHome);
  document.getElementById('btn-back').addEventListener('click', goBack);
  document.getElementById('btn-recents').addEventListener('click', showRecents);

  document.getElementById('btn-volup').addEventListener('click', function () {
    syslog('Volume Up', 'sys');
  });
  document.getElementById('btn-voldown').addEventListener('click', function () {
    syslog('Volume Down', 'sys');
  });
  document.getElementById('btn-screenshot').addEventListener('click', function () {
    syslog('Screenshot saved', 'sys');
  });

  /* ─── Device Select ─── */
  document.getElementById('device-select').addEventListener('change', function (e) {
    var isTablet = e.target.value === 'tablet';
    frame.classList.toggle('tablet', isTablet);
    syslog('Device: ' + e.target.value, 'sys');
  });

  /* ─── Listen for app messages from iframes ─── */
  window.addEventListener('message', function (e) {
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || !msg.type) return;

      switch (msg.type) {
        case 'app.launch':
          launchApp(msg.appId);
          break;
        case 'app.close':
          if (state.currentApp) closeApp(state.currentApp);
          break;
        case 'app.minimize':
          goHome();
          break;
        case 'unlock':
          state.locked = false;
          syslog('Device unlocked', 'sys');
          goHome();
          break;
      }
    } catch (err) {
      /* ignore non-JSON messages */
    }
  });

  /* ─── Keyboard shortcuts ─── */
  document.addEventListener('keydown', function (e) {
    if (e.key === 'h' && !e.ctrlKey && !e.metaKey && document.activeElement === document.body) {
      goHome();
    } else if (e.key === 'Escape') {
      goBack();
    } else if (e.key === 'l' && !e.ctrlKey && document.activeElement === document.body) {
      state.locked = true;
      launchApp('com.zylos.lockscreen');
      syslog('Device locked', 'sys');
    }
  });

  /* ─── Boot Sequence ─── */
  syslog('Zyl OS v0.1.0 booting...', 'sys');
  syslog('Kernel: Linux 6.6.63 riscv64', 'sys');
  syslog('SoC: SpacemiT K1 (8x X60)', 'sys');
  syslog('RAM: 16384 MB LPDDR4X', 'sys');
  syslog('Display: MIPI DSI 1080x2400', 'sys');
  syslog('GPU: IMG BXE-2-32 (PVR)', 'sys');

  setTimeout(function () {
    syslog('Compositor: zyl-compositor started', 'sys');
    syslog('WAM: zyl-wam started', 'sys');
    syslog('D-Bus: org.zylos.WebAppManager registered', 'sys');
    syslog('Boot complete.', 'sys');

    /* 잠금화면으로 시작 */
    launchApp('com.zylos.lockscreen');
  }, 500);

})();
