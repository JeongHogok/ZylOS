// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - UI Component
//
// 역할: Zyl OS 가상 키보드 — 다국어 레이아웃 지원 (en/ko/es/ja/zh), 한글 조합(ZylHangul),
//       일본어·중국어 변환(ZylIme), bigram 기반 예측 변환 후보 바 포함
// 수행범위: 다국어 QWERTY/한글/스페인어 레이아웃 렌더링,
//          키 입력 콜백 전달, Shift/Symbol 토글, 언어 순환,
//          키 높이 설정, 터치 사운드/진동 피드백,
//          예측 변환 후보 바 렌더링 (ZylPrediction 모듈 연동)
// 의존방향: js/inpage.js 가 앱 문서 안에서 window.ZylKeyboard.init() 호출,
//           window.ZylPrediction (prediction.js, optional), ZylIme (ime.js, optional)
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

  /* Hangul composition session — only used while the Korean layout is active */
  var _hangul = (typeof ZylHangul !== 'undefined') ? ZylHangul.create() : null;

  /* Japanese / Chinese conversion session — only for those layouts */
  var _ime = null;
  function imeFor(lang) {
    if (typeof ZylIme === 'undefined' || ZylIme.LANGS.indexOf(lang) === -1) return null;
    ZylImeDict.load(lang);
    return ZylIme.create(lang);
  }
  /* Candidates appear as soon as the dictionary arrives mid-word */
  if (typeof ZylImeDict !== 'undefined') ZylImeDict.onLoad(function () { updatePredictions(); });

  /* Composer to switch to if the receiver could not apply the last edit as
     planned (see adoptFallback). One keystroke ahead at most. */
  var _pendingFallback = null;

  /**
   * Deliver one atomic edit. One keystroke must produce one edit — Backspace
   * plus characters sent separately would click several times and fire
   * several input events, so a search box would briefly see a wrong query.
   *
   * The receiver (apps/shared/bridge.js) applies "text" in place of "expect"
   * only if "expect" is really just before the caret. Otherwise the field was
   * changed behind the keyboard's back — the app cleared it after "send",
   * say — and it deletes "fallbackDelete" characters and inserts "fallback"
   * instead, then tells us so we can adopt the matching composer state
   * (fallbackComposer: { hangul: composer } or { ime: composer }).
   */
  function emitEdit(edit, fallbackDelete, fallback, fallbackComposer) {
    _pendingFallback = fallbackComposer || null;
    if (_onKey) {
      _onKey(null, {
        expect: edit.expect, text: edit.text,
        fallbackDelete: fallbackDelete, fallback: fallback
      });
    }
    _currentFieldText = _currentFieldText.slice(0,
      _currentFieldText.length - edit.expect.length) + edit.text;
  }

  /**
   * End the syllable being composed. It is already on screen, so nothing is
   * sent. Called on non-jamo keys, language switch, hide, and by the
   * compositor when the user touches the app (moving the caret, tapping a
   * button) — real input methods commit on any outside interaction.
   */
  function commitComposition() {
    if (_hangul) _hangul.commit();
    if (_ime) _ime.commit();
    _pendingFallback = null;
  }

  /**
   * The receiver applied the fallback of the last edit: make the composer
   * match what is now in the field, so the next jamo composes onto it.
   */
  function adoptFallback() {
    if (_pendingFallback && _pendingFallback.hangul) _hangul = _pendingFallback.hangul;
    if (_pendingFallback && _pendingFallback.ime) _ime = _pendingFallback.ime;
    _pendingFallback = null;
    _currentFieldText = '';
  }

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
    commitComposition();
    _currentLang = lang;
    _ime = imeFor(lang);
    /* Key caps follow the typing language, not the host page: a kana or hanzi
       key drawn with Korean glyph shapes reads as the wrong script. */
    if (_container) _container.setAttribute('lang', lang);
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
  /* FIX: Per-key event listeners removed — replaced by container-level event delegation
   *       in _attachDelegatedListeners(). createKeyElement now only builds DOM structure.
   *       label stored in data-label so delegation can identify it without textContent ambiguity. */

  function createKeyElement(label) {
    var btn = document.createElement('button');
    btn.className = 'kb-key';
    btn.setAttribute('type', 'button');
    /* Store original label for event delegation lookup */
    btn.dataset.label = label;

    /* Globe key */
    if (label === '\uD83C\uDF10') {
      btn.className += ' kb-special kb-lang';
      btn.textContent = label;
    /* Space bar — show language label */
    } else if (label === ' ') {
      btn.className += ' kb-space';
      btn.textContent = LANG_LABELS[_currentLang] || '';
    /* Other special keys */
    } else if (isSpecialKey(label)) {
      btn.className += ' kb-special';
      btn.textContent = label;
    } else {
      btn.textContent = label;
    }

    /* Shift active indicator */
    if (label === '\u21E7') {
      if (_capsLock) {
        btn.className += ' kb-active kb-capslock';
      } else if (_shifted) {
        btn.className += ' kb-active';
      }
    }

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

    /* Korean: jamo go through the composition automaton (ㅎ ㅏ ㄴ → 한) */
    if (_currentLang === 'ko' && !_symbols && _hangul && ZylHangul.isJamo(key)) {
      var edit = _hangul.feed(key);
      /* If the field moved on without us, this key starts a fresh syllable */
      var fresh = ZylHangul.create();
      var freshEdit = fresh.feed(key);
      emitEdit(edit, 0, freshEdit.text, { hangul: fresh });
      updatePredictions();
      if (_shifted && !_capsLock) {
        _shifted = false;
        render();
      }
      return;
    }

    /* Backspace while composing peels one jamo off the syllable: 한 → 하 */
    if (key === 'Backspace' && _hangul && _hangul.isComposing()) {
      var back = _hangul.backspace();
      /* If the composing syllable is gone, this is an ordinary Backspace */
      emitEdit(back, 1, '', { hangul: ZylHangul.create() });
      updatePredictions();
      return;
    }

    /* Japanese / Chinese: letters build the reading (k a → か, n i → ni) */
    /* '-' lives on the symbols page and is the long vowel ー in Japanese */
    if (_ime && (!_symbols || key === '-') && ZylIme.accepts(_currentLang, key)) {
      var imeEdit = _ime.feed(key);
      var freshIme = ZylIme.create(_currentLang);
      emitEdit(imeEdit, 0, freshIme.feed(key).text, { ime: freshIme });
      updatePredictions();
      return;
    }

    if (_ime && _ime.isComposing()) {
      if (key === 'Backspace') {
        emitEdit(_ime.backspace(), 1, '', { ime: ZylIme.create(_currentLang) });
        updatePredictions();
        return;
      }
      /* Space converts with the best candidate; Enter keeps the reading as typed */
      if (key === ' ' || key === 'Enter') {
        var best = key === ' ' ? _ime.candidates()[0] : null;
        emitEdit(best ? _ime.choose(best) : _ime.finish(), 0, '', null);
        updatePredictions();
        return;
      }
    }

    /* Any other key (space, enter, punctuation, digits) ends the syllable */
    commitComposition();

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

  /* onSelect given: conversion candidates — all of them, scrolling sideways.
     Otherwise the three word predictions. */
  function renderCandidateBar(candidates, onSelect) {
    if (!_candidateBar) return;
    _candidateBar.innerHTML = '';
    _candidateBar.classList.toggle('kb-candidate-bar--scroll', !!onSelect);
    _candidateBar.scrollLeft = 0;

    if (!candidates || candidates.length === 0) {
      _candidateBar.classList.remove('visible');
      return;
    }

    _candidateBar.classList.add('visible');
    var limit = onSelect ? candidates.length : 3;
    for (var i = 0; i < candidates.length && i < limit; i++) {
      (function (word, index) {
        var select = onSelect ? function () { onSelect(index); } : function () { handleCandidateSelect(word); };
        var btn = document.createElement('button');
        btn.className = 'kb-candidate';
        btn.setAttribute('type', 'button');
        btn.textContent = word;

        btn.addEventListener('touchstart', function (e) {
          e.preventDefault();
          select();
        }, { passive: false });

        btn.addEventListener('mousedown', function (e) {
          e.preventDefault();
          select();
        });

        _candidateBar.appendChild(btn);

        /* 구분선 (마지막 제외) */
        if (i < candidates.length - 1 && i < limit - 1) {
          var sep = document.createElement('span');
          sep.className = 'kb-candidate-sep';
          _candidateBar.appendChild(sep);
        }
      })(candidates[i], i);
    }
  }

  function handleCandidateSelect(word) {
    /* 현재 입력 중인 부분 단어를 선택된 후보로 교체하여 onKey로 전달 */
    if (!_onKey) return;

    /* 현재 입력 중인 prefix 를 선택된 후보 + 공백으로 한 번에 교체.
       글자 수만큼 Backspace 를 연타하면 키음과 input 이벤트가 그만큼 반복된다. */
    var words = _currentFieldText ? _currentFieldText.trimRight().split(/\s+/) : [];
    var currentPrefix = words.length > 0 ? words[words.length - 1] : '';

    /* The composing syllable is part of the prefix and is replaced with it.
       If the prefix is no longer before the caret, insert without deleting —
       never erase characters we cannot account for. */
    commitComposition();
    _onKey(null, {
      expect: currentPrefix, text: word + ' ',
      fallbackDelete: 0, fallback: word + ' '
    });

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
    if (_ime && _ime.isComposing()) {
      var cands = _ime.candidates();
      renderCandidateBar(cands.map(function (c) { return c.text; }), function (index) {
        emitEdit(_ime.choose(cands[index]), 0, cands[index].text, null);
        updatePredictions();
      });
      return;
    }
    if (!window.ZylPrediction) return;
    window.ZylPrediction.updateContext(_currentFieldText);
    var candidates = window.ZylPrediction.getPredictions(_currentLang);
    renderCandidateBar(candidates);
  }

  /* ─── Rendering ─── */

  /* FIX: Attach container-level delegated listeners once at init to avoid
   *       re-registering per-key handlers on every render() call.
   *       Each render() still reconstructs DOM (needed for layout change),
   *       but event handling is routed through the stable container listeners. */
  var _delegateAttached = false;

  function _attachDelegatedListeners() {
    if (_delegateAttached || !_container) return;
    _delegateAttached = true;

    _container.addEventListener('touchstart', function (e) {
      var btn = e.target.closest && e.target.closest('button.kb-key');
      if (!btn) return;
      e.preventDefault();
      var label = btn.dataset.label !== undefined ? btn.dataset.label : btn.textContent;
      showKeyPopup(btn, label);
      dismissAccentPopup();
      if (!isSpecialKey(label) && label !== ' ' && !_symbols) {
        var lower = label.toLowerCase();
        if (ACCENT_MAP[lower]) {
          _longPressTimer = setTimeout(function () { showAccentPopup(btn, label); }, LONG_PRESS_MS);
        }
      }
      handleKey(label);
      /* Forward feedback request to compositor via audio service */
      if (_soundEnabled || _vibrationEnabled) {
        if (typeof ZylBridge !== 'undefined') {
          ZylBridge.sendToSystem({
            type: 'service.request', service: 'audio', method: 'playKeyClick',
            params: { sound: _soundEnabled, vibration: _vibrationEnabled }
          });
        }
      }
    }, { passive: false });

    _container.addEventListener('touchend', function (e) {
      var btn = e.target.closest && e.target.closest('button.kb-key');
      if (btn && _longPressTimer) { clearTimeout(_longPressTimer); _longPressTimer = null; }
    }, { passive: true });

    _container.addEventListener('mousedown', function (e) {
      var btn = e.target.closest && e.target.closest('button.kb-key');
      if (!btn) return;
      var label = btn.dataset.label !== undefined ? btn.dataset.label : btn.textContent;
      showKeyPopup(btn, label);
      dismissAccentPopup();
      if (!isSpecialKey(label) && label !== ' ' && !_symbols) {
        var lower = label.toLowerCase();
        if (ACCENT_MAP[lower]) {
          _longPressTimer = setTimeout(function () { showAccentPopup(btn, label); }, LONG_PRESS_MS);
        }
      }
      handleKey(label);
    });

    _container.addEventListener('mouseup', function (e) {
      var btn = e.target.closest && e.target.closest('button.kb-key');
      if (btn && _longPressTimer) { clearTimeout(_longPressTimer); _longPressTimer = null; }
    });
  }

  function render() {
    if (!_container) return;

    dismissAccentPopup();
    dismissKeyPopup();
    _container.innerHTML = '';

    /* Attach delegated listeners once (idempotent) */
    _attachDelegatedListeners();

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
    _container.setAttribute('lang', _currentLang);
    render();
  }

  function show() {
    if (_container) {
      _container.classList.add('visible');
    }
  }

  function hide() {
    commitComposition();
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

  function isComposing() {
    return !!((_hangul && _hangul.isComposing()) || (_ime && _ime.isComposing()));
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
    isComposing: isComposing,
    commitComposition: commitComposition,
    adoptFallback: adoptFallback,
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
