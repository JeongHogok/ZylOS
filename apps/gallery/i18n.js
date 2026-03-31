// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Gallery app translation data
// Scope: gallery.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Gallery 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'gallery.title': '\uAC24\uB7EC\uB9AC',
    'gallery.no_media': '\uC0AC\uC9C4\uC774\uB098 \uB3D9\uC601\uC0C1\uC774 \uC5C6\uC2B5\uB2C8\uB2E4',
    'gallery.delete': '\uC0AD\uC81C',
    'gallery.share': '\uACF5\uC720',
    'gallery.confirm_delete': '\uC774 \uD30C\uC77C\uC744 \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?'
  });

  zylI18n.addTranslations('en', {
    'gallery.title': 'Gallery',
    'gallery.no_media': 'No photos or videos',
    'gallery.delete': 'Delete',
    'gallery.share': 'Share',
    'gallery.confirm_delete': 'Delete this file?'
  });

  zylI18n.addTranslations('ja', {
    'gallery.title': '\u30AE\u30E3\u30E9\u30EA\u30FC',
    'gallery.no_media': '\u5199\u771F\u3084\u52D5\u753B\u304C\u3042\u308A\u307E\u305B\u3093',
    'gallery.delete': '\u524A\u9664',
    'gallery.share': '\u5171\u6709',
    'gallery.confirm_delete': '\u3053\u306E\u30D5\u30A1\u30A4\u30EB\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F'
  });

  zylI18n.addTranslations('zh', {
    'gallery.title': '\u76F8\u518C',
    'gallery.no_media': '\u6CA1\u6709\u7167\u7247\u6216\u89C6\u9891',
    'gallery.delete': '\u5220\u9664',
    'gallery.share': '\u5206\u4EAB',
    'gallery.confirm_delete': '\u5220\u9664\u6B64\u6587\u4EF6\uFF1F'
  });

  zylI18n.addTranslations('es', {
    'gallery.title': 'Galer\u00EDa',
    'gallery.no_media': 'No hay fotos ni videos',
    'gallery.delete': 'Eliminar',
    'gallery.share': 'Compartir',
    'gallery.confirm_delete': '\u00BFEliminar este archivo?'
  });
})();
