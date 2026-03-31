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
    'notes.no_notes': '\uBA54\uBAA8\uAC00 \uC5C6\uC2B5\uB2C8\uB2E4'
  });

  zylI18n.addTranslations('en', {
    'notes.title': 'Notes',
    'notes.new': 'New Note',
    'notes.save': 'Save',
    'notes.delete': 'Delete',
    'notes.untitled': 'Untitled',
    'notes.confirm_delete': 'Delete this note?',
    'notes.no_notes': 'No notes yet'
  });

  zylI18n.addTranslations('ja', {
    'notes.title': '\u30E1\u30E2',
    'notes.new': '\u65B0\u898F\u30E1\u30E2',
    'notes.save': '\u4FDD\u5B58',
    'notes.delete': '\u524A\u9664',
    'notes.untitled': '\u7121\u984C',
    'notes.confirm_delete': '\u3053\u306E\u30E1\u30E2\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'notes.no_notes': '\u30E1\u30E2\u304C\u3042\u308A\u307E\u305B\u3093'
  });

  zylI18n.addTranslations('zh', {
    'notes.title': '\u5907\u5FD8\u5F55',
    'notes.new': '\u65B0\u5EFA\u7B14\u8BB0',
    'notes.save': '\u4FDD\u5B58',
    'notes.delete': '\u5220\u9664',
    'notes.untitled': '\u65E0\u6807\u9898',
    'notes.confirm_delete': '\u5220\u9664\u8FD9\u6761\u7B14\u8BB0\uFF1F',
    'notes.no_notes': '\u6682\u65E0\u7B14\u8BB0'
  });

  zylI18n.addTranslations('es', {
    'notes.title': 'Notas',
    'notes.new': 'Nueva nota',
    'notes.save': 'Guardar',
    'notes.delete': 'Eliminar',
    'notes.untitled': 'Sin t\u00EDtulo',
    'notes.confirm_delete': '\u00BFEliminar esta nota?',
    'notes.no_notes': 'No hay notas'
  });
})();
