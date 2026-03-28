/*
 * BPI-OS i18n (다국어 지원)
 *
 * 앱 전역에서 사용할 수 있는 다국어 시스템.
 * - data-i18n 속성: 텍스트 번역
 * - data-i18n-placeholder 속성: placeholder 번역
 * - i18n.t('key') 함수: JS에서 직접 사용
 * - 날짜/시간 포맷도 로케일 대응
 */

var i18n = (function () {
  'use strict';

  /* ─── 번역 데이터 ─── */
  var translations = {
    ko: {
      'search': '검색...',
      'app.browser': '브라우저',
      'app.files': '파일',
      'app.terminal': '터미널',
      'app.settings': '설정',
      'app.camera': '카메라',
      'app.gallery': '갤러리',
      'app.music': '음악',
      'app.clock': '시계',
      'app.calc': '계산기',
      'app.notes': '메모',
      'app.weather': '날씨',
      'app.store': '앱스토어',
      'date.format': '{y}년 {m}월 {d}일 {day}',
      'day.0': '일요일', 'day.1': '월요일', 'day.2': '화요일',
      'day.3': '수요일', 'day.4': '목요일', 'day.5': '금요일',
      'day.6': '토요일',
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
      'date.format': '{day}, {monthName} {d}, {y}',
      'day.0': 'Sunday', 'day.1': 'Monday', 'day.2': 'Tuesday',
      'day.3': 'Wednesday', 'day.4': 'Thursday', 'day.5': 'Friday',
      'day.6': 'Saturday',
      'month.1': 'January', 'month.2': 'February', 'month.3': 'March',
      'month.4': 'April', 'month.5': 'May', 'month.6': 'June',
      'month.7': 'July', 'month.8': 'August', 'month.9': 'September',
      'month.10': 'October', 'month.11': 'November', 'month.12': 'December',
    },
    ja: {
      'search': '検索...',
      'app.browser': 'ブラウザ',
      'app.files': 'ファイル',
      'app.terminal': 'ターミナル',
      'app.settings': '設定',
      'app.camera': 'カメラ',
      'app.gallery': 'ギャラリー',
      'app.music': 'ミュージック',
      'app.clock': '時計',
      'app.calc': '計算機',
      'app.notes': 'メモ',
      'app.weather': '天気',
      'app.store': 'アプリストア',
      'date.format': '{y}年{m}月{d}日 {day}',
      'day.0': '日曜日', 'day.1': '月曜日', 'day.2': '火曜日',
      'day.3': '水曜日', 'day.4': '木曜日', 'day.5': '金曜日',
      'day.6': '土曜日',
    },
    zh: {
      'search': '搜索...',
      'app.browser': '浏览器',
      'app.files': '文件',
      'app.terminal': '终端',
      'app.settings': '设置',
      'app.camera': '相机',
      'app.gallery': '相册',
      'app.music': '音乐',
      'app.clock': '时钟',
      'app.calc': '计算器',
      'app.notes': '备忘录',
      'app.weather': '天气',
      'app.store': '应用商店',
      'date.format': '{y}年{m}月{d}日 {day}',
      'day.0': '星期日', 'day.1': '星期一', 'day.2': '星期二',
      'day.3': '星期三', 'day.4': '星期四', 'day.5': '星期五',
      'day.6': '星期六',
    },
    es: {
      'search': 'Buscar...',
      'app.browser': 'Navegador',
      'app.files': 'Archivos',
      'app.terminal': 'Terminal',
      'app.settings': 'Ajustes',
      'app.camera': 'Cámara',
      'app.gallery': 'Galería',
      'app.music': 'Música',
      'app.clock': 'Reloj',
      'app.calc': 'Calculadora',
      'app.notes': 'Notas',
      'app.weather': 'Clima',
      'app.store': 'App Store',
      'date.format': '{day}, {d} de {monthName} de {y}',
      'day.0': 'Domingo', 'day.1': 'Lunes', 'day.2': 'Martes',
      'day.3': 'Miércoles', 'day.4': 'Jueves', 'day.5': 'Viernes',
      'day.6': 'Sábado',
      'month.1': 'enero', 'month.2': 'febrero', 'month.3': 'marzo',
      'month.4': 'abril', 'month.5': 'mayo', 'month.6': 'junio',
      'month.7': 'julio', 'month.8': 'agosto', 'month.9': 'septiembre',
      'month.10': 'octubre', 'month.11': 'noviembre', 'month.12': 'diciembre',
    },
  };

  var currentLocale = 'ko';
  var fallbackLocale = 'en';

  /* ─── 로케일 감지 ─── */
  function detectLocale() {
    /* 시스템 설정에서 가져오거나 브라우저 언어 사용 */
    var lang = (navigator.language || navigator.userLanguage || 'ko').split('-')[0];
    if (translations[lang]) return lang;
    return fallbackLocale;
  }

  /* ─── 번역 키 조회 ─── */
  function t(key, params) {
    var dict = translations[currentLocale] || translations[fallbackLocale];
    var text = dict[key];
    if (!text) {
      var fb = translations[fallbackLocale];
      text = fb ? fb[key] : key;
    }
    if (!text) return key;

    /* 파라미터 치환: {key} → value */
    if (params) {
      Object.keys(params).forEach(function (k) {
        text = text.replace(new RegExp('\\{' + k + '\\}', 'g'), params[k]);
      });
    }
    return text;
  }

  /* ─── 날짜 포맷 ─── */
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

  /* ─── DOM 요소 번역 적용 ─── */
  function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(function (el) {
      el.textContent = t(el.getAttribute('data-i18n'));
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(function (el) {
      el.placeholder = t(el.getAttribute('data-i18n-placeholder'));
    });
    document.documentElement.lang = currentLocale;
  }

  /* ─── 로케일 변경 ─── */
  function setLocale(locale) {
    if (!translations[locale]) return;
    currentLocale = locale;
    applyTranslations();
  }

  /* ─── 지원 로케일 목록 ─── */
  function getSupportedLocales() {
    return Object.keys(translations);
  }

  /* ─── 초기화 ─── */
  currentLocale = detectLocale();

  /* DOM 준비 시 번역 적용 */
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', applyTranslations);
  } else {
    applyTranslations();
  }

  return {
    t: t,
    formatDate: formatDate,
    setLocale: setLocale,
    getLocale: function () { return currentLocale; },
    getSupportedLocales: getSupportedLocales,
  };
})();
