// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 전화 앱 — 키패드, 통화기록, 연락처, 통화화면
// 수행범위: 키패드 입력, 발신/수신/종료, 통화기록 조회, 연락처 목록
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

  /* In-call */
  var incallOverlay = document.getElementById('incall-overlay');
  var incallStatus = document.getElementById('incall-status');
  var incallName = document.getElementById('incall-name');
  var incallNumber = document.getElementById('incall-number');
  var incallTimer = document.getElementById('incall-timer');
  var btnEndCall = document.getElementById('btn-end-call');
  var btnMute = document.getElementById('btn-mute');
  var btnSpeaker = document.getElementById('btn-speaker');

  /* ═══════════════════════════════════════════════════════════
     State
     ═══════════════════════════════════════════════════════════ */

  var dialedNumber = '';
  var callTimerInterval = null;
  var callStartTime = 0;
  var isMuted = false;
  var isSpeaker = false;
  var currentCallNumber = '';

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
    btnMute.classList.remove('active');
    btnSpeaker.classList.remove('active');
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
    btnMute.classList.remove('active');
    btnSpeaker.classList.remove('active');
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
      var dirArrow = log.type === 'incoming' ? '\u2199' :
                     log.type === 'missed' ? '\u2199' : '\u2197';
      var dirLabel = '';
      if (typeof zylI18n !== 'undefined') {
        dirLabel = log.type === 'incoming' ? zylI18n.t('phone.incoming') :
                   log.type === 'missed' ? zylI18n.t('phone.missed') :
                   zylI18n.t('phone.outgoing');
      }
      var timeStr = formatRecentTime(log.timestamp);
      var displayName = log.name || log.number || '';
      var nameClass = dirClass === 'missed' ? 'recent-name missed' : 'recent-name';
      html += '<div class="recent-item" data-number="' + escapeAttr(log.number || '') + '">' +
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

  recentsList.addEventListener('click', function (e) {
    var item = e.target.closest('.recent-item');
    if (!item) return;
    var number = item.getAttribute('data-number');
    if (number) {
      dialedNumber = number;
      updateDisplay();
      switchTab('keypad');
    }
  });

  function formatRecentTime(ts) {
    if (!ts) return '';
    var d = new Date(ts);
    var now = new Date();
    var diffMs = now.getTime() - d.getTime();
    var diffMin = Math.floor(diffMs / 60000);
    if (diffMin < 1) {
      return typeof zylI18n !== 'undefined' ? zylI18n.t('phone.just_now') || 'Just now' : 'Just now';
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

  function renderContacts(contacts) {
    var html = '';
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var initial = (c.name || '?').charAt(0).toUpperCase();
      var phone = c.phone || c.number || '';
      var colorClass = avatarColor(c.name || phone);
      html += '<div class="contact-item" data-number="' + escapeAttr(phone) + '">' +
        '<div class="contact-avatar ' + colorClass + '">' + escapeHtml(initial) + '</div>' +
        '<div class="contact-name">' + escapeHtml(c.name || phone) + '</div>' +
      '</div>';
    }
    contactsList.innerHTML = html;
  }

  contactsList.addEventListener('click', function (e) {
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
