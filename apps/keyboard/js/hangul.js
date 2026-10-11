// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Input Method Engine
//
// Role: Hangul 2-beolsik composition automaton — turns a stream of jamo
//       keystrokes into precomposed syllables (ㅎ ㅏ ㄴ → 한)
// Scope: initial/medial/final composition, compound vowels and finals,
//        final-consonant carry-over to the next syllable (ㅎ ㅏ ㄴ ㅏ → 하나),
//        jamo-by-jamo backspace decomposition, commit
// Dependency Direction: Domain -> none (pure state machine, no DOM, no IPC)
// SOLID: SRP — composition rules only; delivering text is the keyboard's job
//        OCP — layouts feed jamo in, any frontend can consume the output
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

var ZylHangul = (function () {
  'use strict';

  /* Unicode precomposed syllable = 0xAC00 + (cho * 21 + jung) * 28 + jong */
  var SYLLABLE_BASE = 0xAC00;

  /* Compatibility jamo, in the order Unicode uses for composition */
  var CHO  = ['ㄱ','ㄲ','ㄴ','ㄷ','ㄸ','ㄹ','ㅁ',
              'ㅂ','ㅃ','ㅅ','ㅆ','ㅇ','ㅈ','ㅉ',
              'ㅊ','ㅋ','ㅌ','ㅍ','ㅎ'];
  var JUNG = ['ㅏ','ㅐ','ㅑ','ㅒ','ㅓ','ㅔ','ㅕ',
              'ㅖ','ㅗ','ㅘ','ㅙ','ㅚ','ㅛ','ㅜ',
              'ㅝ','ㅞ','ㅟ','ㅠ','ㅡ','ㅢ','ㅣ'];
  /* Index 0 is "no final" */
  var JONG = ['',      'ㄱ','ㄲ','ㄳ','ㄴ','ㄵ','ㄶ',
              'ㄷ','ㄹ','ㄺ','ㄻ','ㄼ','ㄽ','ㄾ',
              'ㄿ','ㅀ','ㅁ','ㅂ','ㅄ','ㅅ','ㅆ',
              'ㅇ','ㅈ','ㅊ','ㅋ','ㅌ','ㅍ','ㅎ'];

  /* Two vowels typed in a row that merge into one: ㅗ+ㅏ=ㅘ ... ㅡ+ㅣ=ㅢ */
  var COMPOUND_JUNG = {
    'ㅗㅏ': 'ㅘ', 'ㅗㅐ': 'ㅙ', 'ㅗㅣ': 'ㅚ',
    'ㅜㅓ': 'ㅝ', 'ㅜㅔ': 'ㅞ', 'ㅜㅣ': 'ㅟ',
    'ㅡㅣ': 'ㅢ'
  };

  /* Two consonants typed as a final that merge: ㄱ+ㅅ=ㄳ ... ㅂ+ㅅ=ㅄ */
  var COMPOUND_JONG = {
    'ㄱㅅ': 'ㄳ', 'ㄴㅈ': 'ㄵ', 'ㄴㅎ': 'ㄶ',
    'ㄹㄱ': 'ㄺ', 'ㄹㅁ': 'ㄻ', 'ㄹㅂ': 'ㄼ',
    'ㄹㅅ': 'ㄽ', 'ㄹㅌ': 'ㄾ', 'ㄹㅍ': 'ㄿ',
    'ㄹㅎ': 'ㅀ', 'ㅂㅅ': 'ㅄ'
  };

  function isConsonant(ch) { return CHO.indexOf(ch) !== -1; }
  function isVowel(ch) { return JUNG.indexOf(ch) !== -1; }
  function isJamo(ch) { return isConsonant(ch) || isVowel(ch); }
  /* ㄸ ㅃ ㅉ exist as initials but never as finals */
  function canBeFinal(ch) { return JONG.indexOf(ch) > 0; }

  /**
   * One composition session. Keystrokes go in; each call returns the edit to
   * apply at the cursor:
   *   expect  — the composing text this edit assumes is just before the caret
   *             ('' when nothing was composing). The receiver checks it, so a
   *             field cleared or rewritten behind the keyboard's back is not
   *             corrupted by a stale syllable.
   *   text    — what replaces expect (finished syllables + the new composing one)
   *   compose — the part of text still being composed
   *   replace — expect.length (0 or 1), kept for simple consumers
   */
  function Composer() {
    this.cho = null;        /* initial consonant */
    this.jung = null;       /* medial vowel */
    this.jong = null;       /* final consonant */
    this.jungParts = null;  /* [first, second] when jung was built from two keys */
    this.jongParts = null;  /* [first, second] when jong was built from two keys */
  }

  Composer.prototype.display = function () {
    if (this.cho && this.jung) {
      var c = CHO.indexOf(this.cho);
      var j = JUNG.indexOf(this.jung);
      var f = this.jong ? JONG.indexOf(this.jong) : 0;
      return String.fromCharCode(SYLLABLE_BASE + (c * 21 + j) * 28 + f);
    }
    return this.cho || this.jung || '';
  };

  Composer.prototype.isComposing = function () {
    return this.display() !== '';
  };

  Composer.prototype.clear = function () {
    this.cho = this.jung = this.jong = null;
    this.jungParts = this.jongParts = null;
  };

  /** Finish the current syllable. It is already on screen, so nothing to emit. */
  Composer.prototype.commit = function () {
    this.clear();
  };

  /**
   * Feed one jamo.
   * @param {string} ch a compatibility jamo (ㄱ..ㅎ, ㅏ..ㅣ)
   * @returns {{replace:number, text:string}|null} null if ch is not jamo
   */
  Composer.prototype.feed = function (ch) {
    if (!isJamo(ch)) return null;

    var before = this.display();
    var committed = '';
    var merged;

    if (isConsonant(ch)) {
      if (this.cho && this.jung && !this.jong && canBeFinal(ch)) {
        this.jong = ch;                                   /* 하 + ㄴ → 한 */
      } else if (this.cho && this.jung && this.jong && !this.jongParts &&
                 (merged = COMPOUND_JONG[this.jong + ch])) {
        this.jongParts = [this.jong, ch];                 /* 갈 + ㄱ → 갉 */
        this.jong = merged;
      } else if (!before) {
        this.cho = ch;                                    /* start a syllable */
      } else {
        committed = before;                               /* 한 + ㄷ → 한ㄷ */
        this.clear();
        this.cho = ch;
      }
    } else {
      if (this.cho && this.jung && this.jong) {
        /* The final consonant belongs to the next syllable: 한 + ㅏ → 하나.
           A compound final splits, keeping its first half: 갉 + ㅏ → 갈가 */
        var moving;
        if (this.jongParts) {
          moving = this.jongParts[1];
          this.jong = this.jongParts[0];
          this.jongParts = null;
        } else {
          moving = this.jong;
          this.jong = null;
        }
        committed = this.display();
        this.clear();
        this.cho = moving;
        this.jung = ch;
      } else if (this.jung && !this.jungParts &&
                 (merged = COMPOUND_JUNG[this.jung + ch])) {
        this.jungParts = [this.jung, ch];                 /* 고 + ㅏ → 과 */
        this.jung = merged;
      } else if (this.cho && !this.jung) {
        this.jung = ch;                                   /* ㅎ + ㅏ → 하 */
      } else if (!before) {
        this.jung = ch;                                   /* lone vowel */
      } else {
        committed = before;                               /* 하 + ㅓ → 하ㅓ */
        this.clear();
        this.jung = ch;
      }
    }

    var now = this.display();
    return { replace: before ? 1 : 0, expect: before, text: committed + now, compose: now };
  };

  /**
   * Remove the last jamo of the composing syllable: 한 → 하 → ㅎ → (empty).
   * A compound is undone one key at a time: 과 → 고, 갉 → 갈.
   * @returns {{replace:number, text:string}|null} null when nothing is
   *          composing — the caller should then send an ordinary Backspace
   */
  Composer.prototype.backspace = function () {
    if (!this.isComposing()) return null;
    var before = this.display();

    if (this.jong) {
      if (this.jongParts) { this.jong = this.jongParts[0]; this.jongParts = null; }
      else this.jong = null;
    } else if (this.jung) {
      if (this.jungParts) { this.jung = this.jungParts[0]; this.jungParts = null; }
      else this.jung = null;
    } else {
      this.cho = null;
    }
    var now = this.display();
    return { replace: 1, expect: before, text: now, compose: now };
  };

  return {
    isJamo: isJamo,
    create: function () { return new Composer(); }
  };
})();

if (typeof window !== 'undefined') { window.ZylHangul = ZylHangul; }
if (typeof module !== 'undefined' && module.exports) { module.exports = ZylHangul; }
