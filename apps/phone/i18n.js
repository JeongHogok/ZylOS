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
    'phone.dialing': '발신 중...',
    'phone.just_now': '방금',
    'phone.favorites': '즐겨찾기',
    'phone.no_favorites': '즐겨찾기 없음',
    'phone.delete_log': '삭제',
    'phone.confirm_delete_log': '이 통화 기록을 삭제하시겠습니까?',
    'phone.dtmf': 'DTMF',
    'phone.add_favorite': '즐겨찾기 추가',
    'phone.remove_favorite': '즐겨찾기 해제',
    'phone.cancel': '취소'
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
    'phone.dialing': 'Dialing...',
    'phone.just_now': 'Just now',
    'phone.favorites': 'Favorites',
    'phone.no_favorites': 'No favorites',
    'phone.delete_log': 'Delete',
    'phone.confirm_delete_log': 'Delete this call log entry?',
    'phone.dtmf': 'DTMF',
    'phone.add_favorite': 'Add to favorites',
    'phone.remove_favorite': 'Remove from favorites',
    'phone.cancel': 'Cancel'
  });

  zylI18n.addTranslations('ja', {
    'phone.title': '電話',
    'phone.keypad': 'キーパッド',
    'phone.recents': '履歴',
    'phone.contacts_tab': '連絡先',
    'phone.call': '発信',
    'phone.end_call': '終了',
    'phone.mute': 'ミュート',
    'phone.speaker': 'スピーカー',
    'phone.no_recents': '履歴なし',
    'phone.incoming': '着信',
    'phone.outgoing': '発信',
    'phone.missed': '不在着信',
    'phone.calling': '通話中...',
    'phone.dialing': '発信中...',
    'phone.just_now': 'たった今',
    'phone.favorites': 'お気に入り',
    'phone.no_favorites': 'お気に入りなし',
    'phone.delete_log': '削除',
    'phone.confirm_delete_log': 'この通話履歴を削除しますか？',
    'phone.dtmf': 'DTMF',
    'phone.add_favorite': 'お気に入りに追加',
    'phone.remove_favorite': 'お気に入りから削除',
    'phone.cancel': 'キャンセル'
  });

  zylI18n.addTranslations('zh', {
    'phone.title': '电话',
    'phone.keypad': '键盘',
    'phone.recents': '最近',
    'phone.contacts_tab': '联系人',
    'phone.call': '呼叫',
    'phone.end_call': '结束通话',
    'phone.mute': '静音',
    'phone.speaker': '扩音器',
    'phone.no_recents': '无通话记录',
    'phone.incoming': '来电',
    'phone.outgoing': '去电',
    'phone.missed': '未接',
    'phone.calling': '通话中...',
    'phone.dialing': '拨号中...',
    'phone.just_now': '刚刚',
    'phone.favorites': '收藏',
    'phone.no_favorites': '无收藏联系人',
    'phone.delete_log': '删除',
    'phone.confirm_delete_log': '确定删除此通话记录？',
    'phone.dtmf': 'DTMF',
    'phone.add_favorite': '添加到收藏',
    'phone.remove_favorite': '从收藏移除',
    'phone.cancel': '取消'
  });

  zylI18n.addTranslations('es', {
    'phone.title': 'Tel\u00e9fono',
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
    'phone.dialing': 'Marcando...',
    'phone.just_now': 'Ahora',
    'phone.favorites': 'Favoritos',
    'phone.no_favorites': 'Sin favoritos',
    'phone.delete_log': 'Eliminar',
    'phone.confirm_delete_log': '\u00bfEliminar este registro de llamada?',
    'phone.dtmf': 'DTMF',
    'phone.add_favorite': 'Agregar a favoritos',
    'phone.remove_favorite': 'Quitar de favoritos',
    'phone.cancel': 'Cancelar'
  });
})();
