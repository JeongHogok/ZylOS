// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Terminal app translation data
// Scope: terminal.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Terminal 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'terminal.title': '\uD130\uBBF8\uB110'
  });

  zylI18n.addTranslations('en', {
    'terminal.title': 'Terminal'
  });

  zylI18n.addTranslations('ja', {
    'terminal.title': '\u30BF\u30FC\u30DF\u30CA\u30EB'
  });

  zylI18n.addTranslations('zh', {
    'terminal.title': '\u7EC8\u7AEF'
  });

  zylI18n.addTranslations('es', {
    'terminal.title': 'Terminal'
  });
})();
