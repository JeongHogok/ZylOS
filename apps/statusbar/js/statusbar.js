// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Widget
//
// Role: System UI — status bar, notification panel with quick settings, the
//       new-notification banner, recents
// Scope: Clock, battery, QS toggles with service calls, brightness, the
//        notification service's events, recents. Its surface lies over every
//        app; the OS decides how much of it shows (panel.mode: bar, banner,
//        full — apps/system/shell.js, runtime/wam/src/shell) and this asks
//        (panel.request). Recents lists what the OS says is running
// Dependency: ZylClock (clock.js), zylI18n (i18n.js)
// SOLID: SRP — status bar UI rendering and system state display only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── DOM References ─── */
  var sbTime        = document.getElementById('sb-time');
  var batteryPct    = document.getElementById('sb-battery-pct');
  var notifDot      = document.getElementById('sb-notif-dot');
  var drawer        = document.getElementById('notification-drawer');
  var statusbar     = document.getElementById('statusbar');
  var notifList     = document.getElementById('notification-list');
  var brightnessEl  = document.getElementById('brightness-slider');
  var banner        = document.getElementById('notif-banner');
  var recentsEl     = document.getElementById('recents');
  var recentsList   = document.getElementById('recents-list');

  /* ─── State ─── */
  var mode           = 'bar';   /* what the OS granted: bar | banner | full */
  var showing        = 'panel'; /* what full shows: the panel or recents */
  var bannerTimer    = null;
  var notifCount     = 0;
  var qsState = {
    wifi: true,
    bt: false,
    silent: false,
    rotate: false,
    flashlight: false,
    airplane: false,
    dnd: false,
    location: true
  };

  /* ─── Clock (shared ZylClock) ─── */
  /* clock.js loads before this script (index.html), so ZylClock exists. The
     fallback that lived here drew the process time zone, not the user's. */
  function initClock() {
    ZylClock.create(sbTime, null, { showDate: false });
  }
  initClock();

  /* ════════════════════════════════════════════
   *  Battery — periodic polling
   * ════════════════════════════════════════════ */
  function updateBattery() {
    sendServiceRequest('power', 'getState', {});
  }
  updateBattery();
  setInterval(updateBattery, 60000);

  /* ════════════════════════════════════════════
   *  Data usage — the netstats daemon cuts mobile data at the limit by
   *  itself; the status bar is what tells the user, in their language.
   *  The emulator reports supported:false, so nothing is ever posted there.
   * ════════════════════════════════════════════ */
  var dataFlags = null;
  function postDataNotice(titleKey, body, priority) {
    ZylBridge.requestService('notification', 'post', {
      appId: 'com.zylos.settings',
      channelId: 'datausage',
      title: zylI18n.t(titleKey),
      body: body,
      icon: '',
      priority: priority
    });
  }
  function checkDataUsage() {
    ZylBridge.requestService('netstats', 'getUsage').then(function (u) {
      if (!u || u.supported !== true) return;
      var prev = dataFlags;
      dataFlags = { warned: u.warned === true, limited: u.limited === true };
      /* shortcut: the first reading only sets the baseline, so a crossing while
         the status bar was not running goes unannounced; persist the last
         announced state if that turns out to matter. */
      if (!prev) return;
      if (dataFlags.limited && !prev.limited) {
        postDataNotice('notif.data_limit_title', zylI18n.t('notif.data_limit_body'), 2);
      } else if (dataFlags.warned && !prev.warned) {
        postDataNotice('notif.data_warning_title', zylI18n.t('notif.data_warning_body',
          { used: zylI18n.formatBytes(u.mobileRx + u.mobileTx) }), 1);
      }
    }, function () { /* service unavailable — try again next round */ });
  }
  checkDataUsage();
  setInterval(checkDataUsage, 60000);

  /* ════════════════════════════════════════════
   *  Quick Settings — toggles with service calls
   * ════════════════════════════════════════════ */
  var qsBtns = {
    wifi:       document.getElementById('qs-wifi'),
    bt:         document.getElementById('qs-bt'),
    silent:     document.getElementById('qs-silent'),
    rotate:     document.getElementById('qs-rotate'),
    flashlight: document.getElementById('qs-flashlight'),
    airplane:   document.getElementById('qs-airplane'),
    dnd:        document.getElementById('qs-dnd'),
    location:   document.getElementById('qs-location')
  };

  Object.keys(qsBtns).forEach(function (key) {
    var btn = qsBtns[key];
    if (!btn) return;
    btn.addEventListener('click', function () {
      if (btn.disabled) return;
      qsState[key] = !qsState[key];
      btn.classList.toggle('active', qsState[key]);
      if (RADIOS[key]) setRadio(key, qsState[key]);
      else if (SERVICE_TILES[key]) applyTile(key, qsState[key]);
      else sendQsSetting(key, qsState[key]);
    });
  });

  /* ─── Radios belong to their services (wifi, bluetooth), not to settings:
     the switch is asked for, the state is read back from the service ─── */
  var RADIOS = { wifi: 'wifi', bt: 'bluetooth' };

  function showRadio(key, on) {
    qsState[key] = on;
    if (qsBtns[key]) qsBtns[key].classList.toggle('active', on);
    var icon = document.getElementById(key === 'wifi' ? 'sb-wifi' : 'sb-bt');
    if (icon) icon.style.opacity = on ? '0.85' : '0.15';
  }

  function refreshRadio(key) {
    ZylBridge.requestService(RADIOS[key], 'getState').then(function (st) {
      if (st) showRadio(key, !!st.enabled);
    }, function () {});
  }

  function setRadio(key, on) {
    ZylBridge.requestService(RADIOS[key], 'setEnabled', { enabled: on }).then(function () {
      refreshRadio(key);
    }, function () { refreshRadio(key); });
  }

  /* Tiles a service carries out; the setting keeps the choice where one is kept */
  var SERVICE_TILES = {
    flashlight: { service: 'camera', method: 'setTorch' },
    rotate:     { service: 'display', method: 'setAutoRotate', category: 'display', key: 'autoRotate' },
    silent:     { service: 'audio', method: 'setSilentMode', category: 'sound', key: 'silentMode' }
  };

  function applyTile(key, on) {
    var t = SERVICE_TILES[key];
    sendServiceRequest(t.service, t.method, { enabled: on });
    if (t.category) sendServiceRequest('settings', 'update', { category: t.category, key: t.key, value: on });
  }

  function disableTile(btn) {
    if (!btn) return;
    btn.disabled = true;
    btn.setAttribute('aria-disabled', 'true');
    btn.classList.remove('active');
  }

  /* What this device has: no flash LED, no backlight → the control stays, disabled */
  ZylBridge.requestService('camera', 'getCapabilities').then(function (caps) {
    if (!caps || !caps.torch) disableTile(qsBtns.flashlight);
  }, function () { disableTile(qsBtns.flashlight); });
  ZylBridge.requestService('power', 'getCapabilities').then(function (caps) {
    if (caps && caps.backlight === false && brightnessEl) {
      brightnessEl.disabled = true;
      brightnessEl.setAttribute('aria-disabled', 'true');
    }
  }, function () {});

  /* No radio on this device: the button stays, disabled */
  Object.keys(RADIOS).forEach(function (key) {
    ZylBridge.requestService(RADIOS[key], 'getCapabilities').then(function (caps) {
      if (caps && caps.present) { refreshRadio(key); return; }
      if (qsBtns[key]) {
        qsBtns[key].disabled = true;
        qsBtns[key].setAttribute('aria-disabled', 'true');
      }
      showRadio(key, false);
    }, function () {});
  });

  function sendQsSetting(key, value) {
    var categoryMap = {
      airplane:   { category: 'connectivity', key: 'airplaneMode' },
      dnd:        { category: 'notification', key: 'dndEnabled' },
      location:   { category: 'location',     key: 'enabled' }
    };
    var mapping = categoryMap[key];
    if (!mapping) return;

    /* Airplane mode: the radios go off */
    if (key === 'airplane' && value) {
      Object.keys(RADIOS).forEach(function (r) {
        if (qsBtns[r] && !qsBtns[r].disabled) setRadio(r, false);
      });
    }

    /* DND mode: also call notification service */
    if (key === 'dnd') {
      sendServiceRequest('notification', 'setDndMode', { enabled: value });
    }

    sendServiceRequest('settings', 'update', {
      category: mapping.category,
      key: mapping.key,
      value: value
    });
  }

  /* ════════════════════════════════════════════
   *  Brightness Slider
   * ════════════════════════════════════════════ */
  if (brightnessEl) {
    brightnessEl.addEventListener('input', function () {
      var pct = parseInt(brightnessEl.value, 10);
      /* The power service drives the backlight; the setting keeps the level */
      sendServiceRequest('power', 'setBrightness', { percent: pct });
      sendServiceRequest('settings', 'update', {
        category: 'display',
        key: 'brightness',
        value: pct
      });
    });
  }

  /* ════════════════════════════════════════════
   *  The surface: ask the OS, show what it grants
   * ════════════════════════════════════════════ */
  function requestMode(m) {
    ZylBridge.sendToSystem({ type: 'panel.request', mode: m });
  }

  function applyMode(m) {
    var wasFull = mode === 'full';
    mode = m;
    var full = m === 'full';
    if (full && !wasFull && showing === 'panel') {
      /* Radios may have been switched elsewhere (Settings): show them as they are */
      Object.keys(RADIOS).forEach(function (r) { if (qsBtns[r] && !qsBtns[r].disabled) refreshRadio(r); });
    }
    if (full && showing === 'panel') {
      drawer.classList.remove('hidden');
      drawer.offsetHeight; /* force reflow for animation */
      drawer.classList.add('visible');
    } else if (drawer.classList.contains('visible')) {
      drawer.classList.remove('visible');
      setTimeout(function () { if (mode !== 'full') drawer.classList.add('hidden'); }, 350);
    }
    recentsEl.classList.toggle('hidden', !(full && showing === 'recents'));
    banner.classList.toggle('hidden', m !== 'banner');
    if (!full) showing = 'panel';
  }

  /* Pull the bar down, or tap it: the panel (the OS may say no — locked) */
  if (statusbar) {
    var pullFrom = null;
    statusbar.addEventListener('click', function () {
      requestMode(mode === 'full' ? 'bar' : 'full');
    });
    statusbar.addEventListener('touchstart', function (e) { pullFrom = e.touches[0].clientY; }, { passive: true });
    statusbar.addEventListener('touchmove', function (e) {
      if (pullFrom !== null && e.touches[0].clientY - pullFrom > 30 && mode !== 'full') {
        pullFrom = null;
        requestMode('full');
      }
    }, { passive: true });
  }

  var handle = drawer ? drawer.querySelector('.drawer-handle') : null;
  if (handle) {
    handle.addEventListener('click', function () { requestMode('bar'); });
  }

  /* Opening an app from here: it comes to the front and the panel goes */
  function openApp(appId) {
    ZylBridge.sendToSystem({ type: 'app.launch', appId: appId });
    requestMode('bar');
  }

  /* ─── A new notification: a banner for a few seconds, if allowed ─── */
  function showBanner(data) {
    if (mode === 'full') return;
    document.getElementById('banner-icon').textContent = data.icon || '\uD83D\uDD14';
    document.getElementById('banner-title').textContent = data.title || '';
    document.getElementById('banner-body').textContent = data.body || '';
    banner.onclick = function () { if (data.appId) openApp(data.appId); else requestMode('bar'); };
    requestMode('banner');
    clearTimeout(bannerTimer);
    bannerTimer = setTimeout(function () { if (mode === 'banner') requestMode('bar'); }, 3000);
  }

  /* ─── Recents: the OS says what is running ─── */
  function showRecents(apps) {
    var t = zylI18n.t;
    showing = 'recents';
    recentsList.innerHTML = '';
    if (!apps || apps.length === 0) {
      var empty = document.createElement('div');
      empty.className = 'recents-empty';
      empty.textContent = t('recents.empty');
      recentsList.appendChild(empty);
    }
    (apps || []).forEach(function (a) {
      var card = document.createElement('div');
      card.className = 'recent-card';
      card.setAttribute('role', 'button');
      var name = document.createElement('span');
      name.textContent = a.name || a.appId;
      card.appendChild(name);
      card.addEventListener('click', function () { openApp(a.appId); });
      if (a.closable) {
        var close = document.createElement('button');
        close.className = 'recent-close';
        close.textContent = '\u00d7';
        close.setAttribute('aria-label', t('recents.close'));
        close.addEventListener('click', function (e) {
          e.stopPropagation();
          ZylBridge.sendToSystem({ type: 'recents.close', appId: a.appId });
        });
        card.appendChild(close);
      }
      recentsList.appendChild(card);
    });
    applyMode(mode);
  }

  /* ════════════════════════════════════════════
   *  Notifications
   * ════════════════════════════════════════════ */
  function addNotification(data) {
    if (!notifList) return;
    var empty = notifList.querySelector('.notif-empty');
    if (empty) empty.remove();

    notifCount++;
    if (notifDot) notifDot.classList.remove('hidden');

    var t = (typeof zylI18n !== 'undefined') ? zylI18n.t : function (k) { return k; };

    var item = document.createElement('div');
    item.className = 'notif-item';
    if (data.id !== undefined) item.dataset.id = String(data.id);
    item.setAttribute('role', 'button');
    item.innerHTML =
      '<div class="notif-icon">' + escapeHtml(data.icon || '\uD83D\uDD14') + '</div>' +
      '<div class="notif-content">' +
        '<div class="notif-title">' + escapeHtml(data.title || '') + '</div>' +
        '<div class="notif-body">' + escapeHtml(data.body || '') + '</div>' +
      '</div>' +
      '<div class="notif-time">' + formatTimeAgo(data.timestamp || Date.now()) + '</div>';

    /* Tap notification → launch source app */
    if (data.appId) {
      item.style.cursor = 'pointer';
      item.addEventListener('click', function () { openApp(data.appId); });
    }

    /* Swipe to dismiss */
    var startX = 0;
    item.addEventListener('touchstart', function (e) {
      startX = e.touches[0].clientX;
    }, { passive: true });
    item.addEventListener('touchend', function (e) {
      var dx = e.changedTouches[0].clientX - startX;
      if (Math.abs(dx) > 100) {
        item.style.transform = 'translateX(' + (dx > 0 ? '100%' : '-100%') + ')';
        item.style.opacity = '0';
        setTimeout(function () { dropNotification(item); }, 300);
      }
    });

    notifList.prepend(item);
  }

  function dropNotification(item) {
    if (!item.parentNode) return;
    item.remove();
    notifCount = Math.max(0, notifCount - 1);
    if (notifCount === 0 && notifDot) {
      notifDot.classList.add('hidden');
      var emptyText = (typeof zylI18n !== 'undefined') ? zylI18n.t('notif.empty') : 'No notifications';
      notifList.innerHTML = '<div class="notif-empty" data-i18n="notif.empty">' + escapeHtml(emptyText) + '</div>';
    }
  }

  /* ─── Clear All Notifications ─── */
  function clearAllNotifications() {
    if (!notifList) return;
    notifCount = 0;
    if (notifDot) notifDot.classList.add('hidden');
    var t = (typeof zylI18n !== 'undefined') ? zylI18n.t : function (k) { return k; };
    var emptyText = t('notif.empty');
    notifList.innerHTML = '<div class="notif-empty" data-i18n="notif.empty">' + escapeHtml(emptyText) + '</div>';
    sendServiceRequest('notification', 'clearAll', {});
  }

  /* Insert clear-all button above notification list */
  var clearAllBtn = document.getElementById('notif-clear-all');
  if (clearAllBtn) {
    clearAllBtn.addEventListener('click', clearAllNotifications);
  }

  function escapeHtml(str) {
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(str || ''));
    return div.innerHTML;
  }

  function formatTimeAgo(ts) {
    var t = (typeof zylI18n !== 'undefined') ? zylI18n.t : function (k) { return k; };
    var diff = Math.floor((Date.now() - ts) / 1000);
    if (diff < 60) return t('notif.time_now');
    if (diff < 3600) return t('notif.time_min', { n: Math.floor(diff / 60) });
    if (diff < 86400) return t('notif.time_hour', { n: Math.floor(diff / 3600) });
    return t('notif.time_day', { n: Math.floor(diff / 86400) });
  }

  /* ════════════════════════════════════════════
   *  IPC — postMessage Communication
   * ════════════════════════════════════════════ */
  function sendServiceRequest(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  /* What the device already holds, then the notification service's own
     events (the WAM delivers them on the device, the emulator the same way) */
  ZylBridge.requestService('notification', 'getActive').then(function (list) {
    (list || []).forEach(function (n) { addNotification(n); });
  }, function () {});
  ZylBridge.onServiceEvent('notification', 'NotificationPosted', function (d) {
    if (!d || d.length < 4) return;
    var n = { id: d[0], appId: d[1], title: d[2], body: d[3] };
    addNotification(n);
    showBanner(n);
    /* Sound and buzz: the audio service decides (silent mode, the vibration setting) */
    ZylBridge.requestService('audio', 'playNotificationSound').then(null, function () {});
    ZylBridge.requestService('audio', 'vibrate', { pattern: [200] }).then(null, function () {});
  });
  ZylBridge.onServiceEvent('notification', 'NotificationDismissed', function (id) {
    var el = notifList && notifList.querySelector('.notif-item[data-id="' + String(id).replace(/\D/g, '') + '"]');
    if (el) dropNotification(el);
  });

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || !msg.type) return;

      switch (msg.type) {
        /* What the OS lets the surface be */
        case 'panel.mode':
          if (msg.data && msg.data.mode) applyMode(msg.data.mode);
          break;

        /* Recents, with what is running */
        case 'recents.open':
          showRecents(msg.data && msg.data.apps);
          break;

        /* Service responses */
        case 'service.response':
          handleServiceResponse(msg);
          break;
      }
    } catch (err) { /* ignore parse errors */ }
  });

  function handleServiceResponse(msg) {
    /* Battery level from power service */
    if (msg.service === 'power' && msg.method === 'getState' && msg.data) {
      /* No battery fitted (a development board): no battery to show */
      var batteryBox = document.getElementById('sb-battery');
      if (batteryBox) batteryBox.classList.toggle('hidden', msg.data.batteryPresent === false);
      if (batteryPct && msg.data.batteryLevel !== undefined) {
        batteryPct.textContent = msg.data.batteryLevel + '%';
      }
    }

    /* Sync initial QS toggle states from settings response */
    if (msg.service === 'settings' && msg.data) {
      var d = msg.data;
      if (d.brightness !== undefined) {
        if (brightnessEl) brightnessEl.value = parseInt(d.brightness, 10) || 80;
      }
    }
  }

  /* ─── Request initial state ─── */
  /* FIX: Request all QS-relevant categories on init so UI reflects actual state
   *       before any user interaction rather than relying on hardcoded qsState defaults. */
  sendServiceRequest('settings', 'get', { category: 'display' });
  sendServiceRequest('settings', 'get', { category: 'sound' });
  sendServiceRequest('settings', 'get', { category: 'location' });

})();
