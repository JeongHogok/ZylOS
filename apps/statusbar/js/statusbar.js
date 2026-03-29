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
    flashlight: false
  };

  /* ─── Clock (shared ZylClock) ─── */
  if (typeof ZylClock !== 'undefined') {
    ZylClock.create(sbTime, null, { showDate: false });
  }

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
    flashlight: document.getElementById('qs-flashlight')
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
      wifi: { category: 'wifi', key: 'enabled' },
      bt: { category: 'bluetooth', key: 'enabled' },
      silent: { category: 'sound', key: 'silent' },
      rotate: { category: 'display', key: 'autoRotate' },
      flashlight: { category: 'display', key: 'flashlight' }
    };
    var mapping = categoryMap[key];
    if (!mapping) return;
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
    item.innerHTML =
      '<div class="notif-icon">' + escapeHtml(data.icon || '\uD83D\uDD14') + '</div>' +
      '<div class="notif-content">' +
        '<div class="notif-title">' + escapeHtml(data.title || '') + '</div>' +
        '<div class="notif-body">' + escapeHtml(data.body || '') + '</div>' +
      '</div>' +
      '<div class="notif-time">' + formatTimeAgo(data.timestamp || Date.now()) + '</div>';

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

  /* Expose for external calls */
  window.addNotification = addNotification;

  function escapeHtml(str) {
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(str || ''));
    return div.innerHTML;
  }

  function formatTimeAgo(ts) {
    var diff = Math.floor((Date.now() - ts) / 1000);
    if (diff < 60) return 'now';
    if (diff < 3600) return Math.floor(diff / 60) + 'm';
    if (diff < 86400) return Math.floor(diff / 3600) + 'h';
    return Math.floor(diff / 86400) + 'd';
  }

  /* ════════════════════════════════════════════
   *  IPC — postMessage Communication
   * ════════════════════════════════════════════ */
  function sendServiceRequest(service, method, params) {
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({
        type: 'service.request',
        service: service,
        method: method,
        params: params || {}
      }), '*');
    }
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
    /* WiFi state */
    if (data.category === 'wifi' && data.key === 'enabled') {
      qsState.wifi = !!data.value;
      if (qsBtns.wifi) qsBtns.wifi.classList.toggle('active', qsState.wifi);
      var wifiIcon = document.getElementById('sb-wifi');
      if (wifiIcon) wifiIcon.style.opacity = qsState.wifi ? '0.85' : '0.15';
    }
    /* Bluetooth state */
    if (data.category === 'bluetooth' && data.key === 'enabled') {
      qsState.bt = !!data.value;
      if (qsBtns.bt) qsBtns.bt.classList.toggle('active', qsState.bt);
      var btIcon = document.getElementById('sb-bt');
      if (btIcon) btIcon.style.opacity = qsState.bt ? '0.85' : '0.15';
    }
    /* Brightness */
    if (data.category === 'display' && data.key === 'brightness') {
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
  }

  /* ─── Request initial state ─── */
  sendServiceRequest('settings', 'get', { category: 'wifi' });
  sendServiceRequest('settings', 'get', { category: 'bluetooth' });
  sendServiceRequest('settings', 'get', { category: 'display' });

})();
