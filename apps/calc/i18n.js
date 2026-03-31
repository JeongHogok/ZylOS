// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Calculator app translation data
// Scope: calc.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Calculator 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'calc.title': '\uACC4\uC0B0\uAE30',
    'calc.scientific': '\uACF5\uD559\uC6A9',
    'calc.history': '\uAE30\uB85D',
    'calc.clear_history': '\uC9C0\uC6B0\uAE30',
    'calc.no_history': '\uACC4\uC0B0 \uAE30\uB85D \uC5C6\uC74C'
  });

  zylI18n.addTranslations('en', {
    'calc.title': 'Calculator',
    'calc.scientific': 'SCI',
    'calc.history': 'History',
    'calc.clear_history': 'Clear',
    'calc.no_history': 'No history'
  });

  zylI18n.addTranslations('ja', {
    'calc.title': '\u8A08\u7B97\u6A5F',
    'calc.scientific': '\u95A2\u6570',
    'calc.history': '\u5C65\u6B74',
    'calc.clear_history': '\u30AF\u30EA\u30A2',
    'calc.no_history': '\u5C65\u6B74\u306A\u3057'
  });

  zylI18n.addTranslations('zh', {
    'calc.title': '\u8BA1\u7B97\u5668',
    'calc.scientific': '\u79D1\u5B66',
    'calc.history': '\u5386\u53F2',
    'calc.clear_history': '\u6E05\u9664',
    'calc.no_history': '\u65E0\u8BB0\u5F55'
  });

  zylI18n.addTranslations('es', {
    'calc.title': 'Calculadora',
    'calc.scientific': 'CIENT',
    'calc.history': 'Historial',
    'calc.clear_history': 'Borrar',
    'calc.no_history': 'Sin historial'
  });
})();
