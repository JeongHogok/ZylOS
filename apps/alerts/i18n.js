// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Emergency alerts app translation data
// Scope: alerts.* keys for ko/en/ja/zh/es (category names follow each
//        market's own naming, e.g. KPAS 위급/긴급/안전안내 in Korean)
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — alerts translation data only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;


  zylI18n.addTranslations('ko', {
    'alerts.title': '\uC7AC\uB09C\uBB38\uC790',
    'alerts.history': '\uBC1B\uC740 \uC7AC\uB09C\uBB38\uC790',
    'alerts.none': '\uBC1B\uC740 \uC7AC\uB09C\uBB38\uC790\uAC00 \uC5C6\uC2B5\uB2C8\uB2E4',
    'alerts.settings': '\uC218\uC2E0 \uC124\uC815',
    'alerts.always_on': '\uD56D\uC0C1 \uBC1B\uC74C',
    'alerts.ok': '\uD655\uC778',
    'alerts.cat.presidential': '\uC704\uAE09\uC7AC\uB09C\uBB38\uC790',
    'alerts.cat.extreme': '\uAE34\uAE09\uC7AC\uB09C\uBB38\uC790',
    'alerts.cat.severe': '\uC548\uC804\uC548\uB0B4\uBB38\uC790',
    'alerts.cat.amber': '\uC2E4\uC885\uACBD\uBCF4\uBB38\uC790',
    'alerts.cat.public_safety': '\uACF5\uACF5\uC548\uC804 \uC54C\uB9BC',
    'alerts.cat.earthquake': '\uC9C0\uC9C4\u00B7\uD574\uC77C \uACBD\uBCF4',
    'alerts.cat.test': '\uD6C8\uB828\u00B7\uC2DC\uD5D8 \uACBD\uBCF4'
  });

  zylI18n.addTranslations('en', {
    'alerts.title': 'Emergency alerts',
    'alerts.history': 'Received alerts',
    'alerts.none': 'No alerts received',
    'alerts.settings': 'Alert settings',
    'alerts.always_on': 'Always on',
    'alerts.ok': 'OK',
    'alerts.cat.presidential': 'National alert',
    'alerts.cat.extreme': 'Extreme alert',
    'alerts.cat.severe': 'Severe alert',
    'alerts.cat.amber': 'Missing person alert',
    'alerts.cat.public_safety': 'Public safety message',
    'alerts.cat.earthquake': 'Earthquake and tsunami warning',
    'alerts.cat.test': 'Test alert'
  });

  zylI18n.addTranslations('ja', {
    'alerts.title': '\u7DCA\u6025\u901F\u5831',
    'alerts.history': '\u53D7\u4FE1\u3057\u305F\u901F\u5831',
    'alerts.none': '\u53D7\u4FE1\u3057\u305F\u901F\u5831\u306F\u3042\u308A\u307E\u305B\u3093',
    'alerts.settings': '\u53D7\u4FE1\u8A2D\u5B9A',
    'alerts.always_on': '\u5E38\u306B\u53D7\u4FE1',
    'alerts.ok': 'OK',
    'alerts.cat.presidential': '\u56FD\u5BB6\u7DCA\u6025\u8B66\u5831',
    'alerts.cat.extreme': '\u7DCA\u6025\u8B66\u5831',
    'alerts.cat.severe': '\u91CD\u5927\u8B66\u5831',
    'alerts.cat.amber': '\u884C\u65B9\u4E0D\u660E\u8005\u8B66\u5831',
    'alerts.cat.public_safety': '\u516C\u5171\u5B89\u5168\u60C5\u5831',
    'alerts.cat.earthquake': '\u5730\u9707\u30FB\u6D25\u6CE2\u8B66\u5831',
    'alerts.cat.test': '\u8A13\u7DF4\u30FB\u30C6\u30B9\u30C8\u8B66\u5831'
  });

  zylI18n.addTranslations('zh', {
    'alerts.title': '\u7D27\u6025\u8B66\u62A5',
    'alerts.history': '\u5DF2\u6536\u5230\u7684\u8B66\u62A5',
    'alerts.none': '\u5C1A\u672A\u6536\u5230\u8B66\u62A5',
    'alerts.settings': '\u63A5\u6536\u8BBE\u7F6E',
    'alerts.always_on': '\u59CB\u7EC8\u63A5\u6536',
    'alerts.ok': '\u786E\u5B9A',
    'alerts.cat.presidential': '\u56FD\u5BB6\u7EA7\u8B66\u62A5',
    'alerts.cat.extreme': '\u7279\u522B\u4E25\u91CD\u8B66\u62A5',
    'alerts.cat.severe': '\u4E25\u91CD\u8B66\u62A5',
    'alerts.cat.amber': '\u5931\u8E2A\u4EBA\u5458\u8B66\u62A5',
    'alerts.cat.public_safety': '\u516C\u5171\u5B89\u5168\u4FE1\u606F',
    'alerts.cat.earthquake': '\u5730\u9707\u6D77\u5578\u9884\u8B66',
    'alerts.cat.test': '\u6D4B\u8BD5\u8B66\u62A5'
  });

  zylI18n.addTranslations('es', {
    'alerts.title': 'Alertas de emergencia',
    'alerts.history': 'Alertas recibidas',
    'alerts.none': 'No has recibido alertas',
    'alerts.settings': 'Ajustes de alertas',
    'alerts.always_on': 'Siempre activada',
    'alerts.ok': 'Aceptar',
    'alerts.cat.presidential': 'Alerta nacional',
    'alerts.cat.extreme': 'Alerta extrema',
    'alerts.cat.severe': 'Alerta grave',
    'alerts.cat.amber': 'Alerta de persona desaparecida',
    'alerts.cat.public_safety': 'Mensaje de seguridad p\u00FAblica',
    'alerts.cat.earthquake': 'Alerta de terremoto y tsunami',
    'alerts.cat.test': 'Alerta de prueba'
  });
})();
