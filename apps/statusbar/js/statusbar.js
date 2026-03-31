// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Widget
//
// Role: Status bar widget — time, battery, network status, quick settings, notifications
// Scope: Clock update, battery polling, QS toggle with service calls, notification IPC, brightness slider
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

  /* ─── State ─── */
  var isDrawerOpen   = false;
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
  /* FIX: Guard against ZylClock not yet available (race condition on init) */
  function initClock() {
    if (typeof ZylClock !== 'undefined') {
      ZylClock.create(sbTime, null, { showDate: false });
    } else {
      /* Fallback: simple time display until ZylClock loads */
      function fallbackClock() {
        if (sbTime) {
          var now = new Date();
          var h = now.getHours();
          var m = now.getMinutes();
          sbTime.textContent = (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m;
        }
      }
      fallbackClock();
      setInterval(fallbackClock, 10000);
    }
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
      qsState[key] = !qsState[key];
      btn.classList.toggle('active', qsState[key]);
      sendQsSetting(key, qsState[key]);
    });
  });

  function sendQsSetting(key, value) {
    var categoryMap = {
      wifi:       { category: 'wifi',         key: 'enabled' },
      bt:         { category: 'bluetooth',    key: 'enabled' },
      silent:     { category: 'sound',        key: 'silent' },
      rotate:     { category: 'display',      key: 'autoRotate' },
      flashlight: { category: 'display',      key: 'flashlight' },
      airplane:   { category: 'connectivity', key: 'airplaneMode' },
      dnd:        { category: 'notification', key: 'dndEnabled' },
      location:   { category: 'location',     key: 'enabled' }
    };
    var mapping = categoryMap[key];
    if (!mapping) return;

    /* Airplane mode: also disable wifi and bluetooth */
    if (key === 'airplane') {
      if (value) {
        qsState.wifi = false;
        qsState.bt = false;
        if (qsBtns.wifi) qsBtns.wifi.classList.remove('active');
        if (qsBtns.bt) qsBtns.bt.classList.remove('active');
        sendServiceRequest('settings', 'update', { category: 'wifi', key: 'enabled', value: false });
        sendServiceRequest('settings', 'update', { category: 'bluetooth', key: 'enabled', value: false });
      }
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
      sendServiceRequest('settings', 'update', {
        category: 'display',
        key: 'brightness',
        value: pct
      });
    });
  }

  /* ════════════════════════════════════════════
   *  Notification Drawer
   * ════════════════════════════════════════════ */
  if (statusbar) {
    statusbar.addEventListener('click', function () {
      toggleDrawer();
    });
  }

  function toggleDrawer() {
    isDrawerOpen = !isDrawerOpen;
    if (isDrawerOpen) {
      drawer.classList.remove('hidden');
      drawer.offsetHeight; /* force reflow for animation */
      drawer.classList.add('visible');
    } else {
      drawer.classList.remove('visible');
      setTimeout(function () {
        drawer.classList.add('hidden');
      }, 350);
    }
  }

  var handle = drawer ? drawer.querySelector('.drawer-handle') : null;
  if (handle) {
    handle.addEventListener('click', function () {
      if (isDrawerOpen) toggleDrawer();
    });
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
      item.addEventListener('click', function () {
        ZylBridge.sendToSystem({
          type: 'app.launch', appId: data.appId
        });
        toggleDrawer();
      });
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
        notifCount = Math.max(0, notifCount - 1);
        setTimeout(function () {
          item.remove();
          if (notifCount === 0 && notifDot) {
            notifDot.classList.add('hidden');
            var emptyText = (typeof zylI18n !== 'undefined') ? zylI18n.t('notif.empty') : 'No notifications';
            notifList.innerHTML = '<div class="notif-empty" data-i18n="notif.empty">' + escapeHtml(emptyText) + '</div>';
          }
        }, 300);
      }
    });

    notifList.prepend(item);
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

  /* Expose for external calls */
  window.addNotification = addNotification;

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

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || !msg.type) return;

      switch (msg.type) {
        /* Notification pushed from emulator/system */
        case 'notification.push':
          if (msg.data) addNotification(msg.data);
          break;

        /* Settings changed (WiFi/BT/brightness etc.) */
        case 'settings.changed':
          if (msg.data) handleSettingsChange(msg.data);
          break;

        /* Battery update from emulator */
        case 'battery.update':
          if (msg.data && msg.data.level !== undefined) {
            if (batteryPct) batteryPct.textContent = msg.data.level + '%';
          }
          break;

        /* Service responses */
        case 'service.response':
          handleServiceResponse(msg);
          break;
      }
    } catch (err) { /* ignore parse errors */ }
  });

  function handleSettingsChange(data) {
    if (!data) return;
    /* FIX: Normalize category — may arrive as data.category or data.params.category
     *       depending on which path triggered settings.changed. Coerce to string. */
    var category = data.category || (data.params && data.params.category) || '';
    /* WiFi state */
    if (category === 'wifi' && data.key === 'enabled') {
      qsState.wifi = !!data.value;
      if (qsBtns.wifi) qsBtns.wifi.classList.toggle('active', qsState.wifi);
      var wifiIcon = document.getElementById('sb-wifi');
      if (wifiIcon) wifiIcon.style.opacity = qsState.wifi ? '0.85' : '0.15';
    }
    /* Bluetooth state */
    if (category === 'bluetooth' && data.key === 'enabled') {
      qsState.bt = !!data.value;
      if (qsBtns.bt) qsBtns.bt.classList.toggle('active', qsState.bt);
      var btIcon = document.getElementById('sb-bt');
      if (btIcon) btIcon.style.opacity = qsState.bt ? '0.85' : '0.15';
    }
    /* Brightness */
    if (category === 'display' && data.key === 'brightness') {
      if (brightnessEl) brightnessEl.value = parseInt(data.value, 10) || 80;
    }
  }

  function handleServiceResponse(msg) {
    /* Battery level from power service */
    if (msg.service === 'power' && msg.method === 'getState' && msg.data) {
      if (batteryPct && msg.data.batteryLevel !== undefined) {
        batteryPct.textContent = msg.data.batteryLevel + '%';
      }
    }

    /* Sync initial QS toggle states from settings response */
    if (msg.service === 'settings' && msg.data) {
      var d = msg.data;
      if (d.enabled !== undefined) {
        /* Determine which category based on request context */
        if (d.category === 'wifi' || msg.params && msg.params.category === 'wifi') {
          qsState.wifi = !!d.enabled;
          if (qsBtns.wifi) qsBtns.wifi.classList.toggle('active', qsState.wifi);
          var wi = document.getElementById('sb-wifi');
          if (wi) wi.style.opacity = qsState.wifi ? '0.85' : '0.15';
        }
        if (d.category === 'bluetooth' || msg.params && msg.params.category === 'bluetooth') {
          qsState.bt = !!d.enabled;
          if (qsBtns.bt) qsBtns.bt.classList.toggle('active', qsState.bt);
          var bi = document.getElementById('sb-bt');
          if (bi) bi.style.opacity = qsState.bt ? '0.85' : '0.15';
        }
      }
      if (d.brightness !== undefined) {
        if (brightnessEl) brightnessEl.value = parseInt(d.brightness, 10) || 80;
      }
    }
  }

  /* ─── Request initial state ─── */
  /* FIX: Request all QS-relevant categories on init so UI reflects actual state
   *       before any user interaction rather than relying on hardcoded qsState defaults. */
  sendServiceRequest('settings', 'get', { category: 'wifi' });
  sendServiceRequest('settings', 'get', { category: 'bluetooth' });
  sendServiceRequest('settings', 'get', { category: 'display' });
  sendServiceRequest('settings', 'get', { category: 'sound' });
  sendServiceRequest('settings', 'get', { category: 'location' });

})();
