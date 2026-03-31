// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 전화 앱 — 키패드, 통화기록, 연락처, 즐겨찾기, 통화화면, DTMF
// 수행범위: 키패드 입력, 발신/수신/종료, 통화기록 조회/삭제, 연락처 목록, 즐겨찾기, DTMF 톤
// 의존방향: shared/bridge.js → postMessage IPC (telephony, contacts)
// SOLID: SRP — 전화 관련 UI만 담당, OCP — 통화기록 확장 가능
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ═══════════════════════════════════════════════════════════
     IPC — postMessage service calls
     ═══════════════════════════════════════════════════════════ */

  function requestService(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  var serviceCallbacks = {};

  function onServiceResponse(service, method, cb) {
    serviceCallbacks[service + '.' + method] = cb;
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      if (!e.data) return;
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        if (incallOverlay && !incallOverlay.classList.contains('hidden')) {
          /* During call, back does nothing — must end call explicitly */
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      /* Service responses */
      if (msg.type === 'service.response' && msg.service && msg.method) {
        var key = msg.service + '.' + msg.method;
        if (serviceCallbacks[key]) {
          serviceCallbacks[key](msg.data, msg.error);
        }
        return;
      }

      /* Incoming call notification from telephony service */
      if (msg.type === 'telephony.incomingCall' && msg.data) {
        handleIncomingCall(msg.data);
        return;
      }

      /* Call state change */
      if (msg.type === 'telephony.callStateChanged' && msg.data) {
        handleCallStateChange(msg.data);
        return;
      }

    } catch (err) { /* ignore parse errors */ }
  });

  /* ═══════════════════════════════════════════════════════════
     DOM References
     ═══════════════════════════════════════════════════════════ */

  var tabs = document.querySelectorAll('#tabs .tab');
  var tabContents = document.querySelectorAll('.tab-content');

  /* Keypad */
  var dialedNumberEl = document.getElementById('dialed-number');
  var btnBackspace = document.getElementById('btn-backspace');
  var keypadGrid = document.getElementById('keypad-grid');
  var btnCall = document.getElementById('btn-call');

  /* Recents */
  var recentsList = document.getElementById('recents-list');
  var recentsEmpty = document.getElementById('recents-empty');

  /* Contacts */
  var contactsList = document.getElementById('contacts-list');

  /* Favorites */
  var favoritesList = document.getElementById('favorites-list');
  var favoritesEmpty = document.getElementById('favorites-empty');

  /* In-call */
  var incallOverlay = document.getElementById('incall-overlay');
  var incallStatus = document.getElementById('incall-status');
  var incallName = document.getElementById('incall-name');
  var incallNumber = document.getElementById('incall-number');
  var incallTimer = document.getElementById('incall-timer');
  var btnEndCall = document.getElementById('btn-end-call');
  var btnMute = document.getElementById('btn-mute');
  var btnSpeaker = document.getElementById('btn-speaker');
  var btnKeypadIncall = document.getElementById('btn-keypad-incall');

  /* DTMF */
  var dtmfOverlay = document.getElementById('dtmf-overlay');
  var dtmfGrid = document.getElementById('dtmf-grid');
  var dtmfDigitsEl = document.getElementById('dtmf-digits');

  /* Delete modal */
  var deleteLogModal = document.getElementById('delete-log-modal');
  var btnDeleteCancel = document.getElementById('btn-delete-cancel');
  var btnDeleteConfirm = document.getElementById('btn-delete-confirm');

  /* ═══════════════════════════════════════════════════════════
     State
     ═══════════════════════════════════════════════════════════ */

  var dialedNumber = '';
  var callTimerInterval = null;
  var callStartTime = 0;
  var isMuted = false;
  var isSpeaker = false;
  var currentCallNumber = '';
  var isDtmfVisible = false;
  var dtmfDigits = '';
  var pendingDeleteLogId = null;
  var favoritesSet = {};
  var longPressTimer = null;

  /* ═══════════════════════════════════════════════════════════
     Tab Switching
     ═══════════════════════════════════════════════════════════ */

  function switchTab(tabName) {
    for (var i = 0; i < tabs.length; i++) {
      var t = tabs[i];
      var target = t.getAttribute('data-tab');
      if (target === tabName) {
        t.classList.add('active');
      } else {
        t.classList.remove('active');
      }
    }
    for (var j = 0; j < tabContents.length; j++) {
      var c = tabContents[j];
      if (c.id === 'tab-' + tabName) {
        c.classList.remove('hidden');
        c.classList.add('active');
      } else {
        c.classList.add('hidden');
        c.classList.remove('active');
      }
    }

    if (tabName === 'recents') { loadRecents(); }
    if (tabName === 'contacts') { loadContacts(); }
    if (tabName === 'favorites') { loadFavorites(); }
  }

  for (var i = 0; i < tabs.length; i++) {
    tabs[i].addEventListener('click', function () {
      switchTab(this.getAttribute('data-tab'));
    });
  }

  /* ═══════════════════════════════════════════════════════════
     Keypad
     ═══════════════════════════════════════════════════════════ */

  function updateDisplay() {
    dialedNumberEl.textContent = dialedNumber || '';
    btnBackspace.style.visibility = dialedNumber ? 'visible' : 'hidden';
  }

  keypadGrid.addEventListener('click', function (e) {
    var btn = e.target.closest('.key-btn');
    if (!btn) return;
    var digit = btn.getAttribute('data-digit');
    if (digit) {
      dialedNumber += digit;
      updateDisplay();
      /* Play key click sound via audio service */
      requestService('audio', 'playKeyClick', {});
    }
  });

  btnBackspace.addEventListener('click', function () {
    if (dialedNumber.length > 0) {
      dialedNumber = dialedNumber.slice(0, -1);
      updateDisplay();
    }
  });

  /* Long press backspace to clear all */
  var backspaceTimer = null;
  btnBackspace.addEventListener('touchstart', function () {
    backspaceTimer = setTimeout(function () {
      dialedNumber = '';
      updateDisplay();
    }, 600);
  });
  btnBackspace.addEventListener('touchend', function () {
    clearTimeout(backspaceTimer);
  });

  updateDisplay();

  /* ═══════════════════════════════════════════════════════════
     Call Actions
     ═══════════════════════════════════════════════════════════ */

  btnCall.addEventListener('click', function () {
    if (!dialedNumber) return;
    startCall(dialedNumber);
  });

  function startCall(number) {
    currentCallNumber = number;
    requestService('telephony', 'dial', { number: number });
    showInCall(number);
  }

  function showInCall(number) {
    incallName.textContent = '';
    incallNumber.textContent = number;
    if (typeof zylI18n !== 'undefined') {
      incallStatus.textContent = zylI18n.t('phone.dialing');
    }
    incallTimer.textContent = '00:00';
    isMuted = false;
    isSpeaker = false;
    isDtmfVisible = false;
    dtmfDigits = '';
    btnMute.classList.remove('active');
    btnSpeaker.classList.remove('active');
    btnKeypadIncall.classList.remove('active');
    dtmfOverlay.classList.add('hidden');
    dtmfDigitsEl.textContent = '';
    incallOverlay.classList.remove('hidden');

    /* Start call timer */
    callStartTime = Date.now();
    clearInterval(callTimerInterval);
    callTimerInterval = setInterval(updateCallTimer, 1000);
  }

  function updateCallTimer() {
    var elapsed = Math.floor((Date.now() - callStartTime) / 1000);
    var mins = Math.floor(elapsed / 60);
    var secs = elapsed % 60;
    incallTimer.textContent =
      (mins < 10 ? '0' : '') + mins + ':' +
      (secs < 10 ? '0' : '') + secs;
  }

  function endCall() {
    requestService('telephony', 'hangup', { number: currentCallNumber });
    clearInterval(callTimerInterval);

    var duration = Math.floor((Date.now() - callStartTime) / 1000);
    requestService('telephony', 'addCallLog', {
      number: currentCallNumber,
      type: 'outgoing',
      duration: duration,
      timestamp: Date.now()
    });

    incallOverlay.classList.add('hidden');
    isDtmfVisible = false;
    dtmfDigits = '';
    currentCallNumber = '';
    callStartTime = 0;
  }

  btnEndCall.addEventListener('click', endCall);

  btnMute.addEventListener('click', function () {
    isMuted = !isMuted;
    btnMute.classList.toggle('active', isMuted);
    requestService('telephony', 'setMute', { muted: isMuted });
  });

  btnSpeaker.addEventListener('click', function () {
    isSpeaker = !isSpeaker;
    btnSpeaker.classList.toggle('active', isSpeaker);
    requestService('telephony', 'setSpeaker', { enabled: isSpeaker });
  });

  /* ═══════════════════════════════════════════════════════════
     DTMF In-Call Keypad
     ═══════════════════════════════════════════════════════════ */

  btnKeypadIncall.addEventListener('click', function () {
    isDtmfVisible = !isDtmfVisible;
    btnKeypadIncall.classList.toggle('active', isDtmfVisible);
    if (isDtmfVisible) {
      dtmfOverlay.classList.remove('hidden');
    } else {
      dtmfOverlay.classList.add('hidden');
    }
  });

  dtmfGrid.addEventListener('click', function (e) {
    var btn = e.target.closest('.dtmf-btn');
    if (!btn) return;
    var digit = btn.getAttribute('data-digit');
    if (!digit) return;
    dtmfDigits += digit;
    dtmfDigitsEl.textContent = dtmfDigits;
    requestService('telephony', 'sendDTMF', { digit: digit });
    requestService('audio', 'playKeyClick', {});
  });

  /* ═══════════════════════════════════════════════════════════
     Incoming Call Handling
     ═══════════════════════════════════════════════════════════ */

  function handleIncomingCall(data) {
    currentCallNumber = data.number || '';
    incallName.textContent = data.name || '';
    incallNumber.textContent = data.number || '';
    if (typeof zylI18n !== 'undefined') {
      incallStatus.textContent = zylI18n.t('phone.incoming');
    }
    incallTimer.textContent = '00:00';
    isMuted = false;
    isSpeaker = false;
    isDtmfVisible = false;
    dtmfDigits = '';
    btnMute.classList.remove('active');
    btnSpeaker.classList.remove('active');
    btnKeypadIncall.classList.remove('active');
    dtmfOverlay.classList.add('hidden');
    dtmfDigitsEl.textContent = '';
    incallOverlay.classList.remove('hidden');

    callStartTime = Date.now();
    clearInterval(callTimerInterval);
    callTimerInterval = setInterval(updateCallTimer, 1000);
  }

  function handleCallStateChange(data) {
    if (data.state === 'connected') {
      if (typeof zylI18n !== 'undefined') {
        incallStatus.textContent = zylI18n.t('phone.calling');
      }
      callStartTime = Date.now();
    }
    if (data.state === 'ended') {
      clearInterval(callTimerInterval);
      incallOverlay.classList.add('hidden');
      isDtmfVisible = false;
      dtmfDigits = '';
      currentCallNumber = '';
      loadRecents();
    }
  }

  /* ═══════════════════════════════════════════════════════════
     Recents
     ═══════════════════════════════════════════════════════════ */

  function loadRecents() {
    requestService('telephony', 'getCallLog', {});
  }

  onServiceResponse('telephony', 'getCallLog', function (data) {
    if (!data || !Array.isArray(data.logs) || data.logs.length === 0) {
      recentsList.innerHTML = '';
      recentsEmpty.classList.remove('hidden');
      return;
    }
    recentsEmpty.classList.add('hidden');
    renderRecents(data.logs);
  });

  var DIR_ICONS = {
    incoming: '<svg viewBox="0 0 24 24" width="16" height="16"><path fill="currentColor" d="M20 5.41L18.59 4 7 15.59V9H5v10h10v-2H8.41z"/></svg>',
    outgoing: '<svg viewBox="0 0 24 24" width="16" height="16"><path fill="currentColor" d="M9 5v2h6.59L4 18.59 5.41 20 17 8.41V15h2V5z"/></svg>',
    missed: '<svg viewBox="0 0 24 24" width="16" height="16"><path fill="currentColor" d="M19.59 7L12 14.59 6.41 9H11V7H3v8h2v-4.59l7 7 9-9z"/></svg>'
  };

  function renderRecents(logs) {
    var html = '';
    for (var i = 0; i < logs.length; i++) {
      var log = logs[i];
      var dirClass = log.type === 'incoming' ? 'incoming' :
                     log.type === 'missed' ? 'missed' : 'outgoing';
      var dirLabel = '';
      if (typeof zylI18n !== 'undefined') {
        dirLabel = log.type === 'incoming' ? zylI18n.t('phone.incoming') :
                   log.type === 'missed' ? zylI18n.t('phone.missed') :
                   zylI18n.t('phone.outgoing');
      }
      var timeStr = formatRecentTime(log.timestamp);
      var displayName = log.name || log.number || '';
      var nameClass = dirClass === 'missed' ? 'recent-name missed' : 'recent-name';
      var logId = log.id || '';
      html += '<div class="recent-item" data-number="' + escapeAttr(log.number || '') + '" data-log-id="' + escapeAttr(logId) + '">' +
        '<div class="recent-direction ' + dirClass + '">' + (DIR_ICONS[dirClass] || '') + '</div>' +
        '<div class="recent-info">' +
          '<div class="' + nameClass + '">' + escapeHtml(displayName) + '</div>' +
          '<div class="recent-time">' + escapeHtml(dirLabel + ' \u00B7 ' + timeStr) + '</div>' +
        '</div>' +
        '<button class="recent-call-btn" data-number="' + escapeAttr(log.number || '') + '">' +
          '<svg viewBox="0 0 24 24" width="18" height="18"><path fill="currentColor" d="M6.62 10.79c1.44 2.83 3.76 5.14 6.59 6.59l2.2-2.2c.27-.27.67-.36 1.02-.24 1.12.37 2.33.57 3.57.57.55 0 1 .45 1 1V20c0 .55-.45 1-1 1-9.39 0-17-7.61-17-17 0-.55.45-1 1-1h3.5c.55 0 1 .45 1 1 0 1.25.2 2.45.57 3.57.11.35.03.74-.25 1.02l-2.2 2.2z"/></svg>' +
        '</button>' +
      '</div>';
    }
    recentsList.innerHTML = html;
  }

  /* Click to dial from recents */
  recentsList.addEventListener('click', function (e) {
    /* Ignore if clicking the call button directly */
    if (e.target.closest('.recent-call-btn')) {
      var callBtn = e.target.closest('.recent-call-btn');
      var num = callBtn.getAttribute('data-number');
      if (num) {
        dialedNumber = num;
        updateDisplay();
        switchTab('keypad');
      }
      return;
    }
    var item = e.target.closest('.recent-item');
    if (!item) return;
    var number = item.getAttribute('data-number');
    if (number) {
      dialedNumber = number;
      updateDisplay();
      switchTab('keypad');
    }
  });

  /* Long-press to delete a call log entry */
  recentsList.addEventListener('touchstart', function (e) {
    var item = e.target.closest('.recent-item');
    if (!item) return;
    var logId = item.getAttribute('data-log-id');
    if (!logId) return;
    clearTimeout(longPressTimer);
    longPressTimer = setTimeout(function () {
      item.classList.add('longpress-active');
      setTimeout(function () {
        item.classList.remove('longpress-active');
      }, 300);
      showDeleteLogModal(logId);
    }, 600);
  });

  recentsList.addEventListener('touchend', function () {
    clearTimeout(longPressTimer);
  });

  recentsList.addEventListener('touchmove', function () {
    clearTimeout(longPressTimer);
  });

  /* ═══════════════════════════════════════════════════════════
     Delete Call Log Modal
     ═══════════════════════════════════════════════════════════ */

  function showDeleteLogModal(logId) {
    pendingDeleteLogId = logId;
    deleteLogModal.classList.remove('hidden');
    applyI18n();
  }

  function hideDeleteLogModal() {
    pendingDeleteLogId = null;
    deleteLogModal.classList.add('hidden');
  }

  btnDeleteCancel.addEventListener('click', hideDeleteLogModal);

  document.getElementById('delete-log-backdrop').addEventListener('click', hideDeleteLogModal);

  btnDeleteConfirm.addEventListener('click', function () {
    if (pendingDeleteLogId) {
      requestService('telephony', 'deleteCallLog', { id: pendingDeleteLogId });
    }
    hideDeleteLogModal();
    /* Reload recents after deletion */
    setTimeout(function () {
      loadRecents();
    }, 200);
  });

  onServiceResponse('telephony', 'deleteCallLog', function () {
    loadRecents();
  });

  function formatRecentTime(ts) {
    if (!ts) return '';
    var d = new Date(ts);
    var now = new Date();
    var diffMs = now.getTime() - d.getTime();
    var diffMin = Math.floor(diffMs / 60000);
    if (diffMin < 1) {
      return typeof zylI18n !== 'undefined' ? zylI18n.t('phone.just_now') : '';
    }
    if (diffMin < 60) return diffMin + 'm';
    var diffH = Math.floor(diffMin / 60);
    if (diffH < 24) return diffH + 'h';
    var pad = function (n) { return (n < 10 ? '0' : '') + n; };
    return pad(d.getMonth() + 1) + '/' + pad(d.getDate());
  }

  /* ═══════════════════════════════════════════════════════════
     Contacts
     ═══════════════════════════════════════════════════════════ */

  function loadContacts() {
    requestService('contacts', 'getAll', {});
  }

  onServiceResponse('contacts', 'getAll', function (data) {
    if (!data || !Array.isArray(data.contacts)) {
      contactsList.innerHTML = '';
      return;
    }
    renderContacts(data.contacts);
  });

  function avatarColor(name) {
    var code = 0;
    for (var j = 0; j < (name || '').length; j++) code += name.charCodeAt(j);
    return 'avatar-' + (code % 10);
  }

  var STAR_SVG_FILLED = '<svg viewBox="0 0 24 24"><path fill="currentColor" d="M12 17.27L18.18 21l-1.64-7.03L22 9.24l-7.19-.61L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21z"/></svg>';
  var STAR_SVG_EMPTY = '<svg viewBox="0 0 24 24"><path fill="currentColor" d="M22 9.24l-7.19-.62L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21 12 17.27 18.18 21l-1.64-7.03L22 9.24zM12 15.4l-3.76 2.27 1-4.28-3.32-2.88 4.38-.38L12 6.1l1.71 4.04 4.38.38-3.32 2.88 1 4.28L12 15.4z"/></svg>';

  function renderContacts(contacts) {
    var html = '';
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var initial = (c.name || '?').charAt(0).toUpperCase();
      var phone = c.phone || c.number || '';
      var colorClass = avatarColor(c.name || phone);
      var contactId = c.id || phone;
      var isStarred = !!favoritesSet[contactId];
      var starClass = 'btn-star' + (isStarred ? ' starred' : '');
      var starTitle = '';
      if (typeof zylI18n !== 'undefined') {
        starTitle = isStarred ? zylI18n.t('phone.remove_favorite') : zylI18n.t('phone.add_favorite');
      }
      html += '<div class="contact-item" data-number="' + escapeAttr(phone) + '" data-contact-id="' + escapeAttr(contactId) + '">' +
        '<div class="contact-avatar ' + colorClass + '">' + escapeHtml(initial) + '</div>' +
        '<div class="contact-name">' + escapeHtml(c.name || phone) + '</div>' +
        '<button class="' + starClass + '" data-contact-id="' + escapeAttr(contactId) + '" title="' + escapeAttr(starTitle) + '">' +
          (isStarred ? STAR_SVG_FILLED : STAR_SVG_EMPTY) +
        '</button>' +
      '</div>';
    }
    contactsList.innerHTML = html;
  }

  contactsList.addEventListener('click', function (e) {
    /* Star button click */
    var starBtn = e.target.closest('.btn-star');
    if (starBtn) {
      var contactId = starBtn.getAttribute('data-contact-id');
      if (contactId) {
        toggleFavorite(contactId);
      }
      return;
    }
    var item = e.target.closest('.contact-item');
    if (!item) return;
    var number = item.getAttribute('data-number');
    if (number) {
      dialedNumber = number;
      updateDisplay();
      switchTab('keypad');
    }
  });

  /* ═══════════════════════════════════════════════════════════
     Favorites
     ═══════════════════════════════════════════════════════════ */

  function loadFavorites() {
    requestService('telephony', 'getFavorites', {});
  }

  onServiceResponse('telephony', 'getFavorites', function (data) {
    if (!data || !Array.isArray(data.favorites) || data.favorites.length === 0) {
      favoritesList.innerHTML = '';
      favoritesEmpty.classList.remove('hidden');
      /* Update favoritesSet */
      favoritesSet = {};
      return;
    }
    favoritesEmpty.classList.add('hidden');
    /* Build favoritesSet lookup */
    favoritesSet = {};
    for (var i = 0; i < data.favorites.length; i++) {
      var fav = data.favorites[i];
      var favId = fav.id || fav.contactId || '';
      if (favId) {
        favoritesSet[favId] = true;
      }
    }
    renderFavorites(data.favorites);
  });

  function renderFavorites(favorites) {
    var html = '';
    for (var i = 0; i < favorites.length; i++) {
      var fav = favorites[i];
      var phone = fav.phone || fav.number || '';
      var name = fav.name || phone;
      var initial = (name || '?').charAt(0).toUpperCase();
      var colorClass = avatarColor(name);
      html += '<div class="favorite-item" data-number="' + escapeAttr(phone) + '">' +
        '<div class="contact-avatar ' + colorClass + '">' + escapeHtml(initial) + '</div>' +
        '<div class="contact-name">' + escapeHtml(name) + '</div>' +
        '<div class="favorite-star">' + STAR_SVG_FILLED + '</div>' +
      '</div>';
    }
    favoritesList.innerHTML = html;
  }

  favoritesList.addEventListener('click', function (e) {
    var item = e.target.closest('.favorite-item');
    if (!item) return;
    var number = item.getAttribute('data-number');
    if (number) {
      startCall(number);
    }
  });

  function toggleFavorite(contactId) {
    if (favoritesSet[contactId]) {
      requestService('telephony', 'removeFavorite', { contactId: contactId });
      delete favoritesSet[contactId];
    } else {
      requestService('telephony', 'addFavorite', { contactId: contactId });
      favoritesSet[contactId] = true;
    }
    /* Refresh contacts to update star icons */
    loadContacts();
    /* Refresh favorites list */
    loadFavorites();
  }

  onServiceResponse('telephony', 'addFavorite', function () {
    loadFavorites();
  });

  onServiceResponse('telephony', 'removeFavorite', function () {
    loadFavorites();
  });

  /* Load favorites on init to populate favoritesSet */
  loadFavorites();

  /* ═══════════════════════════════════════════════════════════
     Helpers
     ═══════════════════════════════════════════════════════════ */

  function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function escapeAttr(str) {
    return escapeHtml(str).replace(/'/g, '&#39;');
  }

  /* ═══════════════════════════════════════════════════════════
     i18n apply
     ═══════════════════════════════════════════════════════════ */

  function applyI18n() {
    if (typeof zylI18n === 'undefined') return;
    var els = document.querySelectorAll('[data-i18n]');
    for (var i = 0; i < els.length; i++) {
      var key = els[i].getAttribute('data-i18n');
      var val = zylI18n.t(key);
      if (val) els[i].textContent = val;
    }
    var placeholders = document.querySelectorAll('[data-i18n-placeholder]');
    for (var j = 0; j < placeholders.length; j++) {
      var pk = placeholders[j].getAttribute('data-i18n-placeholder');
      var pv = zylI18n.t(pk);
      if (pv) placeholders[j].setAttribute('placeholder', pv);
    }
  }

  window.addEventListener('languagechange', applyI18n);
  applyI18n();

})();
