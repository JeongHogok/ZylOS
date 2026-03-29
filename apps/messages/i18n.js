// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Messages app translation data
// Scope: messages.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Messages 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'messages.title': '\uBA54\uC2DC\uC9C0',
    'messages.new': '\uC0C8 \uBA54\uC2DC\uC9C0',
    'messages.send': '\uBCF4\uB0B4\uAE30',
    'messages.type_message': '\uBA54\uC2DC\uC9C0 \uC785\uB825',
    'messages.no_messages': '\uBA54\uC2DC\uC9C0 \uC5C6\uC74C',
    'messages.no_threads': '\uB300\uD654 \uC5C6\uC74C',
    'messages.yesterday': '\uC5B4\uC81C',
    'messages.just_now': '\uBC29\uAE08',
    'messages.delete': '\uC0AD\uC81C',
    'messages.search': '\uC5F0\uB77D\uCC98 \uAC80\uC0C9 \uB610\uB294 \uBC88\uD638 \uC785\uB825'
  });

  zylI18n.addTranslations('en', {
    'messages.title': 'Messages',
    'messages.new': 'New Message',
    'messages.send': 'Send',
    'messages.type_message': 'Type a message',
    'messages.no_messages': 'No messages',
    'messages.no_threads': 'No conversations',
    'messages.yesterday': 'Yesterday',
    'messages.just_now': 'Just now',
    'messages.delete': 'Delete',
    'messages.search': 'Search contacts or enter number'
  });

  zylI18n.addTranslations('ja', {
    'messages.title': '\u30E1\u30C3\u30BB\u30FC\u30B8',
    'messages.new': '\u65B0\u898F\u30E1\u30C3\u30BB\u30FC\u30B8',
    'messages.send': '\u9001\u4FE1',
    'messages.type_message': '\u30E1\u30C3\u30BB\u30FC\u30B8\u3092\u5165\u529B',
    'messages.no_messages': '\u30E1\u30C3\u30BB\u30FC\u30B8\u306A\u3057',
    'messages.no_threads': '\u4F1A\u8A71\u306A\u3057',
    'messages.yesterday': '\u6628\u65E5',
    'messages.just_now': '\u305F\u3063\u305F\u4ECA',
    'messages.delete': '\u524A\u9664',
    'messages.search': '\u9023\u7D61\u5148\u691C\u7D22\u307E\u305F\u306F\u756A\u53F7\u5165\u529B'
  });

  zylI18n.addTranslations('zh', {
    'messages.title': '\u77ED\u4FE1',
    'messages.new': '\u65B0\u4FE1\u606F',
    'messages.send': '\u53D1\u9001',
    'messages.type_message': '\u8F93\u5165\u6D88\u606F',
    'messages.no_messages': '\u65E0\u6D88\u606F',
    'messages.no_threads': '\u65E0\u5BF9\u8BDD',
    'messages.yesterday': '\u6628\u5929',
    'messages.just_now': '\u521A\u521A',
    'messages.delete': '\u5220\u9664',
    'messages.search': '\u641C\u7D22\u8054\u7CFB\u4EBA\u6216\u8F93\u5165\u53F7\u7801'
  });

  zylI18n.addTranslations('es', {
    'messages.title': 'Mensajes',
    'messages.new': 'Nuevo mensaje',
    'messages.send': 'Enviar',
    'messages.type_message': 'Escribe un mensaje',
    'messages.no_messages': 'Sin mensajes',
    'messages.no_threads': 'Sin conversaciones',
    'messages.yesterday': 'Ayer',
    'messages.just_now': 'Ahora',
    'messages.delete': 'Eliminar',
    'messages.search': 'Buscar contactos o ingresar n\u00FAmero'
  });
})();
