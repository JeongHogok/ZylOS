// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Contacts app translation data
// Scope: contacts.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Contacts 번역 데이터만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'contacts.title': '\uC5F0\uB77D\uCC98',
    'contacts.add': '\uC5F0\uB77D\uCC98 \uCD94\uAC00',
    'contacts.edit': '\uD3B8\uC9D1',
    'contacts.delete': '\uC0AD\uC81C',
    'contacts.save': '\uC800\uC7A5',
    'contacts.cancel': '\uCDE8\uC18C',
    'contacts.search': '\uAC80\uC0C9',
    'contacts.no_contacts': '\uC5F0\uB77D\uCC98\uAC00 \uC5C6\uC2B5\uB2C8\uB2E4',
    'contacts.name': '\uC774\uB984',
    'contacts.phone': '\uC804\uD654\uBC88\uD638',
    'contacts.email': '\uC774\uBA54\uC77C',
    'contacts.call': '\uC804\uD654',
    'contacts.message': '\uBA54\uC2DC\uC9C0',
    'contacts.email_label': '\uC774\uBA54\uC77C',
    'contacts.confirm_delete': '\uC774 \uC5F0\uB77D\uCC98\uB97C \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?',
    'contacts.delete_confirm': '\uC5F0\uB77D\uCC98 \uC0AD\uC81C',
    'contacts.delete_confirm_msg': '\uC774 \uC5F0\uB77D\uCC98\uB97C \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?',
    'contacts.permission_denied': '\uC5F0\uB77D\uCC98 \uAD8C\uD55C\uC774 \uAC70\uBD80\uB418\uC5C8\uC2B5\uB2C8\uB2E4'
  });

  zylI18n.addTranslations('en', {
    'contacts.title': 'Contacts',
    'contacts.add': 'Add Contact',
    'contacts.edit': 'Edit',
    'contacts.delete': 'Delete',
    'contacts.save': 'Save',
    'contacts.cancel': 'Cancel',
    'contacts.search': 'Search',
    'contacts.no_contacts': 'No contacts yet',
    'contacts.name': 'Name',
    'contacts.phone': 'Phone',
    'contacts.email': 'Email',
    'contacts.call': 'Call',
    'contacts.message': 'Message',
    'contacts.confirm_delete': 'Delete this contact?',
    'contacts.delete_confirm': 'Delete Contact',
    'contacts.delete_confirm_msg': 'Are you sure you want to delete this contact?',
    'contacts.email_label': 'Email',
    'contacts.permission_denied': 'Contacts permission denied'
  });

  zylI18n.addTranslations('ja', {
    'contacts.title': '\u9023\u7D61\u5148',
    'contacts.add': '\u9023\u7D61\u5148\u3092\u8FFD\u52A0',
    'contacts.edit': '\u7DE8\u96C6',
    'contacts.delete': '\u524A\u9664',
    'contacts.save': '\u4FDD\u5B58',
    'contacts.cancel': '\u30AD\u30E3\u30F3\u30BB\u30EB',
    'contacts.search': '\u691C\u7D22',
    'contacts.no_contacts': '\u9023\u7D61\u5148\u304C\u3042\u308A\u307E\u305B\u3093',
    'contacts.name': '\u540D\u524D',
    'contacts.phone': '\u96FB\u8A71\u756A\u53F7',
    'contacts.email': '\u30E1\u30FC\u30EB',
    'contacts.call': '\u96FB\u8A71',
    'contacts.message': '\u30E1\u30C3\u30BB\u30FC\u30B8',
    'contacts.confirm_delete': '\u3053\u306E\u9023\u7D61\u5148\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'contacts.delete_confirm': '\u9023\u7D61\u5148\u306E\u524A\u9664',
    'contacts.delete_confirm_msg': '\u3053\u306E\u9023\u7D61\u5148\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F',
    'contacts.email_label': '\u30E1\u30FC\u30EB',
    'contacts.permission_denied': '\u9023\u7D61\u5148\u306E\u6A29\u9650\u304C\u62D2\u5426\u3055\u308C\u307E\u3057\u305F'
  });

  zylI18n.addTranslations('zh', {
    'contacts.title': '\u901A\u8BAF\u5F55',
    'contacts.add': '\u6DFB\u52A0\u8054\u7CFB\u4EBA',
    'contacts.edit': '\u7F16\u8F91',
    'contacts.delete': '\u5220\u9664',
    'contacts.save': '\u4FDD\u5B58',
    'contacts.cancel': '\u53D6\u6D88',
    'contacts.search': '\u641C\u7D22',
    'contacts.no_contacts': '\u6682\u65E0\u8054\u7CFB\u4EBA',
    'contacts.name': '\u59D3\u540D',
    'contacts.phone': '\u7535\u8BDD',
    'contacts.email': '\u90AE\u7BB1',
    'contacts.call': '\u62E8\u6253',
    'contacts.message': '\u77ED\u4FE1',
    'contacts.confirm_delete': '\u5220\u9664\u8BE5\u8054\u7CFB\u4EBA\uFF1F',
    'contacts.delete_confirm': '\u5220\u9664\u8054\u7CFB\u4EBA',
    'contacts.delete_confirm_msg': '\u786E\u5B9A\u8981\u5220\u9664\u8BE5\u8054\u7CFB\u4EBA\u5417\uFF1F',
    'contacts.email_label': '\u90AE\u4EF6',
    'contacts.permission_denied': '\u901A\u8BAF\u5F55\u6743\u9650\u88AB\u62D2\u7EDD'
  });

  zylI18n.addTranslations('es', {
    'contacts.title': 'Contactos',
    'contacts.add': 'Agregar contacto',
    'contacts.edit': 'Editar',
    'contacts.delete': 'Eliminar',
    'contacts.save': 'Guardar',
    'contacts.cancel': 'Cancelar',
    'contacts.search': 'Buscar',
    'contacts.no_contacts': 'No hay contactos',
    'contacts.name': 'Nombre',
    'contacts.phone': 'Tel\u00E9fono',
    'contacts.email': 'Correo',
    'contacts.call': 'Llamar',
    'contacts.message': 'Mensaje',
    'contacts.confirm_delete': '\u00BFEliminar este contacto?',
    'contacts.delete_confirm': 'Eliminar contacto',
    'contacts.delete_confirm_msg': '\u00BFEst\u00E1 seguro de eliminar este contacto?',
    'contacts.email_label': 'Correo',
    'contacts.permission_denied': 'Permiso de contactos denegado'
  });
})();
