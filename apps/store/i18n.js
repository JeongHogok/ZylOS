// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: App Store translation data
// Scope: store.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Store 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'store.title': '\uC571\uC2A4\uD1A0\uC5B4',
    'store.search': '\uC571 \uAC80\uC0C9...',
    'store.install': '\uC124\uCE58',
    'store.uninstall': '\uC81C\uAC70',
    'store.installed': '\uC124\uCE58\uB428',
    'store.system_app': '\uC2DC\uC2A4\uD15C',
    'store.all': '\uC804\uCCB4',
    'store.available': '\uC124\uCE58 \uAC00\uB2A5'
  });

  zylI18n.addTranslations('en', {
    'store.title': 'App Store',
    'store.search': 'Search apps...',
    'store.install': 'Install',
    'store.uninstall': 'Uninstall',
    'store.installed': 'Installed',
    'store.system_app': 'System',
    'store.all': 'All',
    'store.available': 'Available'
  });

  zylI18n.addTranslations('ja', {
    'store.title': '\u30A2\u30D7\u30EA\u30B9\u30C8\u30A2',
    'store.search': '\u30A2\u30D7\u30EA\u3092\u691C\u7D22...',
    'store.install': '\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB',
    'store.uninstall': '\u30A2\u30F3\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB',
    'store.installed': '\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u6E08\u307F',
    'store.system_app': '\u30B7\u30B9\u30C6\u30E0',
    'store.all': '\u3059\u3079\u3066',
    'store.available': '\u5165\u624B\u53EF\u80FD'
  });

  zylI18n.addTranslations('zh', {
    'store.title': '\u5E94\u7528\u5546\u5E97',
    'store.search': '\u641C\u7D22\u5E94\u7528...',
    'store.install': '\u5B89\u88C5',
    'store.uninstall': '\u5378\u8F7D',
    'store.installed': '\u5DF2\u5B89\u88C5',
    'store.system_app': '\u7CFB\u7EDF',
    'store.all': '\u5168\u90E8',
    'store.available': '\u53EF\u5B89\u88C5'
  });

  zylI18n.addTranslations('es', {
    'store.title': 'App Store',
    'store.search': 'Buscar aplicaciones...',
    'store.install': 'Instalar',
    'store.uninstall': 'Desinstalar',
    'store.installed': 'Instalada',
    'store.system_app': 'Sistema',
    'store.all': 'Todas',
    'store.available': 'Disponibles'
  });
})();
