// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Home app translation data
// Scope: home.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Home 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'home.loading': '\uB85C\uB529 \uC911...',
    'home.delete_title': '\uC571 \uC0AD\uC81C',
    'home.delete_confirm': '\u0027{name}\u0027\uC744(\uB97C) \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?',
    'home.delete_yes': '\uC0AD\uC81C',
    'home.delete_no': '\uCDE8\uC18C',
    'home.sort_alpha': '\uC774\uB984\uC21C',
    'home.sort_custom': '\uC0AC\uC6A9\uC790 \uC815\uB82C'
  });

  zylI18n.addTranslations('en', {
    'home.loading': 'Loading...',
    'home.delete_title': 'Delete App',
    'home.delete_confirm': 'Delete \u0027{name}\u0027?',
    'home.delete_yes': 'Delete',
    'home.delete_no': 'Cancel',
    'home.sort_alpha': 'A\u2013Z',
    'home.sort_custom': 'Custom'
  });

  zylI18n.addTranslations('ja', {
    'home.loading': '\u8AAD\u307F\u8FBC\u307F\u4E2D...',
    'home.delete_title': '\u30A2\u30D7\u30EA\u3092\u524A\u9664',
    'home.delete_confirm': '\u0027{name}\u0027\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'home.delete_yes': '\u524A\u9664',
    'home.delete_no': '\u30AD\u30E3\u30F3\u30BB\u30EB',
    'home.sort_alpha': '\u540D\u524D\u9806',
    'home.sort_custom': '\u30AB\u30B9\u30BF\u30E0'
  });

  zylI18n.addTranslations('zh', {
    'home.loading': '\u52A0\u8F7D\u4E2D...',
    'home.delete_title': '\u5220\u9664\u5E94\u7528',
    'home.delete_confirm': '\u5220\u9664\u0027{name}\u0027\uFF1F',
    'home.delete_yes': '\u5220\u9664',
    'home.delete_no': '\u53D6\u6D88',
    'home.sort_alpha': '\u5B57\u6BCD\u987A\u5E8F',
    'home.sort_custom': '\u81EA\u5B9A\u4E49'
  });

  zylI18n.addTranslations('es', {
    'home.loading': 'Cargando...',
    'home.delete_title': 'Eliminar app',
    'home.delete_confirm': '\u00BFEliminar \u0027{name}\u0027?',
    'home.delete_yes': 'Eliminar',
    'home.delete_no': 'Cancelar',
    'home.sort_alpha': 'A\u2013Z',
    'home.sort_custom': 'Personalizado'
  });
})();
