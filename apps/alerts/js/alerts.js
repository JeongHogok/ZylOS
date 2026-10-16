// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Emergency alerts — full-screen alert with the attention signal until
//       the user confirms it, received-alert history, receive settings
// Scope: shows unacknowledged alerts oldest first, acknowledges them with the
//        telephony service, renders history, per-category toggles
//        (presidential alerts cannot be turned off)
// Dependency: ZylBridge (telephony, audio services), zylI18n
// SOLID: SRP — alert presentation only; reception, classification and
//        storage are the telephony daemon's (runtime/services/telephony/cbs.c)
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  var CATEGORIES = ['presidential', 'extreme', 'severe', 'amber',
                    'public_safety', 'earthquake', 'test'];

  var alertEl = document.getElementById('alert');
  var historyEl = document.getElementById('history');
  var emptyEl = document.getElementById('history-empty');
  var settingsEl = document.getElementById('settings');
  var showing = null;          /* the alert on screen */
  var toneTimers = [];

  function t(key) { return zylI18n.t(key); }

  function stamp(seconds) {
    var d = new Date(seconds * 1000);
    return zylI18n.formatDate(d) + ' ' + zylI18n.formatTime(d);
  }

  /* ─── Attention signal ───
     WEA/CMAS temporal pattern (2 s on, 0.5 s off, 1 s on, 0.5 s off, 1 s on),
     played twice. The standard tone is 853 Hz and 960 Hz together; PlayTone
     takes one frequency, so the two alternate every 250 ms.
     shortcut: alternating single tones approximate the dual tone; switch to a
     mixed tone when the audio service can play one. */
  var PATTERN = [2000, 500, 1000, 500, 1000, 500];

  function stopTone() {
    while (toneTimers.length) clearTimeout(toneTimers.pop());
  }

  function playAttentionSignal() {
    stopTone();
    var at = 0;
    for (var round = 0; round < 2; round++) {
      for (var i = 0; i < PATTERN.length; i++) {
        if (i % 2 === 0) {
          for (var s = 0; s < PATTERN[i]; s += 250) {
            (function (delay, freq) {
              toneTimers.push(setTimeout(function () {
                ZylBridge.requestService('audio', 'playTone', { freqHz: freq, durationMs: 250 });
              }, delay));
            })(at + s, (s / 250) % 2 ? 960 : 853);
          }
        }
        at += PATTERN[i];
      }
    }
  }

  /* ─── Full-screen alert ─── */
  function render(alert) {
    alertEl.className = 'alert cat-' + alert.category;
    document.getElementById('alert-title').textContent = t('alerts.cat.' + alert.category);
    document.getElementById('alert-time').textContent = stamp(alert.at);
    document.getElementById('alert-text').textContent = alert.text;   /* never as HTML */
  }

  /* Felt as well as heard — the attention signal's on/off as a vibration
     (the audio service follows the user's vibration setting) */
  function vibrateAttention() {
    var pattern = [];
    for (var round = 0; round < 2; round++) pattern = pattern.concat(PATTERN);
    ZylBridge.requestService('audio', 'vibrate', { pattern: pattern });
  }

  function show(alert) {
    showing = alert;
    render(alert);
    playAttentionSignal();
    vibrateAttention();
    document.getElementById('alert-ok').focus();
  }

  function hide() {
    showing = null;
    stopTone();
    alertEl.className = 'alert hidden';
  }

  document.getElementById('alert-ok').addEventListener('click', function () {
    if (!showing) return;
    var id = showing.id;
    hide();
    ZylBridge.requestService('telephony', 'acknowledgeEmergencyAlert', { id: id })
      .then(refresh, refresh);
  });

  /* ─── History ─── */
  function renderHistory(list) {
    historyEl.textContent = '';
    emptyEl.classList.toggle('hidden', list.length > 0);
    historyEl.classList.toggle('hidden', list.length === 0);
    list.forEach(function (a) {
      var item = document.createElement('div');
      item.className = 'item';
      var head = document.createElement('div');
      head.className = 'item-head';
      var cat = document.createElement('span');
      cat.className = 'item-cat';
      cat.textContent = t('alerts.cat.' + a.category);
      var when = document.createElement('span');
      when.textContent = stamp(a.at);
      head.appendChild(cat);
      head.appendChild(when);
      var text = document.createElement('div');
      text.className = 'item-text';
      text.textContent = a.text;      /* network-supplied: never as HTML */
      item.appendChild(head);
      item.appendChild(text);
      historyEl.appendChild(item);
    });
  }

  function refresh() {
    ZylBridge.requestService('telephony', 'getEmergencyAlerts').then(function (list) {
      list = (list && list.length) ? list : [];
      renderHistory(list);
      if (showing) return;
      /* Oldest unconfirmed first: the list is newest first */
      for (var i = list.length - 1; i >= 0; i--) {
        if (!list[i].acked) { show(list[i]); return; }
      }
    }, function () { renderHistory([]); });
  }

  /* ─── Receive settings ─── */
  /* enabled: null when the service could not say — the switches are then
     disabled rather than shown off, which would misstate what is received */
  function renderSettings(enabled) {
    settingsEl.textContent = '';
    CATEGORIES.forEach(function (c) {
      var row = document.createElement('label');
      row.className = 'row';
      var name = document.createElement('span');
      name.textContent = t('alerts.cat.' + c);
      row.appendChild(name);
      if (c === 'presidential') {
        var note = document.createElement('span');
        note.className = 'row-note';
        note.textContent = t('alerts.always_on');
        row.appendChild(note);
      } else {
        var box = document.createElement('input');
        box.type = 'checkbox';
        box.checked = !!(enabled && enabled[c]);
        box.disabled = !enabled;
        box.setAttribute('aria-label', t('alerts.cat.' + c));
        box.addEventListener('change', function () {
          var wanted = box.checked;
          ZylBridge.requestService('telephony', 'setEmergencyAlertEnabled',
            { category: c, enabled: wanted }).then(function (ok) {
            if (ok !== true) box.checked = !wanted;   /* not applied — show what is */
          }, function () { box.checked = !wanted; });
        });
        row.appendChild(box);
      }
      settingsEl.appendChild(row);
    });
  }

  function loadSettings() {
    ZylBridge.requestService('telephony', 'getEmergencyAlertSettings').then(function (e) {
      renderSettings(e && typeof e.extreme === 'boolean' ? e : null);
    }, function () { renderSettings(null); });
  }

  /* ─── System messages ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    var msg;
    try { msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data; } catch (err) { return; }
    if (!msg) return;
    if (msg.type === 'navigation.back') {
      /* An alert on screen is dismissed with its button, not by backing out */
      ZylBridge.sendToSystem({ type: showing ? 'navigation.handled' : 'navigation.exit' });
    }
  });

  /* A new alert while this app is open (the WAM opens it for one otherwise) */
  ZylBridge.onServiceEvent('telephony', 'EmergencyAlert', refresh);

  zylI18n.onLocaleChange(function () {
    zylI18n.applyTranslations();
    if (showing) render(showing);
    refresh();
    loadSettings();
  });

  zylI18n.applyTranslations();
  refresh();
  loadSettings();
})();
