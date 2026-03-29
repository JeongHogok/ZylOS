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
    'files.rename_prompt': '\uC774\uB984 \uBCC0\uACBD'
  });

  zylI18n.addTranslations('en', {
    'files.new_folder': 'New Folder',
    'files.confirm_delete': 'Delete this file?',
    'files.rename_prompt': 'Rename'
  });

  zylI18n.addTranslations('ja', {
    'files.new_folder': '\u65B0\u898F\u30D5\u30A9\u30EB\u30C0',
    'files.confirm_delete': '\u3053\u306E\u30D5\u30A1\u30A4\u30EB\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'files.rename_prompt': '\u540D\u524D\u3092\u5909\u66F4'
  });

  zylI18n.addTranslations('zh', {
    'files.new_folder': '\u65B0\u5EFA\u6587\u4EF6\u5939',
    'files.confirm_delete': '\u5220\u9664\u6B64\u6587\u4EF6\uFF1F',
    'files.rename_prompt': '\u91CD\u547D\u540D'
  });

  zylI18n.addTranslations('es', {
    'files.new_folder': 'Nueva carpeta',
    'files.confirm_delete': '\u00BFEliminar este archivo?',
    'files.rename_prompt': 'Cambiar nombre'
  });
})();
