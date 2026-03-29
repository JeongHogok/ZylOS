// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Weather app translation data
// Scope: weather.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Weather 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'weather.title': '\uB0A0\uC528',
    'weather.humidity': '\uC2B5\uB3C4',
    'weather.wind': '\uBC14\uB78C',
    'weather.pressure': '\uAE30\uC555',
    'weather.forecast': '\uC608\uBCF4',
    'weather.loading': '\uBD88\uB7EC\uC624\uB294 \uC911...',
    'weather.error': '\uB0A0\uC528 \uC815\uBCF4\uB97C \uC0AC\uC6A9\uD560 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4',
    'weather.refresh': '\uC0C8\uB85C\uACE0\uCE68'
  });

  zylI18n.addTranslations('en', {
    'weather.title': 'Weather',
    'weather.humidity': 'Humidity',
    'weather.wind': 'Wind',
    'weather.pressure': 'Pressure',
    'weather.forecast': 'Forecast',
    'weather.loading': 'Loading...',
    'weather.error': 'Weather unavailable',
    'weather.refresh': 'Refresh'
  });

  zylI18n.addTranslations('ja', {
    'weather.title': '\u5929\u6C17',
    'weather.humidity': '\u6E7F\u5EA6',
    'weather.wind': '\u98A8\u901F',
    'weather.pressure': '\u6C17\u5727',
    'weather.forecast': '\u4E88\u5831',
    'weather.loading': '\u8AAD\u307F\u8FBC\u307F\u4E2D...',
    'weather.error': '\u5929\u6C17\u60C5\u5831\u3092\u53D6\u5F97\u3067\u304D\u307E\u305B\u3093',
    'weather.refresh': '\u66F4\u65B0'
  });

  zylI18n.addTranslations('zh', {
    'weather.title': '\u5929\u6C14',
    'weather.humidity': '\u6E7F\u5EA6',
    'weather.wind': '\u98CE\u901F',
    'weather.pressure': '\u6C14\u538B',
    'weather.forecast': '\u9884\u62A5',
    'weather.loading': '\u52A0\u8F7D\u4E2D...',
    'weather.error': '\u65E0\u6CD5\u83B7\u53D6\u5929\u6C14\u4FE1\u606F',
    'weather.refresh': '\u5237\u65B0'
  });

  zylI18n.addTranslations('es', {
    'weather.title': 'Clima',
    'weather.humidity': 'Humedad',
    'weather.wind': 'Viento',
    'weather.pressure': 'Presi\u00F3n',
    'weather.forecast': 'Pron\u00F3stico',
    'weather.loading': 'Cargando...',
    'weather.error': 'Clima no disponible',
    'weather.refresh': 'Actualizar'
  });
})();
