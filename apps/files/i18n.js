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
    'files.sort_size': '\uD06C\uAE30',
    'files.empty_folder': '\uBE48 \uD3F4\uB354',
    'files.loading': '\uB85C\uB529 \uC911...',
    'files.delete_success': '\uD30C\uC77C\uC774 \uC0AD\uC81C\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'files.delete_error': '\uD30C\uC77C \uC0AD\uC81C\uC5D0 \uC2E4\uD328\uD588\uC2B5\uB2C8\uB2E4',
    'files.rename_success': '\uC774\uB984\uC774 \uBCC0\uACBD\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'files.rename_error': '\uC774\uB984 \uBCC0\uACBD\uC5D0 \uC2E4\uD328\uD588\uC2B5\uB2C8\uB2E4',
    'files.mkdir_success': '\uD3F4\uB354\uAC00 \uC0DD\uC131\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'files.mkdir_error': '\uD3F4\uB354 \uC0DD\uC131\uC5D0 \uC2E4\uD328\uD588\uC2B5\uB2C8\uB2E4',
    'files.nav_error': '\uD3F4\uB354\uB97C \uC5F4 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4'
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
    'files.sort_size': 'Size',
    'files.empty_folder': 'Empty folder',
    'files.loading': 'Loading...',
    'files.delete_success': 'File deleted',
    'files.delete_error': 'Failed to delete file',
    'files.rename_success': 'File renamed',
    'files.rename_error': 'Failed to rename file',
    'files.mkdir_success': 'Folder created',
    'files.mkdir_error': 'Failed to create folder',
    'files.nav_error': 'Cannot open folder'
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
    'files.sort_size': '\u30B5\u30A4\u30BA',
    'files.empty_folder': '\u7A7A\u306E\u30D5\u30A9\u30EB\u30C0',
    'files.loading': '\u8AAD\u307F\u8FBC\u307F\u4E2D...',
    'files.delete_success': '\u30D5\u30A1\u30A4\u30EB\u3092\u524A\u9664\u3057\u307E\u3057\u305F',
    'files.delete_error': '\u30D5\u30A1\u30A4\u30EB\u306E\u524A\u9664\u306B\u5931\u6557\u3057\u307E\u3057\u305F',
    'files.rename_success': '\u540D\u524D\u3092\u5909\u66F4\u3057\u307E\u3057\u305F',
    'files.rename_error': '\u540D\u524D\u306E\u5909\u66F4\u306B\u5931\u6557\u3057\u307E\u3057\u305F',
    'files.mkdir_success': '\u30D5\u30A9\u30EB\u30C0\u3092\u4F5C\u6210\u3057\u307E\u3057\u305F',
    'files.mkdir_error': '\u30D5\u30A9\u30EB\u30C0\u306E\u4F5C\u6210\u306B\u5931\u6557\u3057\u307E\u3057\u305F',
    'files.nav_error': '\u30D5\u30A9\u30EB\u30C0\u3092\u958B\u3051\u307E\u305B\u3093'
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
    'files.sort_size': '\u5927\u5C0F',
    'files.empty_folder': '\u7A7A\u6587\u4EF6\u5939',
    'files.loading': '\u52A0\u8F7D\u4E2D...',
    'files.delete_success': '\u6587\u4EF6\u5DF2\u5220\u9664',
    'files.delete_error': '\u5220\u9664\u6587\u4EF6\u5931\u8D25',
    'files.rename_success': '\u6587\u4EF6\u5DF2\u91CD\u547D\u540D',
    'files.rename_error': '\u91CD\u547D\u540D\u5931\u8D25',
    'files.mkdir_success': '\u6587\u4EF6\u5939\u5DF2\u521B\u5EFA',
    'files.mkdir_error': '\u521B\u5EFA\u6587\u4EF6\u5939\u5931\u8D25',
    'files.nav_error': '\u65E0\u6CD5\u6253\u5F00\u6587\u4EF6\u5939'
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
    'files.sort_size': 'Tama\u00F1o',
    'files.empty_folder': 'Carpeta vac\u00EDa',
    'files.loading': 'Cargando...',
    'files.delete_success': 'Archivo eliminado',
    'files.delete_error': 'Error al eliminar archivo',
    'files.rename_success': 'Archivo renombrado',
    'files.rename_error': 'Error al renombrar archivo',
    'files.mkdir_success': 'Carpeta creada',
    'files.mkdir_error': 'Error al crear carpeta',
    'files.nav_error': 'No se puede abrir la carpeta'
  });
})();
