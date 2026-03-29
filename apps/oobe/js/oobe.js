// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: OOBE (첫 실행 설정 마법사) 로직
// 수행범위: 언어 선택, WiFi 연결, PIN 설정, 약관 동의, 설정 완료
// 의존방향: ZylBridge (shared/bridge.js)
// SOLID: SRP — OOBE 흐름 관리만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  var steps = ['step-welcome', 'step-language', 'step-wifi', 'step-pin', 'step-terms', 'step-complete'];
  var currentStep = 0;

  /* 다음 단계 */
  window.nextStep = function () {
    if (currentStep >= steps.length - 1) return;
    var current = document.getElementById(steps[currentStep]);
    current.classList.remove('active');
    currentStep++;
    var next = document.getElementById(steps[currentStep]);
    next.classList.add('active');
    updateDots();
  };

  /* 진행 도트 업데이트 */
  function updateDots() {
    var dots = document.querySelectorAll('#progress-dots .dot');
    dots.forEach(function (dot, i) {
      dot.classList.toggle('active', i === currentStep);
    });
  }

  /* 언어 선택 */
  document.querySelectorAll('.oobe-option[data-lang]').forEach(function (opt) {
    opt.addEventListener('click', function () {
      document.querySelectorAll('.oobe-option[data-lang]').forEach(function (o) {
        o.classList.remove('selected');
      });
      opt.classList.add('selected');
      var lang = opt.dataset.lang;
      if (typeof ZylBridge !== 'undefined') {
        ZylBridge.setLocale(lang);
      }
    });
  });

  /* WiFi 목록 요청 */
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
          list.innerHTML = '<div class="oobe-option">네트워크 없음</div>';
          return;
        }
        msg.data.forEach(function (net) {
          var el = document.createElement('div');
          el.className = 'oobe-option' + (net.connected ? ' selected' : '');
          el.textContent = net.ssid + (net.connected ? ' (연결됨)' : '');
          list.appendChild(el);
        });
      }
    } catch (err) { /* ignore */ }
  });

  /* PIN 설정 */
  window.setupPin = function () {
    var pin = document.getElementById('pin-input').value;
    var confirm = document.getElementById('pin-confirm').value;
    var msg = document.getElementById('pin-msg');

    if (pin.length !== 4) {
      msg.textContent = '4자리 PIN을 입력하세요';
      return;
    }
    if (pin !== confirm) {
      msg.textContent = 'PIN이 일치하지 않습니다';
      return;
    }

    msg.textContent = '';
    /* PIN을 시스템에 저장 */
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

  /* 약관 동의 */
  window.acceptTerms = function () {
    var agreed = document.getElementById('terms-agree').checked;
    if (!agreed) {
      return;
    }
    nextStep();
  };

  /* 설정 완료 → 홈으로 */
  window.finishSetup = function () {
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({ type: 'app.launch', appId: 'com.zylos.home' }), '*');
    }
  };

})();
