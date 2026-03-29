// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Service
//
// Role: Emulator compositor i18n — translates system UI rendered by the emulator
// Scope: QS panel labels, notification panel, time-ago formatting, data-emu-i18n DOM binding
// Dependency: None (standalone, receives locale from OS via postMessage)
// SOLID: SRP — compositor i18n only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// The emulator provides a real device runtime environment and must not contain OS image content
// ──────────────────────────────────────────────────────────

window.ZylEmuI18n = (function () {
  'use strict';

  var translations = {
    ko: {
      'qs.wifi': 'Wi-Fi',
      'qs.bt': 'BT',
      'qs.airplane': '\uBE44\uD589\uAE30',
      'qs.silent': '\uBB34\uC74C',
      'qs.rotate': '\uD68C\uC804',
      'qs.flashlight': '\uC190\uC804\uB4F1',
      'qs.notifications': '\uC54C\uB9BC',
      'qs.no_notifications': '\uC54C\uB9BC\uC774 \uC5C6\uC2B5\uB2C8\uB2E4',
      'qs.clear_all': '\uBAA8\uB450 \uC9C0\uC6B0\uAE30',
      'time.now': '\uBC29\uAE08',
      'time.minutes_ago': '{n}\uBD84 \uC804',
      'time.hours_ago': '{n}\uC2DC\uAC04 \uC804',
      'time.days_ago': '{n}\uC77C \uC804',
      'recents.title': '\uCD5C\uADFC \uC571',
      'recents.empty': '\uC2E4\uD589 \uC911\uC778 \uC571 \uC5C6\uC74C',
    },
    en: {
      'qs.wifi': 'Wi-Fi',
      'qs.bt': 'BT',
      'qs.airplane': 'Airplane',
      'qs.silent': 'Silent',
      'qs.rotate': 'Rotate',
      'qs.flashlight': 'Light',
      'qs.notifications': 'Notifications',
      'qs.no_notifications': 'No notifications',
      'qs.clear_all': 'Clear all',
      'time.now': 'now',
      'time.minutes_ago': '{n}m ago',
      'time.hours_ago': '{n}h ago',
      'time.days_ago': '{n}d ago',
      'recents.title': 'Recent Apps',
      'recents.empty': 'No running apps',
    },
    ja: {
      'qs.wifi': 'Wi-Fi',
      'qs.bt': 'BT',
      'qs.airplane': '\u6A5F\u5185\u30E2\u30FC\u30C9',
      'qs.silent': '\u30DE\u30CA\u30FC',
      'qs.rotate': '\u56DE\u8EE2',
      'qs.flashlight': '\u30E9\u30A4\u30C8',
      'qs.notifications': '\u901A\u77E5',
      'qs.no_notifications': '\u901A\u77E5\u306F\u3042\u308A\u307E\u305B\u3093',
      'qs.clear_all': '\u3059\u3079\u3066\u524A\u9664',
      'time.now': '\u305F\u3063\u305F\u4ECA',
      'time.minutes_ago': '{n}\u5206\u524D',
      'time.hours_ago': '{n}\u6642\u9593\u524D',
      'time.days_ago': '{n}\u65E5\u524D',
      'recents.title': '\u6700\u8FD1\u306E\u30A2\u30D7\u30EA',
      'recents.empty': '\u5B9F\u884C\u4E2D\u306E\u30A2\u30D7\u30EA\u306A\u3057',
    },
    zh: {
      'qs.wifi': 'Wi-Fi',
      'qs.bt': 'BT',
      'qs.airplane': '\u98DE\u884C\u6A21\u5F0F',
      'qs.silent': '\u9759\u97F3',
      'qs.rotate': '\u65CB\u8F6C',
      'qs.flashlight': '\u624B\u7535\u7B52',
      'qs.notifications': '\u901A\u77E5',
      'qs.no_notifications': '\u6CA1\u6709\u901A\u77E5',
      'qs.clear_all': '\u5168\u90E8\u6E05\u9664',
      'time.now': '\u521A\u521A',
      'time.minutes_ago': '{n}\u5206\u949F\u524D',
      'time.hours_ago': '{n}\u5C0F\u65F6\u524D',
      'time.days_ago': '{n}\u5929\u524D',
      'recents.title': '\u6700\u8FD1\u4F7F\u7528',
      'recents.empty': '\u6CA1\u6709\u8FD0\u884C\u4E2D\u7684\u5E94\u7528',
    },
    es: {
      'qs.wifi': 'Wi-Fi',
      'qs.bt': 'BT',
      'qs.airplane': 'Avi\u00F3n',
      'qs.silent': 'Silencio',
      'qs.rotate': 'Rotaci\u00F3n',
      'qs.flashlight': 'Linterna',
      'qs.notifications': 'Notificaciones',
      'qs.no_notifications': 'Sin notificaciones',
      'qs.clear_all': 'Borrar todo',
      'time.now': 'ahora',
      'time.minutes_ago': 'hace {n}min',
      'time.hours_ago': 'hace {n}h',
      'time.days_ago': 'hace {n}d',
      'recents.title': 'Apps recientes',
      'recents.empty': 'Sin apps en ejecuci\u00F3n',
    },
  };

  var currentLocale = 'en';
  var fallbackLocale = 'en';
  var listeners = [];

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

  function formatTimeAgo(ts) {
    var diff = Math.floor((Date.now() - ts) / 1000);
    if (diff < 60) return t('time.now');
    if (diff < 3600) return t('time.minutes_ago', { n: Math.floor(diff / 60) });
    if (diff < 86400) return t('time.hours_ago', { n: Math.floor(diff / 3600) });
    return t('time.days_ago', { n: Math.floor(diff / 86400) });
  }

  function applyTranslations() {
    document.querySelectorAll('[data-emu-i18n]').forEach(function (el) {
      el.textContent = t(el.getAttribute('data-emu-i18n'));
    });
  }

  function setLocale(locale) {
    if (!translations[locale]) locale = fallbackLocale;
    currentLocale = locale;
    applyTranslations();
    listeners.forEach(function (fn) { fn(locale); });
  }

  function getLocale() {
    return currentLocale;
  }

  function onLocaleChange(fn) {
    listeners.push(fn);
    return function () {
      listeners = listeners.filter(function (f) { return f !== fn; });
    };
  }

  return {
    t: t,
    formatTimeAgo: formatTimeAgo,
    applyTranslations: applyTranslations,
    setLocale: setLocale,
    getLocale: getLocale,
    onLocaleChange: onLocaleChange,
  };
})();
