// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Notes app translation data
// Scope: notes.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Notes 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'notes.title': '\uBA54\uBAA8',
    'notes.new': '\uC0C8 \uBA54\uBAA8',
    'notes.save': '\uC800\uC7A5',
    'notes.delete': '\uC0AD\uC81C',
    'notes.untitled': '\uC81C\uBAA9 \uC5C6\uC74C',
    'notes.confirm_delete': '\uC774 \uBA54\uBAA8\uB97C \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?',
    'notes.no_notes': '\uBA54\uBAA8\uAC00 \uC5C6\uC2B5\uB2C8\uB2E4',
    'notes.save_success': '\uBA54\uBAA8\uAC00 \uC800\uC7A5\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'notes.save_error': '\uBA54\uBAA8 \uC800\uC7A5\uC5D0 \uC2E4\uD328\uD588\uC2B5\uB2C8\uB2E4',
    'notes.delete_success': '\uBA54\uBAA8\uAC00 \uC0AD\uC81C\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'notes.delete_error': '\uBA54\uBAA8 \uC0AD\uC81C\uC5D0 \uC2E4\uD328\uD588\uC2B5\uB2C8\uB2E4',
    'notes.search_placeholder': '\uBA54\uBAA8 \uAC80\uC0C9...',
    'notes.sort_date': '\uB0A0\uC9DC',
    'notes.sort_name': '\uC774\uB984',
    'notes.title_placeholder': '\uC81C\uBAA9',
    'notes.body_placeholder': '\uB0B4\uC6A9\uC744 \uC785\uB825\uD558\uC138\uC694...',
    'notes.bold': '\uAD75\uAC8C',
    'notes.italic': '\uAE30\uC6B8\uC784',
    'notes.heading': '\uC81C\uBAA9'
  });

  zylI18n.addTranslations('en', {
    'notes.title': 'Notes',
    'notes.new': 'New Note',
    'notes.save': 'Save',
    'notes.delete': 'Delete',
    'notes.untitled': 'Untitled',
    'notes.confirm_delete': 'Delete this note?',
    'notes.no_notes': 'No notes yet',
    'notes.save_success': 'Note saved',
    'notes.save_error': 'Failed to save note',
    'notes.delete_success': 'Note deleted',
    'notes.delete_error': 'Failed to delete note',
    'notes.search_placeholder': 'Search notes...',
    'notes.sort_date': 'Date',
    'notes.sort_name': 'Name',
    'notes.title_placeholder': 'Title',
    'notes.body_placeholder': 'Start writing...',
    'notes.bold': 'Bold',
    'notes.italic': 'Italic',
    'notes.heading': 'Heading'
  });

  zylI18n.addTranslations('ja', {
    'notes.title': '\u30E1\u30E2',
    'notes.new': '\u65B0\u898F\u30E1\u30E2',
    'notes.save': '\u4FDD\u5B58',
    'notes.delete': '\u524A\u9664',
    'notes.untitled': '\u7121\u984C',
    'notes.confirm_delete': '\u3053\u306E\u30E1\u30E2\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'notes.no_notes': '\u30E1\u30E2\u304C\u3042\u308A\u307E\u305B\u3093',
    'notes.save_success': '\u30E1\u30E2\u3092\u4FDD\u5B58\u3057\u307E\u3057\u305F',
    'notes.save_error': '\u30E1\u30E2\u306E\u4FDD\u5B58\u306B\u5931\u6557\u3057\u307E\u3057\u305F',
    'notes.delete_success': '\u30E1\u30E2\u3092\u524A\u9664\u3057\u307E\u3057\u305F',
    'notes.delete_error': '\u30E1\u30E2\u306E\u524A\u9664\u306B\u5931\u6557\u3057\u307E\u3057\u305F',
    'notes.search_placeholder': '\u30E1\u30E2\u3092\u691C\u7D22...',
    'notes.sort_date': '\u65E5\u4ED8',
    'notes.sort_name': '\u540D\u524D',
    'notes.title_placeholder': '\u30BF\u30A4\u30C8\u30EB',
    'notes.body_placeholder': '\u5165\u529B\u3092\u958B\u59CB...',
    'notes.bold': '\u592A\u5B57',
    'notes.italic': '\u659C\u4F53',
    'notes.heading': '\u898B\u51FA\u3057'
  });

  zylI18n.addTranslations('zh', {
    'notes.title': '\u5907\u5FD8\u5F55',
    'notes.new': '\u65B0\u5EFA\u7B14\u8BB0',
    'notes.save': '\u4FDD\u5B58',
    'notes.delete': '\u5220\u9664',
    'notes.untitled': '\u65E0\u6807\u9898',
    'notes.confirm_delete': '\u5220\u9664\u8FD9\u6761\u7B14\u8BB0\uFF1F',
    'notes.no_notes': '\u6682\u65E0\u7B14\u8BB0',
    'notes.save_success': '\u7B14\u8BB0\u5DF2\u4FDD\u5B58',
    'notes.save_error': '\u4FDD\u5B58\u7B14\u8BB0\u5931\u8D25',
    'notes.delete_success': '\u7B14\u8BB0\u5DF2\u5220\u9664',
    'notes.delete_error': '\u5220\u9664\u7B14\u8BB0\u5931\u8D25',
    'notes.search_placeholder': '\u641C\u7D22\u7B14\u8BB0...',
    'notes.sort_date': '\u65E5\u671F',
    'notes.sort_name': '\u540D\u79F0',
    'notes.title_placeholder': '\u6807\u9898',
    'notes.body_placeholder': '\u5F00\u59CB\u8F93\u5165...',
    'notes.bold': '\u52A0\u7C97',
    'notes.italic': '\u659C\u4F53',
    'notes.heading': '\u6807\u9898'
  });

  zylI18n.addTranslations('es', {
    'notes.title': 'Notas',
    'notes.new': 'Nueva nota',
    'notes.save': 'Guardar',
    'notes.delete': 'Eliminar',
    'notes.untitled': 'Sin t\u00EDtulo',
    'notes.confirm_delete': '\u00BFEliminar esta nota?',
    'notes.no_notes': 'No hay notas',
    'notes.save_success': 'Nota guardada',
    'notes.save_error': 'Error al guardar nota',
    'notes.delete_success': 'Nota eliminada',
    'notes.delete_error': 'Error al eliminar nota',
    'notes.search_placeholder': 'Buscar notas...',
    'notes.sort_date': 'Fecha',
    'notes.sort_name': 'Nombre',
    'notes.title_placeholder': 'T\u00EDtulo',
    'notes.body_placeholder': 'Empieza a escribir...',
    'notes.bold': 'Negrita',
    'notes.italic': 'Cursiva',
    'notes.heading': 'Encabezado'
  });
})();
