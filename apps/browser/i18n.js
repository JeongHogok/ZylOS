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
    'browser.search': '\uAC80\uC0C9 \uB610\uB294 URL \uC785\uB825',
    'browser.url_placeholder': '\uAC80\uC0C9 \uB610\uB294 URL \uC785\uB825',
    'browser.loading': '\uBD88\uB7EC\uC624\uB294 \uC911...',
    'browser.url_copied': 'URL\uC774 \uBCF5\uC0AC\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'browser.blocked_iframe': '\uC774 \uC0AC\uC774\uD2B8\uB294 iframe \uC784\uBCA0\uB529\uC744 \uCC28\uB2E8\uD569\uB2C8\uB2E4(X-Frame-Options). \uC2E4\uC81C \uD558\uB4DC\uC6E8\uC5B4\uC5D0\uC11C\uB294 WebKitGTK\uB85C \uD398\uC774\uC9C0\uAC00 \uB85C\uB4DC\uB429\uB2C8\uB2E4.',
    'browser.no_bookmarks': '\uBD81\uB9C8\uD06C \uC5C6\uC74C',
    'browser.history': '\uBC29\uBB38 \uAE30\uB85D',
    'browser.clear_history': '\uAE30\uB85D \uC0AD\uC81C',
    'browser.no_history': '\uBC29\uBB38 \uAE30\uB85D \uC5C6\uC74C',
    'browser.history_cleared': '\uBC29\uBB38 \uAE30\uB85D\uC774 \uC0AD\uC81C\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'browser.menu_copy_url': 'URL \uBCF5\uC0AC',
    'browser.menu_add_bookmark': '\uBD81\uB9C8\uD06C \uCD94\uAC00',
    'browser.menu_history': '\uBC29\uBB38 \uAE30\uB85D',
    'browser.menu_private': '\uC2DC\uAD00 \uBAA8\uB4DC',
    'browser.menu_settings': '\uC124\uC815',
    'browser.private_mode': '\uC2DC\uAD00 \uBAA8\uB4DC',
    'browser.private_on': '\uC2DC\uAD00 \uBAA8\uB4DC \uCF1C\uC9D0',
    'browser.private_off': '\uC2DC\uAD00 \uBAA8\uB4DC \uAEBC\uC9D0',
    'browser.bookmark_added': '\uBD81\uB9C8\uD06C\uAC00 \uCD94\uAC00\uB418\uC5C8\uC2B5\uB2C8\uB2E4'
  });

  zylI18n.addTranslations('en', {
    'browser.new_tab': 'New Tab',
    'browser.bookmarks': 'Bookmarks',
    'browser.add_bookmark': 'Add Bookmark',
    'browser.menu': 'Menu',
    'browser.search': 'Search or enter URL',
    'browser.url_placeholder': 'Search or enter URL',
    'browser.loading': 'Loading...',
    'browser.url_copied': 'URL copied',
    'browser.blocked_iframe': 'This site blocks iframe embedding (X-Frame-Options). On real hardware, pages load via native WebKitGTK.',
    'browser.no_bookmarks': 'No bookmarks',
    'browser.history': 'History',
    'browser.clear_history': 'Clear History',
    'browser.no_history': 'No history',
    'browser.history_cleared': 'History cleared',
    'browser.menu_copy_url': 'Copy URL',
    'browser.menu_add_bookmark': 'Add Bookmark',
    'browser.menu_history': 'History',
    'browser.menu_private': 'Private Mode',
    'browser.menu_settings': 'Settings',
    'browser.private_mode': 'Private Mode',
    'browser.private_on': 'Private mode on',
    'browser.private_off': 'Private mode off',
    'browser.bookmark_added': 'Bookmark added'
  });

  zylI18n.addTranslations('ja', {
    'browser.new_tab': '\u65B0\u3057\u3044\u30BF\u30D6',
    'browser.bookmarks': '\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF',
    'browser.add_bookmark': '\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF\u3092\u8FFD\u52A0',
    'browser.menu': '\u30E1\u30CB\u30E5\u30FC',
    'browser.search': '\u691C\u7D22\u307E\u305F\u306FURL\u3092\u5165\u529B',
    'browser.url_placeholder': '\u691C\u7D22\u307E\u305F\u306FURL\u3092\u5165\u529B',
    'browser.loading': '\u8AAD\u307F\u8FBC\u307F\u4E2D...',
    'browser.url_copied': 'URL\u3092\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F',
    'browser.blocked_iframe': '\u3053\u306E\u30B5\u30A4\u30C8\u306Fiframe\u57CB\u3081\u8FBC\u307F\u3092\u30D6\u30ED\u30C3\u30AF\u3057\u3066\u3044\u307E\u3059(X-Frame-Options)\u3002\u5B9F\u6A5F\u3067\u306FWebKitGTK\u3067\u30DA\u30FC\u30B8\u304C\u8AAD\u307F\u8FBC\u307E\u308C\u307E\u3059\u3002',
    'browser.no_bookmarks': '\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF\u306A\u3057',
    'browser.history': '\u5C65\u6B74',
    'browser.clear_history': '\u5C65\u6B74\u3092\u524A\u9664',
    'browser.no_history': '\u5C65\u6B74\u306A\u3057',
    'browser.history_cleared': '\u5C65\u6B74\u3092\u524A\u9664\u3057\u307E\u3057\u305F',
    'browser.menu_copy_url': 'URL\u3092\u30B3\u30D4\u30FC',
    'browser.menu_add_bookmark': '\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF\u3092\u8FFD\u52A0',
    'browser.menu_history': '\u5C65\u6B74',
    'browser.menu_private': '\u30D7\u30E9\u30A4\u30D9\u30FC\u30C8\u30E2\u30FC\u30C9',
    'browser.menu_settings': '\u8A2D\u5B9A',
    'browser.private_mode': '\u30D7\u30E9\u30A4\u30D9\u30FC\u30C8\u30E2\u30FC\u30C9',
    'browser.private_on': '\u30D7\u30E9\u30A4\u30D9\u30FC\u30C8\u30E2\u30FC\u30C9 \u30AA\u30F3',
    'browser.private_off': '\u30D7\u30E9\u30A4\u30D9\u30FC\u30C8\u30E2\u30FC\u30C9 \u30AA\u30D5',
    'browser.bookmark_added': '\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF\u3092\u8FFD\u52A0\u3057\u307E\u3057\u305F'
  });

  zylI18n.addTranslations('zh', {
    'browser.new_tab': '\u65B0\u6807\u7B7E\u9875',
    'browser.bookmarks': '\u4E66\u7B7E',
    'browser.add_bookmark': '\u6DFB\u52A0\u4E66\u7B7E',
    'browser.menu': '\u83DC\u5355',
    'browser.search': '\u641C\u7D22\u6216\u8F93\u5165\u7F51\u5740',
    'browser.url_placeholder': '\u641C\u7D22\u6216\u8F93\u5165\u7F51\u5740',
    'browser.loading': '\u52A0\u8F7D\u4E2D...',
    'browser.url_copied': 'URL\u5DF2\u590D\u5236',
    'browser.blocked_iframe': '\u6B64\u7F51\u7AD9\u963B\u6B62\u4E86iframe\u5D4C\u5165(X-Frame-Options)\u3002\u5728\u5B9E\u9645\u786C\u4EF6\u4E0A\uFF0C\u9875\u9762\u901A\u8FC7WebKitGTK\u52A0\u8F7D\u3002',
    'browser.no_bookmarks': '\u65E0\u4E66\u7B7E',
    'browser.history': '\u5386\u53F2\u8BB0\u5F55',
    'browser.clear_history': '\u6E05\u9664\u5386\u53F2',
    'browser.no_history': '\u65E0\u5386\u53F2\u8BB0\u5F55',
    'browser.history_cleared': '\u5386\u53F2\u8BB0\u5F55\u5DF2\u6E05\u9664',
    'browser.menu_copy_url': '\u590D\u5236URL',
    'browser.menu_add_bookmark': '\u6DFB\u52A0\u4E66\u7B7E',
    'browser.menu_history': '\u5386\u53F2\u8BB0\u5F55',
    'browser.menu_private': '\u65E0\u75D5\u6A21\u5F0F',
    'browser.menu_settings': '\u8BBE\u7F6E',
    'browser.private_mode': '\u65E0\u75D5\u6A21\u5F0F',
    'browser.private_on': '\u65E0\u75D5\u6A21\u5F0F\u5DF2\u5F00\u542F',
    'browser.private_off': '\u65E0\u75D5\u6A21\u5F0F\u5DF2\u5173\u95ED',
    'browser.bookmark_added': '\u4E66\u7B7E\u5DF2\u6DFB\u52A0'
  });

  zylI18n.addTranslations('es', {
    'browser.new_tab': 'Nueva pesta\u00F1a',
    'browser.bookmarks': 'Marcadores',
    'browser.add_bookmark': 'Agregar marcador',
    'browser.menu': 'Men\u00FA',
    'browser.search': 'Buscar o ingresar URL',
    'browser.url_placeholder': 'Buscar o ingresar URL',
    'browser.loading': 'Cargando...',
    'browser.url_copied': 'URL copiada',
    'browser.blocked_iframe': 'Este sitio bloquea la incrustaci\u00F3n en iframe (X-Frame-Options). En hardware real, las p\u00E1ginas se cargan mediante WebKitGTK nativo.',
    'browser.no_bookmarks': 'Sin marcadores',
    'browser.history': 'Historial',
    'browser.clear_history': 'Borrar historial',
    'browser.no_history': 'Sin historial',
    'browser.history_cleared': 'Historial borrado',
    'browser.menu_copy_url': 'Copiar URL',
    'browser.menu_add_bookmark': 'Agregar marcador',
    'browser.menu_history': 'Historial',
    'browser.menu_private': 'Modo privado',
    'browser.menu_settings': 'Configuraci\u00F3n',
    'browser.private_mode': 'Modo privado',
    'browser.private_on': 'Modo privado activado',
    'browser.private_off': 'Modo privado desactivado',
    'browser.bookmark_added': 'Marcador agregado'
  });
})();
