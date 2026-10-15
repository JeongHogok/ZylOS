// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Host
//
// 역할: OS 키보드를 앱 문서 안에 띄우는 호스트 — 기기와 에뮬레이터 공통
// 수행범위: 키보드 스크립트·스타일 지연 로드, 컨테이너 생성, 키 입력을 브릿지의
//          input.key / input.text 적용 경로로 전달, 키보드 설정(언어·키 높이·
//          키음·진동) 적용, 표시/숨김과 앱 레이아웃 여백(keyboard.show/hide).
//          키음·진동 요청은 keyboard.js 한 곳에서만
// 의존방향: ZylKeyboard (keyboard.js), ZylBridge (shared/bridge.js), zylI18n
// SOLID: SRP — 키보드 호스팅만. 입력기는 keyboard.js/hangul.js/ime.js 몫
//
// shared/bridge.js 가 텍스트 입력에 포커스가 처음 올 때 이 파일을 불러온다.
// 기기의 컴포지터에는 입력기 창 프로토콜이 없고, 같은 문서에 있으면 포커스·
// 커서·조합을 IPC 없이 그대로 다룰 수 있다.
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  if (window.ZylKeyboardHost) return;

  var base = (document.currentScript && document.currentScript.src || '')
    .replace(/js\/inpage\.js(\?.*)?$/, '');
  var SCRIPTS = ['i18n.js', 'js/hangul.js', 'js/ime.js', 'js/prediction.js', 'js/keyboard.js'];
  var container = null;
  var waiting = [];

  function loadScripts(i, done) {
    if (i >= SCRIPTS.length) return done();
    var s = document.createElement('script');
    s.src = base + SCRIPTS[i];
    s.onload = function () { loadScripts(i + 1, done); };
    s.onerror = function () { loadScripts(i + 1, done); };   /* a missing extra (prediction) must not block typing */
    document.head.appendChild(s);
  }

  /* Edits take the same path the emulator compositor used: a message the
     bridge applies to the focused field */
  function deliver(key, edit) {
    var msg = edit ? { type: 'input.text', data: edit } : { type: 'input.key', data: { key: key } };
    window.postMessage(JSON.stringify(msg), '*');
  }

  /* Read on every show: a change in Settings applies at the next text field */
  function applySettings() {
    ZylBridge.requestService('settings', 'get', { category: 'keyboard' }).then(function (kb) {
      if (!kb) return;
      var K = window.ZylKeyboard;
      if (kb.languages) {
        var langs = String(kb.languages).split(',').filter(Boolean);
        K.setEnabledLanguages(langs);
        if (langs.length && langs.indexOf(K.getLanguage()) === -1) K.setLanguage(langs[0]);
      }
      if (kb.keyHeight) K.setKeyHeight(parseInt(kb.keyHeight, 10) || 36);
      if (kb.soundEnabled !== undefined) K.setSoundEnabled(kb.soundEnabled !== false);
      if (kb.vibrationEnabled !== undefined) K.setVibrationEnabled(kb.vibrationEnabled !== false);
    }, function () { /* defaults stay */ });
  }

  function mount() {
    container = document.createElement('div');
    container.id = 'zyl-keyboard';
    container.className = 'zyl-keyboard';
    /* Keys must not take focus away from the field being typed into */
    container.addEventListener('mousedown', function (e) { e.preventDefault(); });
    document.body.appendChild(container);
    window.ZylKeyboard.init(container, deliver);
    var l = document.createElement('link');
    l.rel = 'stylesheet';
    l.href = base + 'css/keyboard.css';
    document.head.appendChild(l);
  }

  function ready(cb) {
    if (container) return cb();
    waiting.push(cb);
    if (waiting.length > 1) return;
    loadScripts(0, function () {
      if (!window.ZylKeyboard) { waiting = []; return; }
      mount();
      var w = waiting;
      waiting = [];
      w.forEach(function (fn) { fn(); });
    });
  }

  function show() {
    ready(function () {
      applySettings();
      window.ZylKeyboard.show();
      window.postMessage(JSON.stringify({ type: 'keyboard.show', data: { height: container.offsetHeight } }), '*');
    });
  }

  function hide() {
    if (!container || !window.ZylKeyboard.isVisible()) return;
    window.ZylKeyboard.hide();
    window.postMessage(JSON.stringify({ type: 'keyboard.hide', data: {} }), '*');
  }

  window.ZylKeyboardHost = {
    show: show,
    hide: hide,
    owns: function (el) { return !!(container && el && container.contains(el)); }
  };
})();
