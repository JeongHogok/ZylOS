// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Zyl Keyboard translation data
// Scope: keyboard.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Keyboard 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'keyboard.title': 'Zyl 키보드'
  });

  zylI18n.addTranslations('en', {
    'keyboard.title': 'Zyl Keyboard'
  });

  zylI18n.addTranslations('ja', {
    'keyboard.title': 'Zyl キーボード'
  });

  zylI18n.addTranslations('zh', {
    'keyboard.title': 'Zyl 键盘'
  });

  zylI18n.addTranslations('es', {
    'keyboard.title': 'Teclado Zyl'
  });
})();
