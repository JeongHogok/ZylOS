// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Files app translation data
// Scope: files.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Files 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'files.new_folder': '\uC0C8 \uD3F4\uB354',
    'files.confirm_delete': '\uC774 \uD30C\uC77C\uC744 \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?',
    'files.rename_prompt': '\uC774\uB984 \uBCC0\uACBD',
    'files.title': '\uD30C\uC77C',
    'files.open': '\uC5F4\uAE30',
    'files.rename': '\uC774\uB984 \uBCC0\uACBD',
    'files.delete': '\uC0AD\uC81C',
    'files.share': '\uACF5\uC720',
    'files.sort_name': '\uC774\uB984',
    'files.sort_date': '\uB0A0\uC9DC',
    'files.sort_size': '\uD06C\uAE30'
  });

  zylI18n.addTranslations('en', {
    'files.new_folder': 'New Folder',
    'files.confirm_delete': 'Delete this file?',
    'files.rename_prompt': 'Rename',
    'files.title': 'Files',
    'files.open': 'Open',
    'files.rename': 'Rename',
    'files.delete': 'Delete',
    'files.share': 'Share',
    'files.sort_name': 'Name',
    'files.sort_date': 'Date',
    'files.sort_size': 'Size'
  });

  zylI18n.addTranslations('ja', {
    'files.new_folder': '\u65B0\u898F\u30D5\u30A9\u30EB\u30C0',
    'files.confirm_delete': '\u3053\u306E\u30D5\u30A1\u30A4\u30EB\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'files.rename_prompt': '\u540D\u524D\u3092\u5909\u66F4',
    'files.title': '\u30D5\u30A1\u30A4\u30EB',
    'files.open': '\u958B\u304F',
    'files.rename': '\u540D\u524D\u3092\u5909\u66F4',
    'files.delete': '\u524A\u9664',
    'files.share': '\u5171\u6709',
    'files.sort_name': '\u540D\u524D',
    'files.sort_date': '\u65E5\u4ED8',
    'files.sort_size': '\u30B5\u30A4\u30BA'
  });

  zylI18n.addTranslations('zh', {
    'files.new_folder': '\u65B0\u5EFA\u6587\u4EF6\u5939',
    'files.confirm_delete': '\u5220\u9664\u6B64\u6587\u4EF6\uFF1F',
    'files.rename_prompt': '\u91CD\u547D\u540D',
    'files.title': '\u6587\u4EF6',
    'files.open': '\u6253\u5F00',
    'files.rename': '\u91CD\u547D\u540D',
    'files.delete': '\u5220\u9664',
    'files.share': '\u5171\u4EAB',
    'files.sort_name': '\u540D\u79F0',
    'files.sort_date': '\u65E5\u671F',
    'files.sort_size': '\u5927\u5C0F'
  });

  zylI18n.addTranslations('es', {
    'files.new_folder': 'Nueva carpeta',
    'files.confirm_delete': '\u00BFEliminar este archivo?',
    'files.rename_prompt': 'Cambiar nombre',
    'files.title': 'Archivos',
    'files.open': 'Abrir',
    'files.rename': 'Cambiar nombre',
    'files.delete': 'Eliminar',
    'files.share': 'Compartir',
    'files.sort_name': 'Nombre',
    'files.sort_date': 'Fecha',
    'files.sort_size': 'Tama\u00F1o'
  });
})();
