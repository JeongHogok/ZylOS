// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Lockscreen app translation data
// Scope: lock.*, qs.*, notif.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Lockscreen 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'lock.swipe': '\uC704\uB85C \uC2A4\uC640\uC774\uD504\uD558\uC5EC \uC7A0\uAE08 \uD574\uC81C',
    'lock.enter_pin': 'PIN\uC744 \uC785\uB825\uD558\uC138\uC694',
    'lock.wrong_pin': '\uC798\uBABB\uB41C PIN\uC785\uB2C8\uB2E4',
    'lock.try_again': '{s}\uCD08 \uD6C4 \uB2E4\uC2DC \uC2DC\uB3C4',
    'lock.cancel': '\uCDE8\uC18C',
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': '\uBB34\uC74C',
    'qs.rotation': '\uD68C\uC804',
    'qs.flashlight': '\uC190\uC804\uB4F1',
    'notif.empty': '\uC54C\uB9BC\uC774 \uC5C6\uC2B5\uB2C8\uB2E4'
  });

  zylI18n.addTranslations('en', {
    'lock.swipe': 'Swipe up to unlock',
    'lock.enter_pin': 'Enter PIN',
    'lock.wrong_pin': 'Wrong PIN',
    'lock.try_again': 'Try again in {s}s',
    'lock.cancel': 'Cancel',
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': 'Silent',
    'qs.rotation': 'Rotation',
    'qs.flashlight': 'Flashlight',
    'notif.empty': 'No notifications'
  });

  zylI18n.addTranslations('ja', {
    'lock.swipe': '\u4E0A\u306B\u30B9\u30EF\u30A4\u30D7\u3057\u3066\u30ED\u30C3\u30AF\u89E3\u9664',
    'lock.enter_pin': 'PIN\u3092\u5165\u529B',
    'lock.wrong_pin': 'PIN\u304C\u9055\u3044\u307E\u3059',
    'lock.try_again': '{s}\u79D2\u5F8C\u306B\u518D\u8A66\u884C',
    'lock.cancel': '\u30AD\u30E3\u30F3\u30BB\u30EB',
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': '\u30DE\u30CA\u30FC',
    'qs.rotation': '\u56DE\u8EE2',
    'qs.flashlight': '\u30E9\u30A4\u30C8',
    'notif.empty': '\u901A\u77E5\u306F\u3042\u308A\u307E\u305B\u3093'
  });

  zylI18n.addTranslations('zh', {
    'lock.swipe': '\u5411\u4E0A\u6ED1\u52A8\u89E3\u9501',
    'lock.enter_pin': '\u8F93\u5165PIN',
    'lock.wrong_pin': 'PIN\u9519\u8BEF',
    'lock.try_again': '{s}\u79D2\u540E\u91CD\u8BD5',
    'lock.cancel': '\u53D6\u6D88',
    'qs.wifi': 'WiFi',
    'qs.bluetooth': '\u84DD\u7259',
    'qs.silent': '\u9759\u97F3',
    'qs.rotation': '\u65CB\u8F6C',
    'qs.flashlight': '\u624B\u7535\u7B52',
    'notif.empty': '\u6CA1\u6709\u901A\u77E5'
  });

  zylI18n.addTranslations('es', {
    'lock.swipe': 'Desliza hacia arriba para desbloquear',
    'lock.enter_pin': 'Ingrese PIN',
    'lock.wrong_pin': 'PIN incorrecto',
    'lock.try_again': 'Int\u00E9ntalo en {s}s',
    'lock.cancel': 'Cancelar',
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': 'Silencio',
    'qs.rotation': 'Rotaci\u00F3n',
    'qs.flashlight': 'Linterna',
    'notif.empty': 'Sin notificaciones'
  });
})();
