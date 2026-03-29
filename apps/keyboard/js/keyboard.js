// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - UI Component
//
// 역할: Zyl OS 가상 키보드 — 다국어 레이아웃 지원 (en/ko/es/ja/zh)
// 수행범위: 다국어 QWERTY/한글/스페인어 레이아웃 렌더링,
//          키 입력 콜백 전달, Shift/Symbol 토글, 언어 순환,
//          키 높이 설정, 터치 사운드/진동 피드백
// 의존방향: 에뮬레이터가 window.ZylKeyboard.init() 호출
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
  var _shifted = false;
  var _capsLock = false;
  var _symbols = false;
  var _currentLang = 'en';
  var _enabledLangs = ['en'];
  var _keyHeight = 36;
  var _soundEnabled = true;
  var _vibrationEnabled = true;

  /* ─── Audio Context (lazy) ─── */

  var _audioCtx = null;

  function getAudioContext() {
    if (!_audioCtx) {
      var Ctor = window.AudioContext || window.webkitAudioContext;
      if (Ctor) {
        _audioCtx = new Ctor();
      }
    }
    return _audioCtx;
  }

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

  /* ─── Feedback ─── */

  function playClickSound() {
    var ctx = getAudioContext();
    if (!ctx) return;
    try {
      var osc = ctx.createOscillator();
      var gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.value = 600;
      gain.gain.value = 0.08;
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(ctx.currentTime);
      osc.stop(ctx.currentTime + 0.05);
    } catch (e) {
      /* silent fail on audio error */
    }
  }

  function triggerFeedback() {
    if (_soundEnabled) {
      playClickSound();
    }
    if (_vibrationEnabled && navigator.vibrate) {
      navigator.vibrate(10);
    }
  }

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

  /* ─── Key Handler ─── */

  function handleKey(label) {
    triggerFeedback();

    /* Globe key — cycle language */
    if (label === '\uD83C\uDF10') {
      cycleLanguage();
      return;
    }

    /* Shift toggle */
    if (label === '\u21E7') {
      _shifted = !_shifted;
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

  /* ─── Sound / Vibration Setters ─── */

  function setSoundEnabled(v) {
    _soundEnabled = !!v;
  }

  function setVibrationEnabled(v) {
    _vibrationEnabled = !!v;
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
    render: render
  };
})();
