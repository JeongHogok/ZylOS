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
    'gallery.delete_title': '\uC0AD\uC81C',
    'gallery.share': '\uACF5\uC720',
    'gallery.confirm_delete': '\uC774 \uD30C\uC77C\uC744 \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?',
    'gallery.all': '\uC804\uCCB4',
    'gallery.albums': '\uC568\uBC94',
    'gallery.slideshow': '\uC2AC\uB77C\uC774\uB4DC\uC1FC',
    'gallery.rotate': '\uD68C\uC804',
    'gallery.photos_count': '{count}\uAC1C',
    'gallery.slideshow_playing': '\uC7AC\uC0DD \uC911',
    'gallery.slideshow_paused': '\uC77C\uC2DC\uC815\uC9C0'
  });

  zylI18n.addTranslations('en', {
    'gallery.title': 'Gallery',
    'gallery.no_media': 'No photos or videos',
    'gallery.delete': 'Delete',
    'gallery.delete_title': 'Delete',
    'gallery.share': 'Share',
    'gallery.confirm_delete': 'Delete this file?',
    'gallery.all': 'All',
    'gallery.albums': 'Albums',
    'gallery.slideshow': 'Slideshow',
    'gallery.rotate': 'Rotate',
    'gallery.photos_count': '{count} items',
    'gallery.slideshow_playing': 'Playing',
    'gallery.slideshow_paused': 'Paused'
  });

  zylI18n.addTranslations('ja', {
    'gallery.title': '\u30AE\u30E3\u30E9\u30EA\u30FC',
    'gallery.no_media': '\u5199\u771F\u3084\u52D5\u753B\u304C\u3042\u308A\u307E\u305B\u3093',
    'gallery.delete': '\u524A\u9664',
    'gallery.delete_title': '\u524A\u9664',
    'gallery.share': '\u5171\u6709',
    'gallery.confirm_delete': '\u3053\u306E\u30D5\u30A1\u30A4\u30EB\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'gallery.all': '\u3059\u3079\u3066',
    'gallery.albums': '\u30A2\u30EB\u30D0\u30E0',
    'gallery.slideshow': '\u30B9\u30E9\u30A4\u30C9\u30B7\u30E7\u30FC',
    'gallery.rotate': '\u56DE\u8EE2',
    'gallery.photos_count': '{count}\u4EF6',
    'gallery.slideshow_playing': '\u518D\u751F\u4E2D',
    'gallery.slideshow_paused': '\u4E00\u6642\u505C\u6B62'
  });

  zylI18n.addTranslations('zh', {
    'gallery.title': '\u76F8\u518C',
    'gallery.no_media': '\u6CA1\u6709\u7167\u7247\u6216\u89C6\u9891',
    'gallery.delete': '\u5220\u9664',
    'gallery.delete_title': '\u5220\u9664',
    'gallery.share': '\u5206\u4EAB',
    'gallery.confirm_delete': '\u5220\u9664\u6B64\u6587\u4EF6\uFF1F',
    'gallery.all': '\u5168\u90E8',
    'gallery.albums': '\u76F8\u518C',
    'gallery.slideshow': '\u5E7B\u706F\u7247',
    'gallery.rotate': '\u65CB\u8F6C',
    'gallery.photos_count': '{count}\u9879',
    'gallery.slideshow_playing': '\u64AD\u653E\u4E2D',
    'gallery.slideshow_paused': '\u5DF2\u6682\u505C'
  });

  zylI18n.addTranslations('es', {
    'gallery.title': 'Galer\u00EDa',
    'gallery.no_media': 'No hay fotos ni videos',
    'gallery.delete': 'Eliminar',
    'gallery.delete_title': 'Eliminar',
    'gallery.share': 'Compartir',
    'gallery.confirm_delete': '\u00BFEliminar este archivo?',
    'gallery.all': 'Todo',
    'gallery.albums': '\u00C1lbumes',
    'gallery.slideshow': 'Presentaci\u00F3n',
    'gallery.rotate': 'Rotar',
    'gallery.photos_count': '{count} elementos',
    'gallery.slideshow_playing': 'Reproduciendo',
    'gallery.slideshow_paused': 'En pausa'
  });
})();
