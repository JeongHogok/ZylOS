// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: OOBE (Out-of-Box Experience) wizard logic
// Scope: Language selection, WiFi connection, PIN setup, terms acceptance, completion flag
// Dependency: ZylBridge (shared/bridge.js), zylI18n (shared/i18n.js)
// SOLID: SRP — OOBE flow management only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  var steps = ['step-welcome', 'step-language', 'step-wifi', 'step-pin', 'step-terms', 'step-complete'];
  var currentStep = 0;

  /* ─── Navigation ─── */
  window.nextStep = function () {
    if (currentStep >= steps.length - 1) return;
    var current = document.getElementById(steps[currentStep]);
    current.classList.remove('active');
    currentStep++;
    var next = document.getElementById(steps[currentStep]);
    next.classList.add('active');
    updateDots();
  };

  window.prevStep = function () {
    if (currentStep <= 0) return;
    var current = document.getElementById(steps[currentStep]);
    current.classList.remove('active');
    currentStep--;
    var prev = document.getElementById(steps[currentStep]);
    prev.classList.add('active');
    updateDots();
  };

  function updateDots() {
    var dots = document.querySelectorAll('#progress-dots .dot');
    dots.forEach(function (dot, i) {
      dot.classList.toggle('active', i === currentStep);
    });
  }

  /* ─── Language Selection ─── */
  document.querySelectorAll('.oobe-option[data-lang]').forEach(function (opt) {
    opt.addEventListener('click', function () {
      document.querySelectorAll('.oobe-option[data-lang]').forEach(function (o) {
        o.classList.remove('selected');
      });
      opt.classList.add('selected');
      var lang = opt.dataset.lang;

      /* Apply locale via i18n */
      if (typeof zylI18n !== 'undefined') {
        zylI18n.setLocale(lang);
      }
      /* Save locale to settings — emulator applies via applySettingSideEffect */
      if (window.parent && window.parent !== window) {
        window.parent.postMessage(JSON.stringify({
          type: 'service.request',
          service: 'settings',
          method: 'update',
          params: { category: 'language', key: 'locale', value: lang }
        }), '*');
      }
    });
  });

  /* ─── WiFi List ─── */
  if (window.parent && window.parent !== window) {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request',
      service: 'wifi',
      method: 'getNetworks'
    }), '*');
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (msg && msg.type === 'service.response' && msg.service === 'wifi' && msg.data) {
        var list = document.getElementById('wifi-list');
        if (!list) return;
        list.innerHTML = '';
        if (msg.data.length === 0) {
          list.innerHTML = '<div class="oobe-option">No networks found</div>';
          return;
        }
        msg.data.forEach(function (net) {
          var el = document.createElement('div');
          el.className = 'oobe-option' + (net.connected ? ' selected' : '');
          el.textContent = net.ssid + (net.connected ? ' (Connected)' : '');
          el.addEventListener('click', function () {
            list.querySelectorAll('.oobe-option').forEach(function (o) { o.classList.remove('selected'); });
            el.classList.add('selected');
          });
          list.appendChild(el);
        });
      }
    } catch (err) { /* ignore */ }
  });

  /* ─── PIN Setup ─── */
  window.setupPin = function () {
    var pin = document.getElementById('pin-input').value;
    var confirmPin = document.getElementById('pin-confirm').value;
    var msg = document.getElementById('pin-msg');

    if (pin.length !== 4 || !/^\d{4}$/.test(pin)) {
      msg.textContent = 'PIN must be 4 digits';
      msg.style.color = '#ef4444';
      return;
    }
    if (pin !== confirmPin) {
      msg.textContent = 'PINs do not match';
      msg.style.color = '#ef4444';
      return;
    }

    msg.textContent = '';
    /* Save PIN to system settings */
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({
        type: 'service.request',
        service: 'settings',
        method: 'update',
        params: { category: 'security', key: 'pin', value: pin }
      }), '*');
    }
    nextStep();
  };

  /* ─── Terms Agreement ─── */
  window.acceptTerms = function () {
    var agreed = document.getElementById('terms-agree').checked;
    if (!agreed) return;
    nextStep();
  };

  /* ─── Setup Complete → save flag + go home ─── */
  window.finishSetup = function () {
    /* Save OOBE completion flag so it doesn't show again */
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({
        type: 'service.request',
        service: 'settings',
        method: 'update',
        params: { category: 'system', key: 'oobe_completed', value: true }
      }), '*');
      /* Launch home */
      window.parent.postMessage(JSON.stringify({
        type: 'app.launch', appId: 'com.zylos.home'
      }), '*');
    }
  };

  /* Select default language based on i18n locale (avoid navigator.language bypass) */
  var detectedLang = (typeof zylI18n !== 'undefined' && zylI18n.getLocale) ? zylI18n.getLocale() : 'en';
  var langOption = document.querySelector('.oobe-option[data-lang="' + detectedLang + '"]');
  if (langOption) {
    document.querySelectorAll('.oobe-option[data-lang]').forEach(function (o) { o.classList.remove('selected'); });
    langOption.classList.add('selected');
  } else {
    /* Default to English */
    var enOpt = document.querySelector('.oobe-option[data-lang="en"]');
    if (enOpt) enOpt.classList.add('selected');
  }

})();
