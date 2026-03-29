// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - UI Component
//
// 역할: Zyl OS 가상 키보드 — 에뮬레이터 컴포지터에 직접 로드
// 수행범위: QWERTY 레이아웃 렌더링, 키 입력 콜백 전달,
//          Shift/Symbol 토글, Backspace/Enter/Space 처리
// 의존방향: 에뮬레이터가 window.ZylKeyboard.init() 호출
// SOLID: SRP — 키보드 UI 렌더링과 입력 콜백 전달만 담당
//        OCP — 레이아웃 배열 교체로 확장 가능
//        DIP — 에뮬레이터가 콜백 주입, 키보드는 구현에 비의존
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

window.ZylKeyboard = (function () {
  'use strict';

  var _onKey = null;
  var _container = null;
  var _shifted = false;
  var _capsLock = false;
  var _symbols = false;

  /* ─── Layout Data ─── */

  var LAYOUT_LOWER = [
    ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
    ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l'],
    ['\u21E7', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\u232B'],
    ['123', ' ', '\u23CE']
  ];

  var LAYOUT_UPPER = [
    ['Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'],
    ['A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L'],
    ['\u21E7', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '\u232B'],
    ['123', ' ', '\u23CE']
  ];

  var LAYOUT_SYMBOLS = [
    ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
    ['-', '/', ':', ';', '(', ')', '$', '&', '@', '"'],
    ['#+=', '.', ',', '?', '!', "'", '\u232B'],
    ['ABC', ' ', '\u23CE']
  ];

  /* ─── Internal Helpers ─── */

  function getCurrentLayout() {
    if (_symbols) return LAYOUT_SYMBOLS;
    if (_shifted || _capsLock) return LAYOUT_UPPER;
    return LAYOUT_LOWER;
  }

  function isSpecialKey(label) {
    return label === '\u21E7' || label === '\u232B' || label === '\u23CE' ||
           label === '123' || label === 'ABC' || label === '#+=';
  }

  function resolveKey(label) {
    if (label === '\u232B') return 'Backspace';
    if (label === '\u23CE') return 'Enter';
    if (label === ' ') return ' ';
    return label;
  }

  function createKeyElement(label) {
    var btn = document.createElement('button');
    btn.className = 'kb-key';
    btn.setAttribute('type', 'button');
    btn.textContent = label;

    if (label === ' ') {
      btn.className += ' kb-space';
      btn.textContent = '';
    } else if (isSpecialKey(label)) {
      btn.className += ' kb-special';
    }

    if (label === '\u21E7' && (_shifted || _capsLock)) {
      btn.className += ' kb-active';
    }

    btn.addEventListener('touchstart', function (e) {
      e.preventDefault();
      handleKey(label);
    }, { passive: false });

    btn.addEventListener('mousedown', function (e) {
      e.preventDefault();
      handleKey(label);
    });

    return btn;
  }

  function handleKey(label) {
    if (label === '\u21E7') {
      _shifted = !_shifted;
      render();
      return;
    }

    if (label === '123') {
      _symbols = true;
      _shifted = false;
      render();
      return;
    }

    if (label === 'ABC' || label === '#+=') {
      _symbols = false;
      _shifted = false;
      render();
      return;
    }

    var key = resolveKey(label);

    if (_onKey) {
      _onKey(key);
    }

    /* Auto-reset shift after one character (not for caps lock) */
    if (_shifted && !_capsLock && key.length === 1) {
      _shifted = false;
      render();
    }
  }

  /* ─── Rendering ─── */

  function render() {
    if (!_container) return;

    _container.innerHTML = '';
    var layout = getCurrentLayout();

    for (var r = 0; r < layout.length; r++) {
      var rowEl = document.createElement('div');
      rowEl.className = 'kb-row';

      var row = layout[r];
      for (var c = 0; c < row.length; c++) {
        rowEl.appendChild(createKeyElement(row[c]));
      }

      _container.appendChild(rowEl);
    }
  }

  /* ─── Public API ─── */

  function init(container, onKeyCallback) {
    _container = container;
    _onKey = onKeyCallback;
    _shifted = false;
    _capsLock = false;
    _symbols = false;
    render();
  }

  function show() {
    if (_container) {
      _container.classList.add('visible');
    }
  }

  function hide() {
    if (_container) {
      _container.classList.remove('visible');
    }
  }

  function isVisible() {
    return _container && _container.classList.contains('visible');
  }

  function setOnKey(callback) {
    _onKey = callback;
  }

  return {
    init: init,
    show: show,
    hide: hide,
    isVisible: isVisible,
    render: render,
    setOnKey: setOnKey
  };
})();
