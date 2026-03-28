// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Service
//
// 역할: 통합 국제화(i18n) 서비스 — 다국어 번역 및 DOM 자동 번역
// 수행범위: ko/en/ja/zh/es 로케일 지원, 파라미터 치환, 날짜 포맷, data-i18n 속성 번역
// 의존방향: 없음 (다른 앱 모듈이 이 모듈에 의존)
// SOLID: SRP — 국제화 로직만 담당
// ──────────────────────────────────────────────────────────

window.zylI18n = (function () {
  'use strict';

  /* ─── Translation Data (merged from home + settings) ─── */
  var translations = {
    ko: {
      /* ── Home / common ── */
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
      'date.format': '{y}\uB144 {m}\uC6D4 {d}\uC77C {day}',
      'day.0': '\uC77C\uC694\uC77C', 'day.1': '\uC6D4\uC694\uC77C', 'day.2': '\uD654\uC694\uC77C',
      'day.3': '\uC218\uC694\uC77C', 'day.4': '\uBAA9\uC694\uC77C', 'day.5': '\uAE08\uC694\uC77C',
      'day.6': '\uD1A0\uC694\uC77C',
      /* ── Settings ── */
      'settings.title': '\uC124\uC815',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': '\uBE14\uB8E8\uD22C\uC2A4',
      'settings.display': '\uB514\uC2A4\uD50C\uB808\uC774',
      'settings.sound': '\uC0AC\uC6B4\uB4DC',
      'settings.language': '\uC5B8\uC5B4',
      'settings.wallpaper': '\uBC30\uACBD\uD654\uBA74',
      'settings.security': '\uBCF4\uC548',
      'settings.storage': '\uC800\uC7A5\uACF5\uAC04',
      'settings.about': '\uC774 \uAE30\uAE30 \uC815\uBCF4',
      'settings.connected': '\uC5F0\uACB0\uB428',
      'settings.on': '\uCF1C\uC9D0',
      'settings.off': '\uAEBC\uC9D0',
      'settings.brightness': '\uBC1D\uAE30',
      'settings.dark_mode': '\uB2E4\uD06C \uBAA8\uB4DC',
      'settings.auto_brightness': '\uC790\uB3D9 \uBC1D\uAE30',
      'settings.font_size': '\uAE00\uAF34 \uD06C\uAE30',
      'settings.device_name': '\uAE30\uAE30 \uC774\uB984',
      'settings.os_version': 'OS \uBC84\uC804',
      'settings.kernel': '\uCEE4\uB110',
      'settings.build': '\uBE4C\uB4DC',
      /* ── Lockscreen ── */
      'lock.swipe': '\uC704\uB85C \uC2A4\uC640\uC774\uD504\uD558\uC5EC \uC7A0\uAE08 \uD574\uC81C',
      'lock.enter_pin': 'PIN\uC744 \uC785\uB825\uD558\uC138\uC694',
      'lock.wrong_pin': '\uC798\uBABB\uB41C PIN\uC785\uB2C8\uB2E4',
      'lock.cancel': '\uCDE8\uC18C',
      /* ── Statusbar / Quick Settings ── */
      'qs.wifi': 'WiFi',
      'qs.bluetooth': 'BT',
      'qs.silent': '\uBB34\uC74C',
      'qs.rotation': '\uD68C\uC804',
      'qs.flashlight': '\uC190\uC804\uB4F1',
      'notif.empty': '\uC54C\uB9BC\uC774 \uC5C6\uC2B5\uB2C8\uB2E4',
    },
    en: {
      /* ── Home / common ── */
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
      'date.format': '{day}, {monthName} {d}, {y}',
      'day.0': 'Sunday', 'day.1': 'Monday', 'day.2': 'Tuesday',
      'day.3': 'Wednesday', 'day.4': 'Thursday', 'day.5': 'Friday',
      'day.6': 'Saturday',
      'month.1': 'January', 'month.2': 'February', 'month.3': 'March',
      'month.4': 'April', 'month.5': 'May', 'month.6': 'June',
      'month.7': 'July', 'month.8': 'August', 'month.9': 'September',
      'month.10': 'October', 'month.11': 'November', 'month.12': 'December',
      /* ── Settings ── */
      'settings.title': 'Settings',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': 'Bluetooth',
      'settings.display': 'Display',
      'settings.sound': 'Sound',
      'settings.language': 'Language',
      'settings.wallpaper': 'Wallpaper',
      'settings.security': 'Security',
      'settings.storage': 'Storage',
      'settings.about': 'About This Device',
      'settings.connected': 'Connected',
      'settings.on': 'On',
      'settings.off': 'Off',
      'settings.brightness': 'Brightness',
      'settings.dark_mode': 'Dark Mode',
      'settings.auto_brightness': 'Auto Brightness',
      'settings.font_size': 'Font Size',
      'settings.device_name': 'Device Name',
      'settings.os_version': 'OS Version',
      'settings.kernel': 'Kernel',
      'settings.build': 'Build',
      /* ── Lockscreen ── */
      'lock.swipe': 'Swipe up to unlock',
      'lock.enter_pin': 'Enter PIN',
      'lock.wrong_pin': 'Wrong PIN',
      'lock.cancel': 'Cancel',
      /* ── Statusbar / Quick Settings ── */
      'qs.wifi': 'WiFi',
      'qs.bluetooth': 'BT',
      'qs.silent': 'Silent',
      'qs.rotation': 'Rotation',
      'qs.flashlight': 'Flashlight',
      'notif.empty': 'No notifications',
    },
    ja: {
      /* ── Home / common ── */
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
      'date.format': '{y}\u5E74{m}\u6708{d}\u65E5 {day}',
      'day.0': '\u65E5\u66DC\u65E5', 'day.1': '\u6708\u66DC\u65E5', 'day.2': '\u706B\u66DC\u65E5',
      'day.3': '\u6C34\u66DC\u65E5', 'day.4': '\u6728\u66DC\u65E5', 'day.5': '\u91D1\u66DC\u65E5',
      'day.6': '\u571F\u66DC\u65E5',
      /* ── Settings ── */
      'settings.title': '\u8A2D\u5B9A',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': 'Bluetooth',
      'settings.display': '\u30C7\u30A3\u30B9\u30D7\u30EC\u30A4',
      'settings.sound': '\u30B5\u30A6\u30F3\u30C9',
      'settings.language': '\u8A00\u8A9E',
      'settings.wallpaper': '\u58C1\u7D19',
      'settings.security': '\u30BB\u30AD\u30E5\u30EA\u30C6\u30A3',
      'settings.storage': '\u30B9\u30C8\u30EC\u30FC\u30B8',
      'settings.about': '\u3053\u306E\u30C7\u30D0\u30A4\u30B9\u306B\u3064\u3044\u3066',
      'settings.connected': '\u63A5\u7D9A\u6E08\u307F',
      'settings.on': '\u30AA\u30F3',
      'settings.off': '\u30AA\u30D5',
      'settings.brightness': '\u660E\u308B\u3055',
      'settings.dark_mode': '\u30C0\u30FC\u30AF\u30E2\u30FC\u30C9',
      'settings.auto_brightness': '\u81EA\u52D5\u660E\u308B\u3055',
      'settings.font_size': '\u30D5\u30A9\u30F3\u30C8\u30B5\u30A4\u30BA',
      'settings.device_name': '\u30C7\u30D0\u30A4\u30B9\u540D',
      'settings.os_version': 'OS\u30D0\u30FC\u30B8\u30E7\u30F3',
      'settings.kernel': '\u30AB\u30FC\u30CD\u30EB',
      'settings.build': '\u30D3\u30EB\u30C9',
      /* ── Lockscreen ── */
      'lock.swipe': '\u4E0A\u306B\u30B9\u30EF\u30A4\u30D7\u3057\u3066\u30ED\u30C3\u30AF\u89E3\u9664',
      'lock.enter_pin': 'PIN\u3092\u5165\u529B',
      'lock.wrong_pin': 'PIN\u304C\u9055\u3044\u307E\u3059',
      'lock.cancel': '\u30AD\u30E3\u30F3\u30BB\u30EB',
      /* ── Statusbar / Quick Settings ── */
      'qs.wifi': 'WiFi',
      'qs.bluetooth': 'BT',
      'qs.silent': '\u30DE\u30CA\u30FC',
      'qs.rotation': '\u56DE\u8EE2',
      'qs.flashlight': '\u30E9\u30A4\u30C8',
      'notif.empty': '\u901A\u77E5\u306F\u3042\u308A\u307E\u305B\u3093',
    },
    zh: {
      /* ── Home / common ── */
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
      'date.format': '{y}\u5E74{m}\u6708{d}\u65E5 {day}',
      'day.0': '\u661F\u671F\u65E5', 'day.1': '\u661F\u671F\u4E00', 'day.2': '\u661F\u671F\u4E8C',
      'day.3': '\u661F\u671F\u4E09', 'day.4': '\u661F\u671F\u56DB', 'day.5': '\u661F\u671F\u4E94',
      'day.6': '\u661F\u671F\u516D',
      /* ── Settings ── */
      'settings.title': '\u8BBE\u7F6E',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': '\u84DD\u7259',
      'settings.display': '\u663E\u793A',
      'settings.sound': '\u58F0\u97F3',
      'settings.language': '\u8BED\u8A00',
      'settings.wallpaper': '\u58C1\u7EB8',
      'settings.security': '\u5B89\u5168',
      'settings.storage': '\u5B58\u50A8',
      'settings.about': '\u5173\u4E8E\u672C\u673A',
      'settings.connected': '\u5DF2\u8FDE\u63A5',
      'settings.on': '\u5F00',
      'settings.off': '\u5173',
      'settings.brightness': '\u4EAE\u5EA6',
      'settings.dark_mode': '\u6DF1\u8272\u6A21\u5F0F',
      'settings.auto_brightness': '\u81EA\u52A8\u4EAE\u5EA6',
      'settings.font_size': '\u5B57\u4F53\u5927\u5C0F',
      'settings.device_name': '\u8BBE\u5907\u540D\u79F0',
      'settings.os_version': '\u7CFB\u7EDF\u7248\u672C',
      'settings.kernel': '\u5185\u6838',
      'settings.build': '\u7248\u672C\u53F7',
      /* ── Lockscreen ── */
      'lock.swipe': '\u5411\u4E0A\u6ED1\u52A8\u89E3\u9501',
      'lock.enter_pin': '\u8F93\u5165PIN',
      'lock.wrong_pin': 'PIN\u9519\u8BEF',
      'lock.cancel': '\u53D6\u6D88',
      /* ── Statusbar / Quick Settings ── */
      'qs.wifi': 'WiFi',
      'qs.bluetooth': '\u84DD\u7259',
      'qs.silent': '\u9759\u97F3',
      'qs.rotation': '\u65CB\u8F6C',
      'qs.flashlight': '\u624B\u7535\u7B52',
      'notif.empty': '\u6CA1\u6709\u901A\u77E5',
    },
    es: {
      /* ── Home / common ── */
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
      'date.format': '{day}, {d} de {monthName} de {y}',
      'day.0': 'Domingo', 'day.1': 'Lunes', 'day.2': 'Martes',
      'day.3': 'Mi\u00E9rcoles', 'day.4': 'Jueves', 'day.5': 'Viernes',
      'day.6': 'S\u00E1bado',
      'month.1': 'enero', 'month.2': 'febrero', 'month.3': 'marzo',
      'month.4': 'abril', 'month.5': 'mayo', 'month.6': 'junio',
      'month.7': 'julio', 'month.8': 'agosto', 'month.9': 'septiembre',
      'month.10': 'octubre', 'month.11': 'noviembre', 'month.12': 'diciembre',
      /* ── Settings ── */
      'settings.title': 'Ajustes',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': 'Bluetooth',
      'settings.display': 'Pantalla',
      'settings.sound': 'Sonido',
      'settings.language': 'Idioma',
      'settings.wallpaper': 'Fondo de pantalla',
      'settings.security': 'Seguridad',
      'settings.storage': 'Almacenamiento',
      'settings.about': 'Acerca del dispositivo',
      'settings.connected': 'Conectado',
      'settings.on': 'Activado',
      'settings.off': 'Desactivado',
      'settings.brightness': 'Brillo',
      'settings.dark_mode': 'Modo oscuro',
      'settings.auto_brightness': 'Brillo autom\u00E1tico',
      'settings.font_size': 'Tama\u00F1o de fuente',
      'settings.device_name': 'Nombre del dispositivo',
      'settings.os_version': 'Versi\u00F3n del SO',
      'settings.kernel': 'Kernel',
      'settings.build': 'Compilaci\u00F3n',
      /* ── Lockscreen ── */
      'lock.swipe': 'Desliza hacia arriba para desbloquear',
      'lock.enter_pin': 'Ingrese PIN',
      'lock.wrong_pin': 'PIN incorrecto',
      'lock.cancel': 'Cancelar',
      /* ── Statusbar / Quick Settings ── */
      'qs.wifi': 'WiFi',
      'qs.bluetooth': 'BT',
      'qs.silent': 'Silencio',
      'qs.rotation': 'Rotaci\u00F3n',
      'qs.flashlight': 'Linterna',
      'notif.empty': 'Sin notificaciones',
    },
  };

  var currentLocale = 'ko';
  var fallbackLocale = 'en';
  var listeners = [];

  /* ─── Locale Detection ─── */
  function detectLocale() {
    var lang = (navigator.language || navigator.userLanguage || 'ko').split('-')[0];
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
  function addTranslations(locale, keys) {
    if (!translations[locale]) translations[locale] = {};
    Object.keys(keys).forEach(function (k) {
      translations[locale][k] = keys[k];
    });
  }

  /* ─── Initialization ─── */
  currentLocale = detectLocale();

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', applyTranslations);
  } else {
    applyTranslations();
  }

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
