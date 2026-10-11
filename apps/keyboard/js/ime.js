// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Input Method Engine
//
// Role: Japanese and Chinese conversion input — romaji → kana as you type
//       (k a → か), pinyin kept as typed, dictionary candidates for both
//       (わたし → 私, nihao → 你好)
// Scope: romaji table (Hepburn, Kunrei and wapuro spellings, っ from doubled
//        consonants, ん rules, small kana), katakana, candidate ranking from
//        the longest dictionary match, partial conversion (the rest of the
//        reading keeps composing), on-demand dictionary loading
// Dependency Direction: Domain -> none (pure state machine + data; the
//        keyboard delivers the edits)
// SOLID: SRP — conversion rules only. OCP — a language is a reading rule
//        plus a dictionary file in ../dict
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

var ZylImeDict = (function () {
  'use strict';

  var _raw = {};        /* lang -> text from dict/<lang>.js */
  var _index = {};      /* lang -> reading -> "w1 w2 ..." built on first lookup */
  var _requested = {};
  var _listeners = [];
  /* dict/ beside js/: resolved from this script so any host page works */
  var _base = (typeof document !== 'undefined' && document.currentScript &&
               document.currentScript.src || '').replace(/js\/ime\.js(\?.*)?$/, 'dict/');

  function register(lang, text) {
    _raw[lang] = text;
    _index[lang] = null;
    for (var i = 0; i < _listeners.length; i++) _listeners[i](lang);
  }

  /* About 1 MB per language, so only fetched when that layout is chosen */
  function load(lang) {
    if (_raw[lang] || _requested[lang] || typeof document === 'undefined') return;
    _requested[lang] = true;
    var s = document.createElement('script');
    s.src = _base + lang + '.js';
    s.onerror = function () { _requested[lang] = false; };   /* retry next time */
    document.head.appendChild(s);
  }

  function index(lang) {
    if (_index[lang]) return _index[lang];
    var text = _raw[lang];
    if (!text) return null;
    var map = Object.create(null);
    var lines = text.split('\n');
    for (var i = 0; i < lines.length; i++) {
      var tab = lines[i].indexOf('\t');
      if (tab > 0) map[lines[i].slice(0, tab)] = lines[i].slice(tab + 1);
    }
    _index[lang] = map;
    return map;
  }

  function lookup(lang, reading) {
    var map = index(lang);
    var hit = map && map[reading];
    return hit ? hit.split(' ') : [];
  }

  return {
    register: register,
    load: load,
    lookup: lookup,
    isLoaded: function (lang) { return !!_raw[lang]; },
    onLoad: function (fn) { _listeners.push(fn); }
  };
})();

