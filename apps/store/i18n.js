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
    'store.search_placeholder': '\uC571 \uAC80\uC0C9...',
    'store.install': '\uC124\uCE58',
    'store.uninstall': '\uC81C\uAC70',
    'store.installed': '\uC124\uCE58\uB428',
    'store.system_app': '\uC2DC\uC2A4\uD15C',
    'store.system': '\uC2DC\uC2A4\uD15C',
    'store.all': '\uC804\uCCB4',
    'store.tab_all': '\uC804\uCCB4',
    'store.tab_installed': '\uC124\uCE58\uB428',
    'store.empty': '\uC571\uC744 \uCC3E\uC744 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4',
    'store.loading': '\uB85C\uB529 \uC911...',
    'store.available': '\uC124\uCE58 \uAC00\uB2A5'
  });

  zylI18n.addTranslations('en', {
    'store.title': 'App Store',
    'store.search': 'Search apps...',
    'store.search_placeholder': 'Search apps...',
    'store.install': 'Install',
    'store.uninstall': 'Uninstall',
    'store.installed': 'Installed',
    'store.system_app': 'System',
    'store.system': 'System',
    'store.all': 'All',
    'store.tab_all': 'All',
    'store.tab_installed': 'Installed',
    'store.empty': 'No apps found',
    'store.loading': 'Loading...',
    'store.available': 'Available'
  });

  zylI18n.addTranslations('ja', {
    'store.title': '\u30A2\u30D7\u30EA\u30B9\u30C8\u30A2',
    'store.search': '\u30A2\u30D7\u30EA\u3092\u691C\u7D22...',
    'store.search_placeholder': '\u30A2\u30D7\u30EA\u3092\u691C\u7D22...',
    'store.install': '\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB',
    'store.uninstall': '\u30A2\u30F3\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB',
    'store.installed': '\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u6E08\u307F',
    'store.system_app': '\u30B7\u30B9\u30C6\u30E0',
    'store.system': '\u30B7\u30B9\u30C6\u30E0',
    'store.all': '\u3059\u3079\u3066',
    'store.tab_all': '\u3059\u3079\u3066',
    'store.tab_installed': '\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u6E08\u307F',
    'store.empty': '\u30A2\u30D7\u30EA\u304C\u898B\u3064\u304B\u308A\u307E\u305B\u3093',
    'store.loading': '\u8AAD\u307F\u8FBC\u307F\u4E2D...',
    'store.available': '\u5165\u624B\u53EF\u80FD'
  });

  zylI18n.addTranslations('zh', {
    'store.title': '\u5E94\u7528\u5546\u5E97',
    'store.search': '\u641C\u7D22\u5E94\u7528...',
    'store.search_placeholder': '\u641C\u7D22\u5E94\u7528...',
    'store.install': '\u5B89\u88C5',
    'store.uninstall': '\u5378\u8F7D',
    'store.installed': '\u5DF2\u5B89\u88C5',
    'store.system_app': '\u7CFB\u7EDF',
    'store.system': '\u7CFB\u7EDF',
    'store.all': '\u5168\u90E8',
    'store.tab_all': '\u5168\u90E8',
    'store.tab_installed': '\u5DF2\u5B89\u88C5',
    'store.empty': '\u672A\u627E\u5230\u5E94\u7528',
    'store.loading': '\u52A0\u8F7D\u4E2D...',
    'store.available': '\u53EF\u5B89\u88C5'
  });

  zylI18n.addTranslations('es', {
    'store.title': 'App Store',
    'store.search': 'Buscar aplicaciones...',
    'store.search_placeholder': 'Buscar apps...',
    'store.install': 'Instalar',
    'store.uninstall': 'Desinstalar',
    'store.installed': 'Instalada',
    'store.system_app': 'Sistema',
    'store.system': 'Sistema',
    'store.all': 'Todas',
    'store.tab_all': 'Todas',
    'store.tab_installed': 'Instaladas',
    'store.empty': 'No se encontraron apps',
    'store.loading': 'Cargando...',
    'store.available': 'Disponibles'
  });
})();
