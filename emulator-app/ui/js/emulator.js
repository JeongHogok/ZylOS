// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: Zyl OS 네이티브 에뮬레이터 — Tauri 통합 + 프리부팅 설정 연동
// 수행범위: 앱 라우팅, 네비게이션 입력, 두 프레임(앱 · OS 의
//           시스템 UI). 어느 앱이 앞에 올지(설정·잠금·홈·최근 앱)와 시스템 UI
//           표면이 얼마나 보일지는 OS 의 ZylShell 이 정하고 여기는 띄우기만 한다.
//           상태바·알림판·최근 앱은 OS 앱(apps/statusbar)이 그린다
// 의존방향: config-ui.js, boot-sequence.js, app-state.js,
//           OS: ZylShell (apps/system/shell.js), ZylAppRegistry, ZylIntent
// SOLID: OCP — 디바이스 프로필 배열에 추가만으로 새 기종 지원
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

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
    panel: 'bar',        /* the system UI surface: bar | banner | full (the OS's) */
  };

  /* ═══ DOM ═══ */
  var emulatorScreen  = document.getElementById('emulator-screen');
  var pickerEl        = document.getElementById('device-picker') || document.getElementById('config-screen');
  var deviceListEl    = document.getElementById('device-list') || document.createElement('div');
  var frameEl         = document.getElementById('device-frame');
  var controlEl       = document.getElementById('control-panel');
  var appFrame        = document.getElementById('app-frame');
  var systemFrame     = document.getElementById('system-frame');
  var viewport        = document.getElementById('app-viewport');
  var logEl           = document.getElementById('sys-log');
  var appsList        = document.getElementById('running-apps-list');
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

  /* ═══════════════════════════════════════════════════════
     Which app may be in front is the OS's (ZylShell): setup, the lock
     screen, home. show() brings what it decided; open() asks it first.
     ═══════════════════════════════════════════════════════ */
  function show(appId) {
    if (appId) launchApp(appId);
  }

  function open(appId, intent) {
    if (!ZylShell.mayLaunch(appId)) {
      syslog('[BLOCKED] ' + appId + ' — not now (setup or locked)', 'warn');
      return;
    }
    launchApp(appId, intent);
  }

  /* ═══ App Lifecycle ═══ */
  /* ═══ App instance state (Android onSaveInstanceState) ═══
     Before the frame leaves an app, the app is asked for its UI state; on its
     next launch the state is handed back. Asking first is the only reliable
     way — a message posted from pagehide is dropped when the iframe navigates
     (verified: only removing the iframe delivers it). Kept in memory, like
     Android instance state: it survives switching apps, not a reboot. */
  var _displayedApp = null;             /* app whose document is in the frame */
  var _navGen = 0;
  var _appStates = ZylAppStateStore.create({
    ask: function () { broadcastToCurrentApp('lifecycle.save', {}); },
    isRunning: function (id) {
      return state.runningApps.some(function (a) { return a.id === id; });
    },
    log: function (m) { syslog(m, 'warn'); },
    timeoutMs: 150,                     /* a hung app must not block switching */
    maxBytes: 64 * 1024                 /* UI state — drafts, scroll, tab — not data */
  });

  /* intent: what the OS resolved for this app to handle (apps/system/intent.js) */
  function launchApp(appId, intent) {
    var app = ZylAppRegistry.getApp(appId);
    if (!app) {
      syslog('App not found: ' + appId, 'warn');
      return;
    }
    setPanel('bar');

    /* Hardware input hint for the OS. The container reports what input
       hardware exists; apps/shared/form-factor.js decides the form factor.
       A host mouse cannot emulate a TV remote, so this cannot be detected. */
    var inputHint = (device && device.inputModel) ? device.inputModel : 'touch';
    var appUrl = ZylAppRegistry.getPath(appId) +
                 '?v=' + Date.now() + '&zylInput=' + encodeURIComponent(inputHint);
    syslog('Launch: ' + app.name + ' → ' + appUrl, 'app');
    viewport.classList.add('launching');
    setTimeout(function () { viewport.classList.remove('launching'); }, 300);

    state.previousApp = state.currentApp;
    state.currentApp = appId;
    ZylShell.shown(appId);

    /* Leaving the displayed app: ask for its state, then navigate. Navigating
       the frame unloads the old document, which is the background execution
       limit. A newer launch during the wait supersedes this one (gen check),
       so rapid switching never attaches one app's state to another. */
    var gen = ++_navGen;
    var leaving = _displayedApp;
    _appStates.saveThen(leaving, function () {
      if (gen !== _navGen) return;

      /* Apply OS sandbox policy before loading app */
      if (typeof ZylSandbox !== 'undefined') {
        appFrame.setAttribute('sandbox', ZylSandbox.SANDBOX_FLAGS);
        var effectivePerms = (typeof ZylPermissions !== 'undefined')
          ? ZylPermissions.getEffectivePermissions(appId) : [];
        appFrame.setAttribute('allow', ZylSandbox.getPolicy(appId, effectivePerms));
      }

      appFrame.src = appUrl;
      _displayedApp = appId;

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
          broadcastToCurrentApp('system.setTimeZone', { timeZone: _currentTimeZone });

          if (intent) broadcastToCurrentApp('intent', intent);

          /* Hand back the state saved when the user left this app (one-shot:
             leaving again saves it again) */
          var restored = _appStates.take(loadAppId);
          if (restored.found) {
            broadcastToCurrentApp('lifecycle.restore', { state: restored.state });
          }
        }, 200);
        appFrame.onload = null;
      };
    });

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
    /* Closing discards instance state — it is removed from running apps first,
       so the save that goHome() triggers below is not stored either */
    _appStates.discard(appId);
    state.runningApps = state.runningApps.filter(function (a) { return a.id !== appId; });
    var closedApp = ZylAppRegistry.getApp(appId);
    syslog('Close: ' + (closedApp ? closedApp.name : appId), 'sys');
    if (state.currentApp === appId) goHome();
    updateRunningApps();
  }

  /* The OS removed an app (its app store): close it and forget its instance */
  window.addEventListener('zyl:app-removed', function (e) {
    if (e.detail) closeApp(e.detail);
  });

  /* Home: the lock screen while locked, nothing during setup (the OS's) */
  function goHome() {
    setPanel('bar');
    show(ZylShell.home());
  }

  var _backResponsePending = false;

  /* Back goes to the app first; an app that exits goes where home goes */
  function goBack() {
    if (state.panel !== 'bar') { setPanel('bar'); return; }   /* back closes the panel first */
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

  /* ═══ System UI surface ═══
     The OS's status bar app in its own frame over the app: the OS says how
     much of it shows (ZylShell.panelMode) and tells it (panel.mode) */
  function postToSystemUi(type, data) {
    try {
      systemFrame.contentWindow.postMessage(JSON.stringify({ type: type, data: data }), _ipcOrigin);
    } catch (e) { /* not loaded yet */ }
  }

  function setPanel(mode) {
    if (!systemFrame) return;
    state.panel = mode;
    systemFrame.className = mode === 'bar' ? '' : 'panel-' + mode;
    postToSystemUi('panel.mode', { mode: mode });
  }

  function loadSystemUi() {
    var id = ZylShell.systemUi();
    if (!systemFrame || !ZylAppRegistry.getApp(id)) return;
    systemFrame.onload = function () {
      setTimeout(function () {
        postToSystemUi('system.setLocale', { locale: ZylEmuI18n.getLocale() });
        postToSystemUi('system.setTimeZone', { timeZone: _currentTimeZone });
        setPanel('bar');
      }, 200);
    };
    systemFrame.src = ZylAppRegistry.getPath(id) + '?v=' + Date.now();
  }

  /* Recents: the system UI shows them, with what is running */
  function recentsList() {
    return state.runningApps.map(function (a) {
      return { appId: a.id, name: a.name, closable: ZylShell.mayClose(a.id) };
    });
  }

  function showRecents() {
    if (!ZylShell.navigable()) return;
    setPanel('full');
    postToSystemUi('recents.open', { apps: recentsList() });
    syslog('Recents', 'sys');
  }

  function toggleRecents() {
    if (state.panel === 'full') setPanel('bar');
    else showRecents();
  }

  function switchApp(direction) {
    if (!ZylShell.navigable()) return;
    var apps = state.runningApps;
    if (apps.length < 2) return;
    var idx = apps.findIndex(function (a) { return a.id === state.currentApp; });
    if (idx < 0) return;
    var next = direction > 0
      ? (idx + 1) % apps.length
      : (idx - 1 + apps.length) % apps.length;
    open(apps[next].id);
    syslog('Switch → ' + apps[next].name, 'sys');
  }

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
      name.onclick = function () { open(app.id); };
      var closeBtn = document.createElement('button');
      closeBtn.className = 'app-close-btn';
      closeBtn.textContent = '×';
      closeBtn.onclick = function (e) { e.stopPropagation(); closeApp(app.id); };
      item.appendChild(name);
      if (ZylShell.mayClose(app.id)) item.appendChild(closeBtn);
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

  /* ═══ 제어 패널 ═══ */
  document.getElementById('btn-power').addEventListener('click', powerToggle);
  document.getElementById('btn-volup').addEventListener('click', function () { adjustVolume(5); });
  document.getElementById('btn-voldown').addEventListener('click', function () { adjustVolume(-5); });

  /* 측면 물리 버튼 (디바이스 프레임 옆) */
  function powerToggle() {
    state.screenOn = !state.screenOn;
    frameEl.classList.toggle('screen-off', !state.screenOn);
    syslog('Screen ' + (state.screenOn ? 'ON' : 'OFF'), 'sys');
    /* Screen off locks (the lock screen goes up behind the dark screen);
       screen on shows what holds the device — the OS decides both */
    if (!state.screenOn) {
      setPanel('bar');
      show(ZylShell.lock());
    } else {
      show(ZylShell.screenOn());
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
    /* Clean up persistent timers before reboot to prevent accumulation */
    if (_volumeOsdTimer) { clearTimeout(_volumeOsdTimer); _volumeOsdTimer = null; }
    state.runningApps = [];
    state.currentApp = null;
    updateRunningApps();
    bootDevice(device);
  });

  /* ═══════════════════════════════════════════════════════
     Service events: an OS service's signal to the frames the OS allows
     (ZylPermissions.mayReceive — the device bridge's rule), in the device
     bridge's form { type: 'service.event', service, event, data }
     ═══════════════════════════════════════════════════════ */
  function deliverEvent(service, event, data) {
    var msg = JSON.stringify({ type: 'service.event', service: service, event: event, data: data });
    var ui = ZylShell.systemUi();
    if (systemFrame && ZylPermissions.mayReceive(ui, service, event, data)) {
      try { systemFrame.contentWindow.postMessage(msg, _ipcOrigin); } catch (e) { /* not loaded */ }
    }
    if (_displayedApp && ZylPermissions.mayReceive(_displayedApp, service, event, data)) {
      try { appFrame.contentWindow.postMessage(msg, frameOrigin()); } catch (e) { /* detached */ }
    }
  }

  window.addEventListener('zyl:notification-posted', function (e) {
    var n = e.detail;
    if (n) deliverEvent('notification', 'NotificationPosted', [n.id, n.appId, n.title, n.body, n.priority]);
  });
  window.addEventListener('zyl:notification-dismissed', function (e) {
    deliverEvent('notification', 'NotificationDismissed', e.detail);
  });

  /* ── postMessage target origin ──
   * Use window.location.origin so responses are only accepted by same-origin
   * content. Avoids broadcasting to any cross-origin iframe or page that may
   * be loaded in the same WebView. Falls back to '*' only when origin is
   * the opaque 'null' string (e.g. file:// context without a real origin). */
  var _ipcOrigin = (window.location.origin && window.location.origin !== 'null')
    ? window.location.origin
    : '*';

  /* The displayed app's origin: a store app is its own (zyl-app://<id>), the
     image's apps share this page's */
  function frameOrigin() {
    var o = _displayedApp ? ZylAppRegistry.getOrigin(_displayedApp) : '';
    return o || _ipcOrigin;
  }

  /* Where a reply to a frame may go: the image's own pages share this origin */
  function originFor(source) {
    return (source === window || source === systemFrame.contentWindow) ? _ipcOrigin : frameOrigin();
  }

  /* Who sent it, as the OS knows: the system UI's frame, or the app in front */
  function senderOf(source) {
    return source === systemFrame.contentWindow ? ZylShell.systemUi() : (state.currentApp || '');
  }

  /* The OS decided who handles an intent; the container launches and delivers */
  function runIntent(d) {
    if (!d) return;
    if (d.launch) open(d.launch, d.intent);
    else if (d.choose) broadcastToCurrentApp('intent.choose', d.choose);
    else broadcastToCurrentApp('intent.none', {});
  }

  function broadcastToCurrentApp(type, data) {
    try {
      appFrame.contentWindow.postMessage(
        JSON.stringify({ type: type, data: data }),
        frameOrigin()
      );
    } catch(e) { /* iframe may be cross-origin or detached */ }
  }

  /* ═══ System Services IPC ═══ */
  function handleServiceRequest(msg, source, callerAppId) {
    if (!msg.service || !msg.method) return;

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
          source.postMessage(camResponse, originFor(source));
        } else if (appFrame && appFrame.contentWindow) {
          appFrame.contentWindow.postMessage(camResponse, frameOrigin());
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
          source.postMessage(response, originFor(source));
        } else if (appFrame && appFrame.contentWindow) {
          appFrame.contentWindow.postMessage(response, frameOrigin());
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
  /* The device's configured time zone (Settings > Date & time); 'auto' follows
     the system zone. Broadcast to every app on load, like the locale. */
  var _currentTimeZone = 'auto';

  function applySettingSideEffect(category, key, value) {
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

    if (category === 'sound') syslog('Sound: ' + key + ' \u2192 ' + value, 'sys');


    /* ── Locale change → update compositor i18n ── */
    if (category === 'language' && key === 'locale') {
      ZylEmuI18n.setLocale(String(value));
      postToSystemUi('system.setLocale', { locale: String(value) });
      syslog('Locale: ' + value, 'sys');
    }

    /* ── Time zone → current app now, every other app when it loads ── */
    if (category === 'datetime' && key === 'timezone') {
      _currentTimeZone = String(value || 'auto');
      broadcastToCurrentApp('system.setTimeZone', { timeZone: _currentTimeZone });
      postToSystemUi('system.setTimeZone', { timeZone: _currentTimeZone });
      syslog('Time zone: ' + _currentTimeZone, 'sys');
    }

    /* ── 앱 권한 변경 → 권한 시스템 업데이트 ── */
    if (category === 'app_permissions') {
      syslog('App permissions updated: ' + key, 'sys');
      if (typeof ZylPermissions !== 'undefined' && ZylPermissions.setAppOverride) {
        var revoked = String(value || '').split(',').filter(Boolean);
        ZylPermissions.setAppOverride(key, revoked);
      }
    }
  }

  /* ═══ iframe 메시지 ═══ */
  window.addEventListener('message', function (e) {
    /* S7: Origin verification — only our two frames (the app, the system UI)
       or the window itself. The OS names the sender, never the message. */
    if (e.source !== appFrame.contentWindow && e.source !== systemFrame.contentWindow &&
        e.source !== window) return;
    var from = senderOf(e.source);
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || !msg.type) return;
      switch (msg.type) {
        case 'lifecycle.state':
          if (e.source !== systemFrame.contentWindow) _appStates.onState(msg.state);
          break;
        case 'app.launch':
          open(msg.appId);
          break;
        case 'app.close':
          if (from && from !== ZylShell.systemUi() && ZylShell.mayClose(from)) closeApp(from);
          break;
        case 'intent.start':
          runIntent(ZylIntent.start(from, msg.intent));
          break;
        case 'intent.chosen':
          runIntent(ZylIntent.choose(from, msg.token, msg.appId));
          break;
        /* Only the lock screen unlocks, only setup finishes setup */
        case 'unlock':
          var opened = ZylShell.unlock(from, msg.appId);
          if (opened) syslog('Device unlocked', 'sys');
          show(opened);
          break;
        case 'setup.done':
          show(ZylShell.setupDone(from));
          break;
        /* The system UI asks for its surface; the OS says how much */
        case 'panel.request':
          var granted = ZylShell.panelMode(from, msg.mode);
          if (granted) setPanel(granted);
          break;
        /* Closing an app from recents — the system UI's, what the OS lets close */
        case 'recents.close':
          if (from === ZylShell.systemUi() && ZylShell.mayClose(msg.appId)) {
            closeApp(msg.appId);
            postToSystemUi('recents.open', { apps: recentsList() });
          }
          break;
        case 'service.request':
          handleServiceRequest(msg, e.source, from);
          break;
        case 'system.setLocale':
          if (msg.locale) {
            ZylEmuI18n.setLocale(String(msg.locale));
            syslog('Locale: ' + msg.locale, 'sys');
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

  /* ═══ 키보드 ═══ */
  document.addEventListener('keydown', function (e) {
    if (!state.booted || document.activeElement !== document.body) return;
    if (e.key === 'h' && !e.ctrlKey && !e.metaKey) goHome();
    else if (e.key === 'Escape') goBack();
    else if (e.key === 'r' && !e.ctrlKey) toggleRecents();
    else if (e.key === 'l' && !e.ctrlKey) {
      show(ZylShell.lock());
      syslog('Locked', 'sys');
    }
    else if (e.key === 'p' && !e.ctrlKey) powerToggle();
    else if (e.key === 'ArrowUp') { e.preventDefault(); adjustVolume(5); }
    else if (e.key === 'ArrowDown') { e.preventDefault(); adjustVolume(-5); }
  });


  /* ═══════════════════════════════════════════════════════
     Device Selection
     디바이스 목록 UI 는 config-ui.js 가 device-profiles.json 에서 렌더한다.
     선택 결과는 initFromConfig() 로 들어온다.
     ═══════════════════════════════════════════════════════ */

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
      ZylServices.applyDisplayProfile(profile);
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

      /* Load persisted time zone */
      var tzLoad = ZylServices.handleRequest('settings', 'get', { category: 'datetime' });
      if (tzLoad && typeof tzLoad.then === 'function') {
        tzLoad.then(function (dt) {
          if (dt && dt.timezone) { _currentTimeZone = String(dt.timezone); updateClock(); }
        });
      }

      /* Load persisted locale */
      var localeLoad = ZylServices.handleRequest('settings', 'get', { category: 'language' });
      if (localeLoad && typeof localeLoad.then === 'function') {
        localeLoad.then(function (lang) {
          if (lang && lang.locale) ZylEmuI18n.setLocale(String(lang.locale));
        });
      }


      /* Setup first until it is done, then the lock screen (the OS's call);
         settings unreadable → locked, never open */
      var setupCheck = ZylServices.handleRequest('settings', 'get', { category: 'system' });
      Promise.resolve(setupCheck).then(function (sys) {
        show(ZylShell.boot(!!(sys && sys.oobe_completed)));
        loadSystemUi();
      }, function () {
        show(ZylShell.boot(true));
        loadSystemUi();
      });

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
      inputModel: String(config.input_model || config.inputModel || 'touch'),
      hasNotch: config.has_notch !== false,
      deviceId: (bootInfo && bootInfo.device_id) || '',
    };

    /* 부팅 정보 로그 (앱은 Rust에서 ui/apps/로 복사됨) */
    if (bootInfo && bootInfo.os_image_mount) {
      syslog('OS image: ' + esc(bootInfo.os_image_mount), 'sys');
    }

    /* 에뮬레이터 화면 표시 */
    if (emulatorScreen) emulatorScreen.classList.remove('hidden');

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
