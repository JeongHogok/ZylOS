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
    'calc.title': '\uACC4\uC0B0\uAE30'
  });

  zylI18n.addTranslations('en', {
    'calc.title': 'Calculator'
  });

  zylI18n.addTranslations('ja', {
    'calc.title': '\u8A08\u7B97\u6A5F'
  });

  zylI18n.addTranslations('zh', {
    'calc.title': '\u8BA1\u7B97\u5668'
  });

  zylI18n.addTranslations('es', {
    'calc.title': 'Calculadora'
  });
})();
