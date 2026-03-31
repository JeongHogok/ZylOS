// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - UI Component
//
// 역할: Zyl OS 가상 키보드 — 다국어 레이아웃 지원 (en/ko/es/ja/zh),
//       bigram 기반 예측 변환 후보 바 포함
// 수행범위: 다국어 QWERTY/한글/스페인어 레이아웃 렌더링,
//          키 입력 콜백 전달, Shift/Symbol 토글, 언어 순환,
//          키 높이 설정, 터치 사운드/진동 피드백,
//          예측 변환 후보 바 렌더링 (ZylPrediction 모듈 연동)
// 의존방향: 에뮬레이터가 window.ZylKeyboard.init() 호출,
//           window.ZylPrediction (prediction.js, optional)
// SOLID: SRP — 키보드 UI 렌더링과 입력 콜백 전달만 담당
//        OCP — LAYOUTS 객체에 언어 추가로 확장 가능
//        DIP — 에뮬레이터가 콜백 주입, 키보드는 구현에 비의존
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

window.ZylKeyboard = (function () {
  'use strict';

  /* ─── State ─── */

  var _onKey = null;
  var _container = null;
  var _candidateBar = null;   /* 예측 후보 바 엘리먼트 */
  var _shifted = false;
  var _capsLock = false;
  var _symbols = false;
  var _currentLang = 'en';
  var _enabledLangs = ['en'];
  var _keyHeight = 36;
  var _soundEnabled = true;
  var _vibrationEnabled = true;

  /* 현재 입력 필드 텍스트 추적 (예측 변환용) */
  var _currentFieldText = '';

  /* ─── Audio Context (lazy) ─── */

  /* Audio/vibration feedback is handled by the compositor via audio service.
     Keyboard does NOT directly access AudioContext or navigator.vibrate.
     See emulator.js keyboard init callback → audio.playKeyClick service. */

  /* ─── Language Labels ─── */

  var LANG_LABELS = {
    en: 'EN',
    ko: '\uD55C',
    ja: '\u3042',
    zh: '\u4E2D',
    es: 'ES'
  };

  /* ─── Layout Data ─── */

  var LAYOUTS = {
    en: {
      lower: [
        ['q','w','e','r','t','y','u','i','o','p'],
        ['a','s','d','f','g','h','j','k','l'],
        ['\u21E7','z','x','c','v','b','n','m','\u232B'],
        ['\uD83C\uDF10','123',' ','\u23CE']
      ],
      upper: [
        ['Q','W','E','R','T','Y','U','I','O','P'],
        ['A','S','D','F','G','H','J','K','L'],
        ['\u21E7','Z','X','C','V','B','N','M','\u232B'],
        ['\uD83C\uDF10','123',' ','\u23CE']
      ],
      symbols: [
        ['1','2','3','4','5','6','7','8','9','0'],
        ['-','/',':',';','(',')','\u0024','&','@','"'],
        ['#+=','.',',','?','!','\'','\u232B'],
        ['\uD83C\uDF10','ABC',' ','\u23CE']
      ]
    },
    ko: {
      lower: [
        ['\u3142','\u3148','\u3137','\u3131','\u3145','\u315B','\u3155','\u3151','\u3150','\u3154'],
        ['\u3141','\u3134','\u3147','\u3139','\u314E','\u3157','\u3153','\u314F','\u3163'],
        ['\u21E7','\u314B','\u314C','\u314A','\u314D','\u3160','\u315C','\u3161','\u232B'],
        ['\uD83C\uDF10','123',' ','\u23CE']
      ],
      upper: [
        ['\u3143','\u3149','\u3138','\u3132','\u3146','\u315B','\u3155','\u3151','\u3152','\u3156'],
        ['\u3141','\u3134','\u3147','\u3139','\u314E','\u3157','\u3153','\u314F','\u3163'],
        ['\u21E7','\u314B','\u314C','\u314A','\u314D','\u3160','\u315C','\u3161','\u232B'],
        ['\uD83C\uDF10','123',' ','\u23CE']
      ],
      symbols: [
        ['1','2','3','4','5','6','7','8','9','0'],
        ['-','/',':',';','(',')','\u0024','&','@','"'],
        ['#+=','.',',','?','!','\'','\u232B'],
        ['\uD83C\uDF10','ABC',' ','\u23CE']
      ]
    },
    es: {
      lower: [
        ['q','w','e','r','t','y','u','i','o','p'],
        ['a','s','d','f','g','h','j','k','l','\u00F1'],
        ['\u21E7','z','x','c','v','b','n','m','\u232B'],
        ['\uD83C\uDF10','123',' ','\u23CE']
      ],
      upper: [
        ['Q','W','E','R','T','Y','U','I','O','P'],
        ['A','S','D','F','G','H','J','K','L','\u00D1'],
        ['\u21E7','Z','X','C','V','B','N','M','\u232B'],
        ['\uD83C\uDF10','123',' ','\u23CE']
      ],
      symbols: [
        ['1','2','3','4','5','6','7','8','9','0'],
        ['-','/',':',';','(',')','\u00BF','\u00A1','@','"'],
        ['#+=','.',',','?','!','\'','\u232B'],
        ['\uD83C\uDF10','ABC',' ','\u23CE']
      ]
    }
  };

  /* ja and zh use en layout (romaji / pinyin input) */
  LAYOUTS.ja = LAYOUTS.en;
  LAYOUTS.zh = LAYOUTS.en;

  /* ─── Caps Lock — double-tap shift ─── */
  var _lastShiftTime = 0;
  var DOUBLE_TAP_MS = 400;

  /* ─── Long-press accent map ─── */
  var ACCENT_MAP = {
    'a': ['\u00E0', '\u00E1', '\u00E2', '\u00E4', '\u00E3', '\u00E5', '\u00E6'],
    'e': ['\u00E8', '\u00E9', '\u00EA', '\u00EB'],
    'i': ['\u00EC', '\u00ED', '\u00EE', '\u00EF'],
    'o': ['\u00F2', '\u00F3', '\u00F4', '\u00F6', '\u00F5', '\u00F8'],
    'u': ['\u00F9', '\u00FA', '\u00FB', '\u00FC'],
    'n': ['\u00F1'],
    's': ['\u00DF'],
    'c': ['\u00E7'],
    'y': ['\u00FD', '\u00FF']
  };

  var _longPressTimer = null;
  var _accentPopup = null;
  var LONG_PRESS_MS = 500;

  function showAccentPopup(btn, baseKey) {
    var lower = baseKey.toLowerCase();
    var accents = ACCENT_MAP[lower];
    if (!accents || accents.length === 0) return;

    dismissAccentPopup();

    var popup = document.createElement('div');
    popup.className = 'kb-accent-popup';

    var shifted = (_shifted || _capsLock);
    for (var i = 0; i < accents.length; i++) {
      (function (ch) {
        var charToShow = shifted ? ch.toUpperCase() : ch;
        var ab = document.createElement('button');
        ab.className = 'kb-accent-key';
        ab.setAttribute('type', 'button');
        ab.textContent = charToShow;
        ab.addEventListener('touchstart', function (ev) {
          ev.preventDefault();
          dismissAccentPopup();
          if (_onKey) _onKey(charToShow);
          if (_shifted && !_capsLock) { _shifted = false; render(); }
        }, { passive: false });
        ab.addEventListener('mousedown', function (ev) {
          ev.preventDefault();
          dismissAccentPopup();
          if (_onKey) _onKey(charToShow);
          if (_shifted && !_capsLock) { _shifted = false; render(); }
        });
        popup.appendChild(ab);
      })(accents[i]);
    }

    var rect = btn.getBoundingClientRect();
    popup.style.left = Math.max(4, rect.left - 10) + 'px';
    popup.style.bottom = (window.innerHeight - rect.top + 4) + 'px';

    _container.appendChild(popup);
    _accentPopup = popup;
  }

  function dismissAccentPopup() {
    if (_accentPopup) {
      _accentPopup.remove();
      _accentPopup = null;
    }
    if (_longPressTimer) {
      clearTimeout(_longPressTimer);
      _longPressTimer = null;
    }
  }

  /* ─── Key press popup (visual feedback) ─── */
  var _keyPopup = null;

  function showKeyPopup(btn, label) {
    dismissKeyPopup();
    if (isSpecialKey(label) || label === ' ') return;

    var popup = document.createElement('div');
    popup.className = 'kb-key-popup';
    popup.textContent = label;

    var rect = btn.getBoundingClientRect();
    var containerRect = _container.getBoundingClientRect();
    popup.style.left = (rect.left - containerRect.left + rect.width / 2 - 20) + 'px';
    popup.style.bottom = (containerRect.bottom - rect.top + 4) + 'px';

    _container.appendChild(popup);
    _keyPopup = popup;

    setTimeout(dismissKeyPopup, 300);
  }

  function dismissKeyPopup() {
    if (_keyPopup) {
      _keyPopup.remove();
      _keyPopup = null;
    }
  }

  /* ─── Internal Helpers ─── */

  function getCurrentLayout() {
    var lang = LAYOUTS[_currentLang] || LAYOUTS.en;
    if (_symbols) return lang.symbols;
    if (_shifted || _capsLock) return lang.upper;
    return lang.lower;
  }

  function isSpecialKey(label) {
    return label === '\u21E7' || label === '\u232B' || label === '\u23CE' ||
           label === '123' || label === 'ABC' || label === '#+=' ||
           label === '\uD83C\uDF10';
  }

  function resolveKey(label) {
    if (label === '\u232B') return 'Backspace';
    if (label === '\u23CE') return 'Enter';
    if (label === ' ') return ' ';
    return label;
  }

  /* Feedback is delegated to compositor → audio service.
     No direct hardware access from keyboard app. */

  /* ─── Language ─── */

  function setLanguage(lang) {
    if (!LAYOUTS[lang]) return;
    _currentLang = lang;
    _shifted = false;
    _capsLock = false;
    _symbols = false;
    render();
  }

  function getLanguage() {
    return _currentLang;
  }

  function setEnabledLanguages(langs) {
    if (!langs || !langs.length) return;
    _enabledLangs = langs;
    /* If current language is not in enabled list, switch to first */
    if (_enabledLangs.indexOf(_currentLang) === -1) {
      setLanguage(_enabledLangs[0]);
    }
  }

  function cycleLanguage() {
    if (_enabledLangs.length <= 1) return;
    var idx = _enabledLangs.indexOf(_currentLang);
    var next = (idx + 1) % _enabledLangs.length;
    setLanguage(_enabledLangs[next]);
  }

  /* ─── Key Height ─── */

  function setKeyHeight(h) {
    _keyHeight = h;
    if (_container) {
      _container.style.setProperty('--kb-key-height', h + 'px');
    }
  }

  /* ─── Key Element Factory ─── */

  function createKeyElement(label) {
    var btn = document.createElement('button');
    btn.className = 'kb-key';
    btn.setAttribute('type', 'button');
    btn.textContent = label;

    /* Globe key */
    if (label === '\uD83C\uDF10') {
      btn.className += ' kb-special kb-lang';
    /* Space bar — show language label */
    } else if (label === ' ') {
      btn.className += ' kb-space';
      btn.textContent = LANG_LABELS[_currentLang] || '';
    /* Other special keys */
    } else if (isSpecialKey(label)) {
      btn.className += ' kb-special';
    }

    /* Shift active indicator */
    if (label === '\u21E7') {
      if (_capsLock) {
        btn.className += ' kb-active kb-capslock';
      } else if (_shifted) {
        btn.className += ' kb-active';
      }
    }

    /* Long-press for accents */
    function onPressStart(e) {
      e.preventDefault();
      showKeyPopup(btn, label);
      dismissAccentPopup();
      if (!isSpecialKey(label) && label !== ' ' && !_symbols) {
        var lower = label.toLowerCase();
        if (ACCENT_MAP[lower]) {
          _longPressTimer = setTimeout(function () {
            showAccentPopup(btn, label);
          }, LONG_PRESS_MS);
        }
      }
      handleKey(label);
    }

    function onPressEnd() {
      if (_longPressTimer) {
        clearTimeout(_longPressTimer);
        _longPressTimer = null;
      }
    }

    btn.addEventListener('touchstart', onPressStart, { passive: false });
    btn.addEventListener('touchend', onPressEnd, { passive: true });
    btn.addEventListener('mousedown', onPressStart);
    btn.addEventListener('mouseup', onPressEnd);

    return btn;
  }

  /* ─── Key Handler ─── */

  function handleKey(label) {
    /* Feedback handled by compositor via audio service — not here */

    /* Globe key — cycle language */
    if (label === '\uD83C\uDF10') {
      cycleLanguage();
      return;
    }

    /* Shift toggle / Caps lock (double-tap) */
    if (label === '\u21E7') {
      var now = Date.now();
      if (_shifted && (now - _lastShiftTime) < DOUBLE_TAP_MS) {
        /* Double-tap → caps lock */
        _capsLock = true;
        _shifted = true;
      } else if (_capsLock) {
        /* Turn off caps lock */
        _capsLock = false;
        _shifted = false;
      } else {
        _shifted = !_shifted;
      }
      _lastShiftTime = now;
      render();
      return;
    }

    /* Switch to symbols */
    if (label === '123') {
      _symbols = true;
      _shifted = false;
      render();
      return;
    }

    /* Switch back to letters */
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

    /* 입력 텍스트 추적 (예측 변환용) */
    if (key === 'Backspace') {
      if (_currentFieldText.length > 0) {
        _currentFieldText = _currentFieldText.slice(0, -1);
      }
    } else if (key === 'Enter') {
      _currentFieldText = '';
      if (window.ZylPrediction) window.ZylPrediction.reset();
    } else if (key.length === 1) {
      _currentFieldText += key;
    }
    updatePredictions();

    /* Auto-reset shift after one character (not for caps lock) */
    if (_shifted && !_capsLock && key.length === 1) {
      _shifted = false;
      render();
    }
  }

  /* ─── 예측 후보 바 ─── */

  function renderCandidateBar(candidates) {
    if (!_candidateBar) return;
    _candidateBar.innerHTML = '';

    if (!candidates || candidates.length === 0) {
      _candidateBar.classList.remove('visible');
      return;
    }

    _candidateBar.classList.add('visible');
    for (var i = 0; i < candidates.length && i < 3; i++) {
      (function (word) {
        var btn = document.createElement('button');
        btn.className = 'kb-candidate';
        btn.setAttribute('type', 'button');
        btn.textContent = word;

        btn.addEventListener('touchstart', function (e) {
          e.preventDefault();
          handleCandidateSelect(word);
        }, { passive: false });

        btn.addEventListener('mousedown', function (e) {
          e.preventDefault();
          handleCandidateSelect(word);
        });

        _candidateBar.appendChild(btn);

        /* 구분선 (마지막 제외) */
        if (i < candidates.length - 1 && i < 2) {
          var sep = document.createElement('span');
          sep.className = 'kb-candidate-sep';
          _candidateBar.appendChild(sep);
        }
      })(candidates[i]);
    }
  }

  function handleCandidateSelect(word) {
    /* 현재 입력 중인 부분 단어를 선택된 후보로 교체하여 onKey로 전달 */
    if (!_onKey) return;

    /* 현재 입력 중인 prefix 길이만큼 Backspace 전송 후 단어 삽입 */
    var words = _currentFieldText ? _currentFieldText.trimRight().split(/\s+/) : [];
    var currentPrefix = words.length > 0 ? words[words.length - 1] : '';

    for (var i = 0; i < currentPrefix.length; i++) {
      _onKey('Backspace');
    }

    /* 단어 + 공백 삽입 */
    for (var j = 0; j < word.length; j++) {
      _onKey(word[j]);
    }
    _onKey(' ');

    /* 예측 컨텍스트 업데이트 */
    if (window.ZylPrediction) {
      window.ZylPrediction.onCandidateSelected(word);
    }
    _currentFieldText = _currentFieldText
      ? _currentFieldText.trimRight().split(/\s+/).slice(0, -1).join(' ') + (words.length > 1 ? ' ' : '') + word + ' '
      : word + ' ';

    updatePredictions();
  }

  function updatePredictions() {
    if (!window.ZylPrediction) return;
    window.ZylPrediction.updateContext(_currentFieldText);
    var candidates = window.ZylPrediction.getPredictions(_currentLang);
    renderCandidateBar(candidates);
  }

  /* ─── Rendering ─── */

  function render() {
    if (!_container) return;

    dismissAccentPopup();
    dismissKeyPopup();
    _container.innerHTML = '';

    /* 예측 후보 바 (최상단) */
    _candidateBar = document.createElement('div');
    _candidateBar.className = 'kb-candidate-bar';
    _container.appendChild(_candidateBar);

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

    /* 초기 예측 업데이트 */
    updatePredictions();
  }

  /* ─── Public API ─── */

  function init(container, onKeyCallback) {
    _container = container;
    _onKey = onKeyCallback;
    _shifted = false;
    _capsLock = false;
    _symbols = false;
    _container.style.setProperty('--kb-key-height', _keyHeight + 'px');
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

  function setSoundEnabled(v) { _soundEnabled = !!v; }
  function setVibrationEnabled(v) { _vibrationEnabled = !!v; }
  function getSoundEnabled() { return _soundEnabled; }
  function getVibrationEnabled() { return _vibrationEnabled; }

  return {
    init: init,
    show: show,
    hide: hide,
    isVisible: isVisible,
    setOnKey: setOnKey,
    setLanguage: setLanguage,
    getLanguage: getLanguage,
    setEnabledLanguages: setEnabledLanguages,
    setKeyHeight: setKeyHeight,
    setSoundEnabled: setSoundEnabled,
    setVibrationEnabled: setVibrationEnabled,
    getSoundEnabled: getSoundEnabled,
    getVibrationEnabled: getVibrationEnabled,
    render: render
  };
})();
