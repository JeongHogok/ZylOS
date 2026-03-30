// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Service
//
// 역할: 통합 국제화(i18n) 엔진 — 다국어 번역 및 DOM 자동 번역
// 수행범위: ko/en/ja/zh/es 로케일 지원, 파라미터 치환, 날짜 포맷, data-i18n 속성 번역
//          앱별 키는 각 앱의 i18n.js에서 addTranslations으로 등록
// 의존방향: 없음 (다른 앱 모듈이 이 모듈에 의존)
// SOLID: SRP — 국제화 로직만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

window.zylI18n = (function () {
  'use strict';

  /* ─── Common OS-level Translation Data ─── */
  var translations = {
    ko: {
      'search': '\uAC80\uC0C9...',
      'app.browser': '\uBE0C\uB77C\uC6B0\uC800',
      'app.files': '\uD30C\uC77C',
      'app.terminal': '\uD130\uBBF8\uB110',
      'app.settings': '\uC124\uC815',
      'app.camera': '\uCE74\uBA54\uB77C',
      'app.gallery': '\uAC24\uB7EC\uB9AC',
      'app.music': '\uC74C\uC545',
      'app.clock': '\uC2DC\uACC4',
      'app.calc': '\uACC4\uC0B0\uAE30',
      'app.notes': '\uBA54\uBAA8',
      'app.weather': '\uB0A0\uC528',
      'app.store': '\uC571\uC2A4\uD1A0\uC5B4',
      'app.phone': '\uC804\uD654',
      'app.messages': '\uBB38\uC790',
      'app.contacts': '\uC5F0\uB77D\uCC98',
      'date.format': '{y}\uB144 {m}\uC6D4 {d}\uC77C {day}',
      'day.0': '\uC77C\uC694\uC77C', 'day.1': '\uC6D4\uC694\uC77C', 'day.2': '\uD654\uC694\uC77C',
      'day.3': '\uC218\uC694\uC77C', 'day.4': '\uBAA9\uC694\uC77C', 'day.5': '\uAE08\uC694\uC77C',
      'day.6': '\uD1A0\uC694\uC77C'
    },
    en: {
      'search': 'Search...',
      'app.browser': 'Browser',
      'app.files': 'Files',
      'app.terminal': 'Terminal',
      'app.settings': 'Settings',
      'app.camera': 'Camera',
      'app.gallery': 'Gallery',
      'app.music': 'Music',
      'app.clock': 'Clock',
      'app.calc': 'Calculator',
      'app.notes': 'Notes',
      'app.weather': 'Weather',
      'app.store': 'App Store',
      'app.phone': 'Phone',
      'app.messages': 'Messages',
      'app.contacts': 'Contacts',
      'date.format': '{day}, {monthName} {d}, {y}',
      'day.0': 'Sunday', 'day.1': 'Monday', 'day.2': 'Tuesday',
      'day.3': 'Wednesday', 'day.4': 'Thursday', 'day.5': 'Friday',
      'day.6': 'Saturday',
      'month.1': 'January', 'month.2': 'February', 'month.3': 'March',
      'month.4': 'April', 'month.5': 'May', 'month.6': 'June',
      'month.7': 'July', 'month.8': 'August', 'month.9': 'September',
      'month.10': 'October', 'month.11': 'November', 'month.12': 'December'
    },
    ja: {
      'search': '\u691C\u7D22...',
      'app.browser': '\u30D6\u30E9\u30A6\u30B6',
      'app.files': '\u30D5\u30A1\u30A4\u30EB',
      'app.terminal': '\u30BF\u30FC\u30DF\u30CA\u30EB',
      'app.settings': '\u8A2D\u5B9A',
      'app.camera': '\u30AB\u30E1\u30E9',
      'app.gallery': '\u30AE\u30E3\u30E9\u30EA\u30FC',
      'app.music': '\u30DF\u30E5\u30FC\u30B8\u30C3\u30AF',
      'app.clock': '\u6642\u8A08',
      'app.calc': '\u8A08\u7B97\u6A5F',
      'app.notes': '\u30E1\u30E2',
      'app.weather': '\u5929\u6C17',
      'app.store': '\u30A2\u30D7\u30EA\u30B9\u30C8\u30A2',
      'app.phone': '\u96FB\u8A71',
      'app.messages': '\u30E1\u30C3\u30BB\u30FC\u30B8',
      'app.contacts': '\u9023\u7D61\u5148',
      'date.format': '{y}\u5E74{m}\u6708{d}\u65E5 {day}',
      'day.0': '\u65E5\u66DC\u65E5', 'day.1': '\u6708\u66DC\u65E5', 'day.2': '\u706B\u66DC\u65E5',
      'day.3': '\u6C34\u66DC\u65E5', 'day.4': '\u6728\u66DC\u65E5', 'day.5': '\u91D1\u66DC\u65E5',
      'day.6': '\u571F\u66DC\u65E5'
    },
    zh: {
      'search': '\u641C\u7D22...',
      'app.browser': '\u6D4F\u89C8\u5668',
      'app.files': '\u6587\u4EF6',
      'app.terminal': '\u7EC8\u7AEF',
      'app.settings': '\u8BBE\u7F6E',
      'app.camera': '\u76F8\u673A',
      'app.gallery': '\u76F8\u518C',
      'app.music': '\u97F3\u4E50',
      'app.clock': '\u65F6\u949F',
      'app.calc': '\u8BA1\u7B97\u5668',
      'app.notes': '\u5907\u5FD8\u5F55',
      'app.weather': '\u5929\u6C14',
      'app.store': '\u5E94\u7528\u5546\u5E97',
      'app.phone': '\u7535\u8BDD',
      'app.messages': '\u77ED\u4FE1',
      'app.contacts': '\u8054\u7CFB\u4EBA',
      'date.format': '{y}\u5E74{m}\u6708{d}\u65E5 {day}',
      'day.0': '\u661F\u671F\u65E5', 'day.1': '\u661F\u671F\u4E00', 'day.2': '\u661F\u671F\u4E8C',
      'day.3': '\u661F\u671F\u4E09', 'day.4': '\u661F\u671F\u56DB', 'day.5': '\u661F\u671F\u4E94',
      'day.6': '\u661F\u671F\u516D'
    },
    es: {
      'search': 'Buscar...',
      'app.browser': 'Navegador',
      'app.files': 'Archivos',
      'app.terminal': 'Terminal',
      'app.settings': 'Ajustes',
      'app.camera': 'C\u00E1mara',
      'app.gallery': 'Galer\u00EDa',
      'app.music': 'M\u00FAsica',
      'app.clock': 'Reloj',
      'app.calc': 'Calculadora',
      'app.notes': 'Notas',
      'app.weather': 'Clima',
      'app.store': 'App Store',
      'app.phone': 'Tel\u00E9fono',
      'app.messages': 'Mensajes',
      'app.contacts': 'Contactos',
      'date.format': '{day}, {d} de {monthName} de {y}',
      'day.0': 'Domingo', 'day.1': 'Lunes', 'day.2': 'Martes',
      'day.3': 'Mi\u00E9rcoles', 'day.4': 'Jueves', 'day.5': 'Viernes',
      'day.6': 'S\u00E1bado',
      'month.1': 'enero', 'month.2': 'febrero', 'month.3': 'marzo',
      'month.4': 'abril', 'month.5': 'mayo', 'month.6': 'junio',
      'month.7': 'julio', 'month.8': 'agosto', 'month.9': 'septiembre',
      'month.10': 'octubre', 'month.11': 'noviembre', 'month.12': 'diciembre'
    }
  };

  var currentLocale = 'ko';
  var fallbackLocale = 'en';
  var listeners = [];

  /* ─── Locale Detection ─── */
  function detectLocale() {
    var lang = (navigator.language || navigator.userLanguage || 'en').split('-')[0];
    if (translations[lang]) return lang;
    return fallbackLocale;
  }

  /* ─── Translation Lookup ─── */
  function t(key, params) {
    var dict = translations[currentLocale] || translations[fallbackLocale];
    var text = dict[key];
    if (!text) {
      var fb = translations[fallbackLocale];
      text = fb ? fb[key] : key;
    }
    if (!text) return key;

    if (params) {
      Object.keys(params).forEach(function (k) {
        text = text.replace(new RegExp('\\{' + k + '\\}', 'g'), params[k]);
      });
    }
    return text;
  }

  /* ─── Date Formatting ─── */
  function formatDate(date) {
    var y = date.getFullYear();
    var m = date.getMonth() + 1;
    var d = date.getDate();
    var dayOfWeek = date.getDay();
    var dayName = t('day.' + dayOfWeek);
    var monthName = t('month.' + m) || String(m);

    return t('date.format', {
      y: y, m: m, d: d, day: dayName, monthName: monthName,
    });
  }

  /* ─── Apply Translations to DOM ─── */
  function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(function (el) {
      el.textContent = t(el.getAttribute('data-i18n'));
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(function (el) {
      el.placeholder = t(el.getAttribute('data-i18n-placeholder'));
    });
    document.documentElement.lang = currentLocale;
  }

  /* ─── Set Locale ─── */
  function setLocale(locale) {
    if (!translations[locale]) return;
    currentLocale = locale;
    applyTranslations();
    /* notify listeners */
    listeners.forEach(function (fn) { fn(locale); });
  }

  /* ─── Get Current Locale ─── */
  function getLocale() {
    return currentLocale;
  }

  /* ─── Get Supported Locales ─── */
  function getSupportedLocales() {
    return Object.keys(translations);
  }

  /* ─── Register locale change listener ─── */
  function onLocaleChange(fn) {
    listeners.push(fn);
    return function () {
      listeners = listeners.filter(function (f) { return f !== fn; });
    };
  }

  /* ─── Register additional translation keys at runtime ─── */
  var _pendingApply = false;
  function addTranslations(locale, keys) {
    if (!translations[locale]) translations[locale] = {};
    Object.keys(keys).forEach(function (k) {
      translations[locale][k] = keys[k];
    });
    /* Auto-apply after all synchronous addTranslations calls complete */
    if (!_pendingApply) {
      _pendingApply = true;
      setTimeout(function () {
        _pendingApply = false;
        applyTranslations();
      }, 0);
    }
  }

  /* ─── Listen for locale injection from emulator (parent frame) ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;
      if (msg.type === 'system.setLocale') {
        var loc = msg.locale || (msg.data && msg.data.locale);
        if (loc) setLocale(loc);
      }
    } catch (err) { /* ignore */ }
  });

  /* ─── Initialization ─── */
  currentLocale = detectLocale();

  /* Defer applyTranslations to DOMContentLoaded so per-app i18n.js
     files have time to register their keys via addTranslations() */
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () {
      applyTranslations();
      /* Request saved locale from emulator */
      if (window.parent && window.parent !== window) {
        window.parent.postMessage(JSON.stringify({
          type: 'service.request',
          service: 'settings',
          method: 'get',
          params: { category: 'language' }
        }), '*');
      }
    });
  } else {
    applyTranslations();
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({
        type: 'service.request',
        service: 'settings',
        method: 'get',
        params: { category: 'language' }
      }), '*');
    }
  }

  /* Handle settings response for saved locale */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (msg && msg.type === 'service.response' && msg.service === 'settings' && msg.data) {
        if (msg.data.locale && translations[msg.data.locale]) {
          setLocale(msg.data.locale);
        }
      }
    } catch (err) { /* ignore */ }
  });

  return {
    t: t,
    formatDate: formatDate,
    setLocale: setLocale,
    getLocale: getLocale,
    getSupportedLocales: getSupportedLocales,
    applyTranslations: applyTranslations,
    onLocaleChange: onLocaleChange,
    addTranslations: addTranslations,
  };
})();
