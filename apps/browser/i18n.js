// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Browser app translation data
// Scope: browser.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Browser 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'browser.new_tab': '\uC0C8 \uD0ED',
    'browser.bookmarks': '\uBD81\uB9C8\uD06C',
    'browser.add_bookmark': '\uBD81\uB9C8\uD06C \uCD94\uAC00',
    'browser.menu': '\uBA54\uB274',
    'browser.search': '\uAC80\uC0C9 \uB610\uB294 URL \uC785\uB825'
  });

  zylI18n.addTranslations('en', {
    'browser.new_tab': 'New Tab',
    'browser.bookmarks': 'Bookmarks',
    'browser.add_bookmark': 'Add Bookmark',
    'browser.menu': 'Menu',
    'browser.search': 'Search or enter URL',
    'browser.loading': 'Loading...'
  });

  zylI18n.addTranslations('ja', {
    'browser.new_tab': '\u65B0\u3057\u3044\u30BF\u30D6',
    'browser.bookmarks': '\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF',
    'browser.add_bookmark': '\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF\u3092\u8FFD\u52A0',
    'browser.menu': '\u30E1\u30CB\u30E5\u30FC',
    'browser.search': '\u691C\u7D22\u307E\u305F\u306FURL\u3092\u5165\u529B'
  });

  zylI18n.addTranslations('zh', {
    'browser.new_tab': '\u65B0\u6807\u7B7E\u9875',
    'browser.bookmarks': '\u4E66\u7B7E',
    'browser.add_bookmark': '\u6DFB\u52A0\u4E66\u7B7E',
    'browser.menu': '\u83DC\u5355',
    'browser.search': '\u641C\u7D22\u6216\u8F93\u5165\u7F51\u5740'
  });

  zylI18n.addTranslations('es', {
    'browser.new_tab': 'Nueva pesta\u00F1a',
    'browser.bookmarks': 'Marcadores',
    'browser.add_bookmark': 'Agregar marcador',
    'browser.menu': 'Men\u00FA',
    'browser.search': 'Buscar o ingresar URL',
    'browser.loading': 'Cargando...'
  });
})();