var ZylIme = (function () {
  'use strict';

  var MAX_CANDIDATES = 30;

  /* ─── Romaji → hiragana ─── */
  var T = {};
  var VOWELS = 'aiueo';
  var ROWS = {
    '': 'あいうえお', k: 'かきくけこ', g: 'がぎぐげご', s: 'さしすせそ',
    z: 'ざじずぜぞ', t: 'たちつてと', d: 'だぢづでど', n: 'なにぬねの',
    h: 'はひふへほ', b: 'ばびぶべぼ', p: 'ぱぴぷぺぽ', m: 'まみむめも',
    r: 'らりるれろ'
  };
  var YOUON = { k: 'き', g: 'ぎ', s: 'し', z: 'じ', t: 'ち', d: 'ぢ', n: 'に',
                h: 'ひ', b: 'び', p: 'ぴ', m: 'み', r: 'り', c: 'ち', j: 'じ', f: 'ふ' };
  var SMALL = { a: 'ぁ', i: 'ぃ', u: 'ぅ', e: 'ぇ', o: 'ぉ', ya: 'ゃ', yu: 'ゅ', yo: 'ょ',
                tu: 'っ', tsu: 'っ', wa: 'ゎ' };
  var c, i;
  for (c in ROWS) for (i = 0; i < 5; i++) T[c + VOWELS[i]] = ROWS[c][i];
  for (c in YOUON) {
    T[c + 'ya'] = YOUON[c] + 'ゃ';
    T[c + 'yu'] = YOUON[c] + 'ゅ';
    T[c + 'yo'] = YOUON[c] + 'ょ';
  }
  for (c in SMALL) { T['x' + c] = SMALL[c]; T['l' + c] = SMALL[c]; }
  var EXTRA = {
    ya: 'や', yu: 'ゆ', yo: 'よ', yi: 'い', ye: 'いぇ',
    wa: 'わ', wi: 'うぃ', wu: 'う', we: 'うぇ', wo: 'を',
    shi: 'し', sha: 'しゃ', shu: 'しゅ', she: 'しぇ', sho: 'しょ',
    chi: 'ち', cha: 'ちゃ', chu: 'ちゅ', che: 'ちぇ', cho: 'ちょ',
    tsu: 'つ', tsa: 'つぁ', tsi: 'つぃ', tse: 'つぇ', tso: 'つぉ',
    ji: 'じ', ja: 'じゃ', ju: 'じゅ', je: 'じぇ', jo: 'じょ',
    fu: 'ふ', fa: 'ふぁ', fi: 'ふぃ', fe: 'ふぇ', fo: 'ふぉ',
    va: 'ゔぁ', vi: 'ゔぃ', vu: 'ゔ', ve: 'ゔぇ', vo: 'ゔぉ',
    thi: 'てぃ', thu: 'てゅ', dhi: 'でぃ', dhu: 'でゅ', twu: 'とぅ', dwu: 'どぅ',
    nn: 'ん', "n'": 'ん', '-': 'ー'
  };
  for (c in EXTRA) T[c] = EXTRA[c];

  var PENDING = /[a-z'\-]+$/;

  /* Converts the latin run at the end of s; what cannot convert yet ("k",
     "ky", a lone "n") stays latin until the next letter decides it */
  function toKana(s) {
    var m = PENDING.exec(s);
    if (!m) return s;
    var run = m[0], out = '', p = 0;
    while (p < run.length) {
      var hit = false;
      for (var len = 4; len > 0; len--) {
        var chunk = run.substr(p, len);
        if (chunk.length === len && T[chunk]) { out += T[chunk]; p += len; hit = true; break; }
      }
      if (hit) continue;
      var ch = run.charAt(p), next = run.charAt(p + 1);
      if (ch === 'n' && next && "aiueoyn'".indexOf(next) === -1) {
        out += 'ん'; p++;                               /* n before a consonant */
      } else if (next && (ch === next || (ch === 't' && next === 'c')) &&
                 VOWELS.indexOf(ch) === -1 && /[a-z]/.test(ch)) {
        out += 'っ'; p++;                               /* kk → っk, tch → っch */
      } else {
        break;
      }
    }
    return s.slice(0, s.length - run.length) + out + run.slice(p);
  }

  function toKatakana(s) {
    var out = '';
    for (var k = 0; k < s.length; k++) {
      var code = s.charCodeAt(k);
      out += (code >= 0x3041 && code <= 0x3096) ? String.fromCharCode(code + 0x60) : s.charAt(k);
    }
    return out;
  }

  /* A trailing n means ん once the reading is finished */
  function finishKana(s) {
    return /n$/.test(s) ? s.slice(0, -1) + 'ん' : s;
  }

  /* ─── Composer ─── */
  function create(lang) {
    var reading = '';          /* exactly what is on screen for the composition */

    function edit(before) { return { expect: before, text: reading }; }

    function candidates() {
      var key = lang === 'ja' ? finishKana(reading).replace(PENDING, '') : reading;
      var out = [], seen = {};
      function add(text, consume) {
        if (text && !seen[text] && out.length < MAX_CANDIDATES) {
          seen[text] = true;
          out.push({ text: text, consume: consume });
        }
      }
      var placedPlain = false;
      for (var len = key.length; len > 0; len--) {
        var words = ZylImeDict.lookup(lang, key.slice(0, len));
        for (var w = 0; w < words.length; w++) add(words[w], len);
        /* The reading itself comes right after the best (longest) match, so
           space converts and the as-typed form stays one tap away */
        if (!placedPlain && (words.length || len === 1)) {
          placedPlain = true;
          if (lang === 'ja') {
            add(key, key.length);
            add(toKatakana(key), key.length);
          }
        }
      }
      if (lang !== 'ja') add(reading, reading.length);
      return out;
    }

    return {
      isComposing: function () { return reading.length > 0; },
      reading: function () { return reading; },
      /* letter: a-z, plus '-' (ー) for Japanese */
      feed: function (letter) {
        var before = reading;
        reading = lang === 'ja' ? toKana(reading + letter) : reading + letter;
        return edit(before);
      },
      backspace: function () {
        var before = reading;
        reading = reading.slice(0, -1);
        return edit(before);
      },
      candidates: candidates,
      /* Converts the part of the reading the candidate covers; the rest keeps
         composing (わたしは → 私 + は) */
      choose: function (cand) {
        var before = reading;
        var rest = reading.slice(cand.consume);
        reading = rest;
        return { expect: before, text: cand.text + rest };
      },
      /* Enter: keep the reading as typed (a trailing n becomes ん) */
      finish: function () {
        var before = reading;
        var text = lang === 'ja' ? finishKana(reading) : reading;
        reading = '';
        return { expect: before, text: text };
      },
      /* Outside interaction: what is on screen simply stays */
      commit: function () { reading = ''; }
    };
  }

  function accepts(lang, key) {
    return /^[a-z]$/.test(key) || (lang === 'ja' && key === '-');
  }

  return {
    LANGS: ['ja', 'zh'],
    create: create,
    accepts: accepts,
    toKana: toKana,
    toKatakana: toKatakana
  };
})();

if (typeof window !== 'undefined') { window.ZylImeDict = ZylImeDict; window.ZylIme = ZylIme; }
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { ZylIme: ZylIme, ZylImeDict: ZylImeDict };
}
