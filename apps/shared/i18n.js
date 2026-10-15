// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Service
//
// 역할: 통합 국제화(i18n) 엔진 — 다국어 번역 및 DOM 자동 번역
// 수행범위: ko/en/ja/zh/es/ar/he 로케일 지원, 파라미터 치환, 날짜 포맷,
//           복수형(tp), 숫자·용량 포맷(formatNumber, formatBytes), 시간 포맷(formatTime),
//           RTL 감지(isRtl), data-i18n 속성 번역
//           앱별 키는 각 앱의 i18n.js에서 addTranslations으로 등록
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
      'intent.open_with': '\uB2E4\uC74C\uC73C\uB85C \uC5F4\uAE30',
      'intent.no_app': '\uC774\uAC83\uC744 \uC5F4 \uC218 \uC788\uB294 \uC571\uC774 \uC5C6\uC2B5\uB2C8\uB2E4',
      'intent.cancel': '\uCDE8\uC18C',
      'intent.ok': '\uD655\uC778',
      'app.browser': '\uBE0C\uB77C\uC6B0\uC800',
      'app.files': '\uD30C\uC77C',
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
      'date.lunar': '\uC74C\uB825 {date}',
      'time.just_now': '\uBC29\uAE08',
      'time.min_short': '{n}\uBD84',
      'time.hour_short': '{n}\uC2DC\uAC04',
      'time.yesterday': '\uC5B4\uC81C',
      'time.month_day': '{m}/{d}',
      'day.0': '\uC77C\uC694\uC77C', 'day.1': '\uC6D4\uC694\uC77C', 'day.2': '\uD654\uC694\uC77C',
      'day.3': '\uC218\uC694\uC77C', 'day.4': '\uBAA9\uC694\uC77C', 'day.5': '\uAE08\uC694\uC77C',
      'day.6': '\uD1A0\uC694\uC77C',
      'permission.camera': '\uCE74\uBA54\uB77C',
      'permission.location': '\uC704\uCE58',
      'permission.contacts': '\uC5F0\uB77D\uCC98',
      'permission.messaging': '\uBB38\uC790',
      'permission.telephony': '\uC804\uD654',
      'permission.storage': '\uC800\uC7A5\uACF5\uAC04',
      'permission.microphone': '\uB9C8\uC774\uD06C',
      'permission.bluetooth': '\uBE14\uB8E8\uD22C\uC2A4',
      'permission.deny': '\uAC70\uBD80',
      'permission.allow': '\uD5C8\uC6A9',
      'permission.dont_ask_again': '\uB2E4\uC2DC \uBB3B\uC9C0 \uC54A\uAE30',
      'permission.request_message': '{app}\uC774(\uAC00) {perm}\uC5D0 \uC811\uADFC\uD558\uB824\uACE0 \uD569\uB2C8\uB2E4',
      'home.loading': '\uB85C\uB529 \uC911...',
      'month.1': '1\uC6D4', 'month.2': '2\uC6D4', 'month.3': '3\uC6D4',
      'month.4': '4\uC6D4', 'month.5': '5\uC6D4', 'month.6': '6\uC6D4',
      'month.7': '7\uC6D4', 'month.8': '8\uC6D4', 'month.9': '9\uC6D4',
      'month.10': '10\uC6D4', 'month.11': '11\uC6D4', 'month.12': '12\uC6D4'
    },
    en: {
      'search': 'Search...',
      'intent.open_with': 'Open with',
      'intent.no_app': 'No app can open this',
      'intent.cancel': 'Cancel',
      'intent.ok': 'OK',
      'app.browser': 'Browser',
      'app.files': 'Files',
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
      'date.lunar': 'Lunar {date}',
      'time.just_now': 'Just now',
      'time.min_short': '{n}m',
      'time.hour_short': '{n}h',
      'time.yesterday': 'Yesterday',
      'time.month_day': '{m}/{d}',
      'day.0': 'Sunday', 'day.1': 'Monday', 'day.2': 'Tuesday',
      'day.3': 'Wednesday', 'day.4': 'Thursday', 'day.5': 'Friday',
      'day.6': 'Saturday',
      'month.1': 'January', 'month.2': 'February', 'month.3': 'March',
      'month.4': 'April', 'month.5': 'May', 'month.6': 'June',
      'month.7': 'July', 'month.8': 'August', 'month.9': 'September',
      'month.10': 'October', 'month.11': 'November', 'month.12': 'December',
      'permission.camera': 'Camera',
      'permission.location': 'Location',
      'permission.contacts': 'Contacts',
      'permission.messaging': 'Messaging',
      'permission.telephony': 'Phone',
      'permission.storage': 'Storage',
      'permission.microphone': 'Microphone',
      'permission.bluetooth': 'Bluetooth',
      'permission.deny': 'Deny',
      'permission.allow': 'Allow',
      'permission.dont_ask_again': "Don't ask again",
      'permission.request_message': '{app} wants to access {perm}',
      'home.loading': 'Loading...'
    },
    ja: {
      'search': '\u691C\u7D22...',
      'intent.open_with': '\u3053\u306E\u30A2\u30D7\u30EA\u3067\u958B\u304F',
      'intent.no_app': '\u3053\u308C\u3092\u958B\u3051\u308B\u30A2\u30D7\u30EA\u304C\u3042\u308A\u307E\u305B\u3093',
      'intent.cancel': '\u30AD\u30E3\u30F3\u30BB\u30EB',
      'intent.ok': 'OK',
      'app.browser': '\u30D6\u30E9\u30A6\u30B6',
      'app.files': '\u30D5\u30A1\u30A4\u30EB',
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
      'date.lunar': '\u65E7\u66A6 {date}',
      'time.just_now': '\u305F\u3063\u305F\u4ECA',
      'time.min_short': '{n}\u5206',
      'time.hour_short': '{n}\u6642\u9593',
      'time.yesterday': '\u6628\u65E5',
      'time.month_day': '{m}/{d}',
      'day.0': '\u65E5\u66DC\u65E5', 'day.1': '\u6708\u66DC\u65E5', 'day.2': '\u706B\u66DC\u65E5',
      'day.3': '\u6C34\u66DC\u65E5', 'day.4': '\u6728\u66DC\u65E5', 'day.5': '\u91D1\u66DC\u65E5',
      'day.6': '\u571F\u66DC\u65E5',
      'permission.camera': '\u30AB\u30E1\u30E9',
      'permission.location': '\u4F4D\u7F6E\u60C5\u5831',
      'permission.contacts': '\u9023\u7D61\u5148',
      'permission.messaging': '\u30E1\u30C3\u30BB\u30FC\u30B8',
      'permission.telephony': '\u96FB\u8A71',
      'permission.storage': '\u30B9\u30C8\u30EC\u30FC\u30B8',
      'permission.microphone': '\u30DE\u30A4\u30AF',
      'permission.bluetooth': 'Bluetooth',
      'permission.deny': '\u62D2\u5426',
      'permission.allow': '\u8A31\u53EF',
      'permission.dont_ask_again': '\u4ECA\u5F8C\u8868\u793A\u3057\u306A\u3044',
      'permission.request_message': '{app}\u304C{perm}\u3078\u306E\u30A2\u30AF\u30BB\u30B9\u3092\u6C42\u3081\u3066\u3044\u307E\u3059',
      'home.loading': '\u8AAD\u307F\u8FBC\u307F\u4E2D...',
      'month.1': '1\u6708', 'month.2': '2\u6708', 'month.3': '3\u6708',
      'month.4': '4\u6708', 'month.5': '5\u6708', 'month.6': '6\u6708',
      'month.7': '7\u6708', 'month.8': '8\u6708', 'month.9': '9\u6708',
      'month.10': '10\u6708', 'month.11': '11\u6708', 'month.12': '12\u6708'
    },
    zh: {
      'search': '\u641C\u7D22...',
      'intent.open_with': '\u6253\u5F00\u65B9\u5F0F',
      'intent.no_app': '\u6CA1\u6709\u53EF\u4EE5\u6253\u5F00\u6B64\u5185\u5BB9\u7684\u5E94\u7528',
      'intent.cancel': '\u53D6\u6D88',
      'intent.ok': '\u786E\u5B9A',
      'app.browser': '\u6D4F\u89C8\u5668',
      'app.files': '\u6587\u4EF6',
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
      'date.lunar': '\u519C\u5386{date}',
      'time.just_now': '\u521A\u521A',
      'time.min_short': '{n}\u5206\u949F',
      'time.hour_short': '{n}\u5C0F\u65F6',
      'time.yesterday': '\u6628\u5929',
      'time.month_day': '{m}/{d}',
      'day.0': '\u661F\u671F\u65E5', 'day.1': '\u661F\u671F\u4E00', 'day.2': '\u661F\u671F\u4E8C',
      'day.3': '\u661F\u671F\u4E09', 'day.4': '\u661F\u671F\u56DB', 'day.5': '\u661F\u671F\u4E94',
      'day.6': '\u661F\u671F\u516D',
      'permission.camera': '\u76F8\u673A',
      'permission.location': '\u4F4D\u7F6E',
      'permission.contacts': '\u8054\u7CFB\u4EBA',
      'permission.messaging': '\u77ED\u4FE1',
      'permission.telephony': '\u7535\u8BDD',
      'permission.storage': '\u5B58\u50A8',
      'permission.microphone': '\u9EA6\u514B\u98CE',
      'permission.bluetooth': '\u84DD\u7259',
      'permission.deny': '\u62D2\u7EDD',
      'permission.allow': '\u5141\u8BB8',
      'permission.dont_ask_again': '\u4E0D\u518D\u8BE2\u95EE',
      'permission.request_message': '{app}\u60F3\u8981\u8BBF\u95EE{perm}',
      'home.loading': '\u52A0\u8F7D\u4E2D...',
      'month.1': '\u4E00\u6708', 'month.2': '\u4E8C\u6708', 'month.3': '\u4E09\u6708',
      'month.4': '\u56DB\u6708', 'month.5': '\u4E94\u6708', 'month.6': '\u516D\u6708',
      'month.7': '\u4E03\u6708', 'month.8': '\u516B\u6708', 'month.9': '\u4E5D\u6708',
      'month.10': '\u5341\u6708', 'month.11': '\u5341\u4E00\u6708', 'month.12': '\u5341\u4E8C\u6708'
    },
    es: {
      'search': 'Buscar...',
      'intent.open_with': 'Abrir con',
      'intent.no_app': 'Ninguna app puede abrir esto',
      'intent.cancel': 'Cancelar',
      'intent.ok': 'Aceptar',
      'app.browser': 'Navegador',
      'app.files': 'Archivos',
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
      'date.lunar': 'Lunar {date}',
      'time.just_now': 'Ahora',
      'time.min_short': '{n} min',
      'time.hour_short': '{n} h',
      'time.yesterday': 'Ayer',
      'time.month_day': '{d}/{m}',
      'day.0': 'Domingo', 'day.1': 'Lunes', 'day.2': 'Martes',
      'day.3': 'Mi\u00E9rcoles', 'day.4': 'Jueves', 'day.5': 'Viernes',
      'day.6': 'S\u00E1bado',
      'month.1': 'enero', 'month.2': 'febrero', 'month.3': 'marzo',
      'month.4': 'abril', 'month.5': 'mayo', 'month.6': 'junio',
      'month.7': 'julio', 'month.8': 'agosto', 'month.9': 'septiembre',
      'month.10': 'octubre', 'month.11': 'noviembre', 'month.12': 'diciembre',
      'permission.camera': 'C\u00E1mara',
      'permission.location': 'Ubicaci\u00F3n',
      'permission.contacts': 'Contactos',
      'permission.messaging': 'Mensajes',
      'permission.telephony': 'Tel\u00E9fono',
      'permission.storage': 'Almacenamiento',
      'permission.microphone': 'Micr\u00F3fono',
      'permission.bluetooth': 'Bluetooth',
      'permission.deny': 'Denegar',
      'permission.allow': 'Permitir',
      'permission.dont_ask_again': 'No volver a preguntar',
      'permission.request_message': '{app} quiere acceder a {perm}',
      'home.loading': 'Cargando...'
    },
    ar: {
      'search': '\u0628\u062D\u062B...',
      'app.browser': '\u0627\u0644\u0645\u062A\u0635\u0641\u062D',
      'app.files': '\u0627\u0644\u0645\u0644\u0641\u0627\u062A',
      'app.settings': '\u0627\u0644\u0625\u0639\u062F\u0627\u062F\u0627\u062A',
      'app.camera': '\u0627\u0644\u0643\u0627\u0645\u064A\u0631\u0627',
      'app.gallery': '\u0627\u0644\u0645\u0639\u0631\u0636',
      'app.music': '\u0627\u0644\u0645\u0648\u0633\u064A\u0642\u0649',
      'app.clock': '\u0627\u0644\u0633\u0627\u0639\u0629',
      'app.calc': '\u0627\u0644\u0622\u0644\u0629 \u0627\u0644\u062D\u0627\u0633\u0628\u0629',
      'app.notes': '\u0627\u0644\u0645\u0644\u0627\u062D\u0638\u0627\u062A',
      'app.weather': '\u0627\u0644\u0637\u0642\u0633',
      'app.store': '\u0645\u062A\u062C\u0631 \u0627\u0644\u062A\u0637\u0628\u064A\u0642\u0627\u062A',
      'app.phone': '\u0627\u0644\u0647\u0627\u062A\u0641',
      'app.messages': '\u0627\u0644\u0631\u0633\u0627\u0626\u0644',
      'app.contacts': '\u062C\u0647\u0627\u062A \u0627\u0644\u0627\u062A\u0635\u0627\u0644',
      'date.format': '{d}/{m}/{y} {day}',
      'date.lunar': 'Lunar {date}',
      'time.just_now': '\u0627\u0644\u0622\u0646',
      'time.min_short': '{n} \u062F',
      'time.hour_short': '{n} \u0633',
      'time.yesterday': '\u0623\u0645\u0633',
      'time.month_day': '{d}/{m}',
      'day.0': '\u0627\u0644\u0623\u062D\u062F', 'day.1': '\u0627\u0644\u0627\u062B\u0646\u064A\u0646', 'day.2': '\u0627\u0644\u062B\u0644\u0627\u062B\u0627\u0621',
      'day.3': '\u0627\u0644\u0623\u0631\u0628\u0639\u0627\u0621', 'day.4': '\u0627\u0644\u062E\u0645\u064A\u0633', 'day.5': '\u0627\u0644\u062C\u0645\u0639\u0629',
      'day.6': '\u0627\u0644\u0633\u0628\u062A'
    },
    he: {
      'search': '\u05D7\u05D9\u05E4\u05D5\u05E9...',
      'app.browser': '\u05D3\u05E4\u05D3\u05E4\u05DF',
      'app.files': '\u05E7\u05D1\u05E6\u05D9\u05DD',
      'app.settings': '\u05D4\u05D2\u05D3\u05E8\u05D5\u05EA',
      'app.camera': '\u05DE\u05E6\u05DC\u05DE\u05D4',
      'date.format': '{d}/{m}/{y} {day}',
      'date.lunar': 'Lunar {date}',
      'time.just_now': '\u05E2\u05DB\u05E9\u05D9\u05D5',
      'time.min_short': '{n} \u05D3\u05F3',
      'time.hour_short': '{n} \u05E9\u05F3',
      'time.yesterday': '\u05D0\u05EA\u05DE\u05D5\u05DC',
      'time.month_day': '{d}/{m}'
    }
  };

  /* ─── RTL Language Support ─── */
  var RTL_LOCALES = { ar: true, he: true, fa: true, ur: true };

  /**
   * isRtl([locale])
   * When called with no arguments (or undefined), uses currentLocale.
   * When called with a locale string, tests that locale.
   */
  function isRtl(locale) {
    var loc = (locale !== undefined) ? locale : currentLocale;
    return !!RTL_LOCALES[loc];
  }

  function applyDirection(locale) {
    var dir = isRtl(locale) ? 'rtl' : 'ltr';
    document.documentElement.dir = dir;
    document.documentElement.setAttribute('data-dir', dir);
    /* Apply CSS class for layout adjustments */
    if (isRtl(locale)) {
      document.body.classList.add('zyl-rtl');
      document.body.classList.remove('zyl-ltr');
    } else {
      document.body.classList.add('zyl-ltr');
      document.body.classList.remove('zyl-rtl');
    }
  }

  var currentLocale = 'ko';
  var fallbackLocale = 'en';
  var listeners = [];

  /* ─── Locale Detection ─── */
  function detectLocale() {
    var lang = (navigator.language || navigator.userLanguage || 'en').split('-')[0];
    if (translations[lang]) return lang;
    return fallbackLocale;
  }

  /* ─── Plural Rules ──────────────────────────────────────────
   *
   * Plural category selection per locale:
   *   ko / ja / zh : always 'one'  (no grammatical plural in these languages)
   *   en / es      : count === 1 → 'one', otherwise → 'other'
   *   ar / he      : fallback to 'other' for count !== 1 (simplified rule)
   *   unknown      : 'other'
   *
   * Convention for translation keys:
   *   '{key}.one'   — singular form
   *   '{key}.other' — plural / generic form
   * ─────────────────────────────────────────────────────────── */

  function _pluralCategory(locale, count) {
    if (locale === 'ko' || locale === 'ja' || locale === 'zh') {
      return 'one';
    }
    if (locale === 'en' || locale === 'es') {
      return count === 1 ? 'one' : 'other';
    }
    /* ar, he, fa, ur and all other locales: simple singular/plural */
    return count === 1 ? 'one' : 'other';
  }

  /**
   * tp(key, count [, params])
   * Translate a pluralisable key.
   *   key    — base translation key; the engine appends '.one' or '.other'
   *   count  — numeric quantity that drives plural selection
   *   params — optional substitution object; {count} is always injected
   *
   * Example keys in translations:
   *   'notif.count.one'   → '알림 {count}개'   (ko — same for all counts)
   *   'notif.count.other' → '{count} notifications'
   *
   * Falls back to key+'.other', then key itself, then the key string.
   */
  function tp(key, count, params) {
    var category = _pluralCategory(currentLocale, count);
    var pluralKey = key + '.' + category;

    /* Build merged params: always inject {count} */
    var merged = { count: count };
    if (params) {
      var k;
      for (k in params) {
        if (Object.prototype.hasOwnProperty.call(params, k)) {
          merged[k] = params[k];
        }
      }
    }

    var dict = translations[currentLocale] || translations[fallbackLocale];

    /* Try locale-specific plural key */
    if (dict && dict[pluralKey]) {
      return t(pluralKey, merged);
    }

    /* Fallback: try '.other' form */
    var otherKey = key + '.other';
    if (dict && dict[otherKey]) {
      return t(otherKey, merged);
    }

    /* Last resort: base key */
    return t(key, merged);
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

  /* ─── Number Formatting ──────────────────────────────────────
   *
   * formatNumber(num)
   * Format a numeric value according to the active locale's conventions.
   *
   * Locale rules (decimal separator / thousands separator):
   *   ko, en, ja, zh, ar, he : decimal='.'  thousands=','   → 1,234.56
   *   de (future)            : decimal=','  thousands='.'   → 1.234,56
   *   es                     : decimal=','  thousands='.'   → 1.234,56
   *                            (standard Castilian, Spain variant)
   *
   * Fractional digits are preserved as-is.
   * Returns a string. Returns '' for non-finite input.
   * ─────────────────────────────────────────────────────────── */

  function formatNumber(num) {
    if (typeof num !== 'number' || !isFinite(num)) return '';

    /* Determine separators for the current locale */
    var decSep = '.';    /* decimal separator  */
    var grpSep = ',';    /* thousands separator */

    if (currentLocale === 'de' || currentLocale === 'es') {
      decSep = ',';
      grpSep = '.';
    }

    /* Split integer and fractional parts */
    var parts   = num.toString().split('.');
    var intPart = parts[0];
    var fracPart = parts.length > 1 ? parts[1] : '';

    /* Handle negative sign */
    var sign = '';
    if (intPart.charAt(0) === '-') {
      sign    = '-';
      intPart = intPart.slice(1);
    }

    /* Insert thousands separator every 3 digits (right to left) */
    var grouped = '';
    var i;
    for (i = 0; i < intPart.length; i++) {
      var pos = intPart.length - 1 - i;
      if (i > 0 && i % 3 === 0) {
        grouped = grpSep + grouped;
      }
      grouped = intPart.charAt(pos) + grouped;
    }

    var result = sign + grouped;
    if (fracPart) {
      result += decSep + fracPart;
    }
    return result;
  }

  /* formatBytes(n): decimal units (1 GB = 10^9 bytes) — what carriers bill and
     storage vendors print. One decimal from MB up, in the locale's separator. */
  function formatBytes(n) {
    n = Number(n) || 0;
    if (n < 1e6) return formatNumber(Math.round(n / 1e3)) + ' KB';
    if (n < 1e9) return formatNumber(Math.round(n / 1e5) / 10) + ' MB';
    return formatNumber(Math.round(n / 1e8) / 10) + ' GB';
  }

  /* ─── Time Formatting ────────────────────────────────────────
   *
   * formatTime(date)
   * Format a Date object as a locale-aware time string (HH:MM).
   *
   * ko / ja / zh / ar / he : 24-hour  →  "14:05"
   * en                     : 12-hour  →  "2:05 PM"
   * es / de (future)       : 24-hour  →  "14:05"
   *
   * Returns a string.
   * ─────────────────────────────────────────────────────────── */

  /* ─── Time zone ───────────────────────────────────────────────
     The OS has one configured time zone (Settings > Date & time). null means
     automatic: the system zone. Every date and time an app shows must come
     through parts() — Date#getHours() reads the process zone, which is not
     the user's choice when they picked one by hand. */
  var currentTimeZone = null;
  var _partsFormatter = null;
  var WEEKDAY_INDEX = { Sun: 0, Mon: 1, Tue: 2, Wed: 3, Thu: 4, Fri: 5, Sat: 6 };

  function systemTimeZone() {
    try { return Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC'; }
    catch (e) { return 'UTC'; }
  }

  /**
   * @param {string|null} tz IANA id (e.g. "Asia/Seoul"), or null / "auto"
   * @returns {boolean} false if the id is unknown — the zone is kept
   */
  function setTimeZone(tz) {
    var next = null;
    if (tz && tz !== 'auto') {
      /* Store the engine's canonical id, so "Asia/Kolkata" and the "Asia/Calcutta"
         the engine lists compare equal */
      try { next = new Intl.DateTimeFormat('en-US', { timeZone: tz }).resolvedOptions().timeZone || tz; }
      catch (e) { return false; }
    }
    if (next === currentTimeZone) return true;
    currentTimeZone = next;
    _partsFormatter = null;
    /* Clocks and timestamps re-render on display-setting changes */
    listeners.forEach(function (fn) { fn(currentLocale); });
    return true;
  }

  function getTimeZone() { return currentTimeZone || systemTimeZone(); }
  function isAutoTimeZone() { return currentTimeZone === null; }

  /**
   * Calendar fields of a moment in the configured zone.
   * @param {Date} [date] defaults to now
   * @returns {{year,month,day,hour,minute,second,weekday}|null}
   *          month 1-12, weekday 0 (Sunday) - 6
   */
  function parts(date) {
    var d = (date instanceof Date) ? date : new Date();
    if (isNaN(d.getTime())) return null;
    if (currentTimeZone && typeof Intl !== 'undefined' &&
        Intl.DateTimeFormat && Intl.DateTimeFormat.prototype.formatToParts) {
      try {
        if (!_partsFormatter) {
          _partsFormatter = new Intl.DateTimeFormat('en-US', {
            timeZone: currentTimeZone, hourCycle: 'h23',
            year: 'numeric', month: 'numeric', day: 'numeric',
            hour: 'numeric', minute: 'numeric', second: 'numeric', weekday: 'short'
          });
        }
        var f = {};
        _partsFormatter.formatToParts(d).forEach(function (p) { f[p.type] = p.value; });
        var hour = parseInt(f.hour, 10);
        return {
          year: parseInt(f.year, 10), month: parseInt(f.month, 10), day: parseInt(f.day, 10),
          hour: hour === 24 ? 0 : hour,            /* some engines print 24 at midnight */
          minute: parseInt(f.minute, 10), second: parseInt(f.second, 10),
          weekday: WEEKDAY_INDEX[f.weekday]
        };
      } catch (e) { /* engine without time zone support — fall back to local */ }
    }
    return {
      year: d.getFullYear(), month: d.getMonth() + 1, day: d.getDate(),
      hour: d.getHours(), minute: d.getMinutes(), second: d.getSeconds(),
      weekday: d.getDay()
    };
  }

  /* UTC offset of a zone at an instant, in minutes — whole minutes, so the
     half- and quarter-hour zones (India +5:30, Nepal +5:45) come out right */
  function zoneOffsetMinutes(tz, at) {
    var when = (at instanceof Date) ? at : new Date();
    try {
      var f = new Intl.DateTimeFormat('en-US', {
        timeZone: tz, hourCycle: 'h23',
        year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric'
      });
      var p = {};
      f.formatToParts(when).forEach(function (x) { p[x.type] = x.value; });
      var wall = Date.UTC(+p.year, +p.month - 1, +p.day, (+p.hour) % 24, +p.minute);
      var instant = when.getTime() - when.getSeconds() * 1000 - when.getMilliseconds();
      return Math.round((wall - instant) / 60000);
    } catch (e) {
      return 0;
    }
  }

  /* "GMT+9", "GMT+5:30", "GMT" — digits read the same in every language */
  function formatOffset(minutes) {
    if (!minutes) return 'GMT';
    var sign = minutes > 0 ? '+' : '-';
    var abs = Math.abs(minutes);
    var h = Math.floor(abs / 60);
    var m = abs % 60;
    return 'GMT' + sign + h + (m ? ':' + (m < 10 ? '0' : '') + m : '');
  }

  /* Localised zone name from ICU ("한국 표준시", "米国東部時間"); the id if the
     engine has none */
  var INTL_TAG = { ko: 'ko-KR', en: 'en-US', ja: 'ja-JP', zh: 'zh-CN', es: 'es-ES',
                   ar: 'ar', he: 'he-IL' };
  function zoneName(tz) {
    try {
      var f = new Intl.DateTimeFormat(INTL_TAG[currentLocale] || 'en-US',
                                      { timeZone: tz, timeZoneName: 'longGeneric' });
      var parts = f.formatToParts(new Date());
      for (var i = 0; i < parts.length; i++) {
        if (parts[i].type === 'timeZoneName') return parts[i].value;
      }
    } catch (e) { /* fall through */ }
    return tz;
  }

  /* ─── Lunar date ──────────────────────────────────────────────
     Korean (dangi) and Chinese calendars are lunisolar; holidays and many
     birthdays follow them. ICU converts — no tables here: the two calendars
     even disagree on some dates, because Seoul and Beijing use different
     meridians for the new moon. Returns null for other locales, or when the
     engine lacks the calendar. */
  var LUNAR = { ko: { tag: 'ko-KR', cal: 'dangi' }, zh: { tag: 'zh-CN', cal: 'chinese' } };

  function formatLunarDate(date) {
    var l = LUNAR[currentLocale];
    if (!l || typeof Intl === 'undefined' || !Intl.DateTimeFormat) return null;
    try {
      var opts = { month: 'long', day: 'numeric' };
      if (currentTimeZone) opts.timeZone = currentTimeZone;
      var f = new Intl.DateTimeFormat(l.tag + '-u-ca-' + l.cal, opts);
      if (f.resolvedOptions().calendar !== l.cal) return null;
      return t('date.lunar', { date: f.format(date instanceof Date ? date : new Date()) });
    } catch (e) {
      return null;
    }
  }

  /* ─── Relative time for lists (messages, call log) ────────────
     One formatter for every app: three apps each had their own, two of them
     printing English "5m" / "3h" / "Yesterday" whatever the language.
     Calendar comparisons use the configured zone, so "yesterday" means the
     user's yesterday. */
  function dayKey(p) { return p.year * 10000 + p.month * 100 + p.day; }

  function formatRelative(ts, now) {
    var d = (ts instanceof Date) ? ts : new Date(ts);
    if (isNaN(d.getTime())) return '';
    var ref = (now instanceof Date) ? now : new Date();
    var diffMin = Math.floor((ref.getTime() - d.getTime()) / 60000);
    if (diffMin < 1) return t('time.just_now');
    if (diffMin < 60) return t('time.min_short', { n: diffMin });
    var diffH = Math.floor(diffMin / 60);
    if (diffH < 24) return t('time.hour_short', { n: diffH });
    var pd = parts(d);
    var py = parts(new Date(ref.getTime() - 86400000));
    if (pd && py && dayKey(pd) === dayKey(py)) return t('time.yesterday');
    return t('time.month_day', { m: pd.month, d: pd.day });
  }

  function formatTime(date) {
    if (!(date instanceof Date) || isNaN(date.getTime())) return '';

    var p   = parts(date);
    var h   = p.hour;
    var min = p.minute;

    /* Zero-pad helper */
    function pad2(n) { return n < 10 ? '0' + n : '' + n; }

    if (currentLocale === 'en') {
      /* 12-hour clock with AM/PM */
      var ampm  = h >= 12 ? 'PM' : 'AM';
      var h12   = h % 12;
      if (h12 === 0) { h12 = 12; }
      return h12 + ':' + pad2(min) + ' ' + ampm;
    }

    /* 24-hour clock for all other locales */
    return pad2(h) + ':' + pad2(min);
  }

  /* ─── Date Formatting ─── */
  function formatDate(date) {
    var p = parts(date);
    if (!p) return '';
    var y = p.year;
    var m = p.month;
    var d = p.day;
    var dayOfWeek = p.weekday;
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
    /* Controls with no visible text of their own (switches) still need a
       spoken name in the user's language */
    document.querySelectorAll('[data-i18n-aria-label]').forEach(function (el) {
      el.setAttribute('aria-label', t(el.getAttribute('data-i18n-aria-label')));
    });
    document.documentElement.lang = currentLocale;
    applyDirection(currentLocale);
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
      if (msg.type === 'system.setTimeZone') {
        setTimeZone(msg.timeZone !== undefined ? msg.timeZone : (msg.data && msg.data.timeZone));
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
      /* Request saved locale — uses ZylBridge if available, else postMessage */
      var localeMsg = { type: 'service.request', service: 'settings', method: 'get', params: { category: 'language' } };
      if (typeof ZylBridge !== 'undefined' && ZylBridge.sendToSystem) {
        ZylBridge.sendToSystem(localeMsg);
      } else if (window.parent && window.parent !== window) {
        window.parent.postMessage(JSON.stringify(localeMsg), '*');
      }
    });
  } else {
    applyTranslations();
    var localeMsg2 = { type: 'service.request', service: 'settings', method: 'get', params: { category: 'language' } };
    if (typeof ZylBridge !== 'undefined' && ZylBridge.sendToSystem) {
      ZylBridge.sendToSystem(localeMsg2);
    } else if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify(localeMsg2), '*');
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
    tp: tp,
    formatDate: formatDate,
    formatNumber: formatNumber,
    formatBytes: formatBytes,
    formatTime: formatTime,
    formatLunarDate: formatLunarDate,
    formatRelative: formatRelative,
    parts: parts,
    setTimeZone: setTimeZone,
    getTimeZone: getTimeZone,
    isAutoTimeZone: isAutoTimeZone,
    zoneOffsetMinutes: zoneOffsetMinutes,
    formatOffset: formatOffset,
    zoneName: zoneName,
    setLocale: setLocale,
    getLocale: getLocale,
    getSupportedLocales: getSupportedLocales,
    applyTranslations: applyTranslations,
    onLocaleChange: onLocaleChange,
    addTranslations: addTranslations,
    isRtl: isRtl,
  };
})();
