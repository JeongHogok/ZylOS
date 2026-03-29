// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Phone app translation data
// Scope: phone.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Phone 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'phone.title': '전화',
    'phone.keypad': '키패드',
    'phone.recents': '최근기록',
    'phone.contacts_tab': '연락처',
    'phone.call': '통화',
    'phone.end_call': '통화 종료',
    'phone.mute': '음소거',
    'phone.speaker': '스피커',
    'phone.no_recents': '최근 통화 기록 없음',
    'phone.incoming': '수신',
    'phone.outgoing': '발신',
    'phone.missed': '부재중',
    'phone.calling': '통화 중...',
    'phone.dialing': '발신 중...'
  });

  zylI18n.addTranslations('en', {
    'phone.title': 'Phone',
    'phone.keypad': 'Keypad',
    'phone.recents': 'Recents',
    'phone.contacts_tab': 'Contacts',
    'phone.call': 'Call',
    'phone.end_call': 'End Call',
    'phone.mute': 'Mute',
    'phone.speaker': 'Speaker',
    'phone.no_recents': 'No recent calls',
    'phone.incoming': 'Incoming',
    'phone.outgoing': 'Outgoing',
    'phone.missed': 'Missed',
    'phone.calling': 'Calling...',
    'phone.dialing': 'Dialing...'
  });

  zylI18n.addTranslations('ja', {
    'phone.title': '\u96FB\u8A71',
    'phone.keypad': '\u30AD\u30FC\u30D1\u30C3\u30C9',
    'phone.recents': '\u5C65\u6B74',
    'phone.contacts_tab': '\u9023\u7D61\u5148',
    'phone.call': '\u767A\u4FE1',
    'phone.end_call': '\u7D42\u4E86',
    'phone.mute': '\u30DF\u30E5\u30FC\u30C8',
    'phone.speaker': '\u30B9\u30D4\u30FC\u30AB\u30FC',
    'phone.no_recents': '\u5C65\u6B74\u306A\u3057',
    'phone.incoming': '\u7740\u4FE1',
    'phone.outgoing': '\u767A\u4FE1',
    'phone.missed': '\u4E0D\u5728\u7740\u4FE1',
    'phone.calling': '\u901A\u8A71\u4E2D...',
    'phone.dialing': '\u767A\u4FE1\u4E2D...'
  });

  zylI18n.addTranslations('zh', {
    'phone.title': '\u7535\u8BDD',
    'phone.keypad': '\u952E\u76D8',
    'phone.recents': '\u6700\u8FD1',
    'phone.contacts_tab': '\u8054\u7CFB\u4EBA',
    'phone.call': '\u547C\u53EB',
    'phone.end_call': '\u7ED3\u675F\u901A\u8BDD',
    'phone.mute': '\u9759\u97F3',
    'phone.speaker': '\u6269\u97F3\u5668',
    'phone.no_recents': '\u65E0\u901A\u8BDD\u8BB0\u5F55',
    'phone.incoming': '\u6765\u7535',
    'phone.outgoing': '\u53BB\u7535',
    'phone.missed': '\u672A\u63A5',
    'phone.calling': '\u901A\u8BDD\u4E2D...',
    'phone.dialing': '\u62E8\u53F7\u4E2D...'
  });

  zylI18n.addTranslations('es', {
    'phone.title': 'Tel\u00E9fono',
    'phone.keypad': 'Teclado',
    'phone.recents': 'Recientes',
    'phone.contacts_tab': 'Contactos',
    'phone.call': 'Llamar',
    'phone.end_call': 'Finalizar',
    'phone.mute': 'Silenciar',
    'phone.speaker': 'Altavoz',
    'phone.no_recents': 'Sin llamadas recientes',
    'phone.incoming': 'Entrante',
    'phone.outgoing': 'Saliente',
    'phone.missed': 'Perdida',
    'phone.calling': 'Llamando...',
    'phone.dialing': 'Marcando...'
  });
})();
