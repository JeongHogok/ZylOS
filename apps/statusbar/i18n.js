// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Statusbar app translation data
// Scope: qs.*, notif.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Statusbar translation data only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': '\uBB34\uC74C',
    'qs.rotation': '\uD68C\uC804',
    'qs.flashlight': '\uC190\uC804\uB4F1',
    'notif.empty': '\uC54C\uB9BC\uC774 \uC5C6\uC2B5\uB2C8\uB2E4'
  });

  zylI18n.addTranslations('en', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': 'Silent',
    'qs.rotation': 'Rotate',
    'qs.flashlight': 'Flashlight',
    'notif.empty': 'No notifications'
  });

  zylI18n.addTranslations('ja', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': '\u30DE\u30CA\u30FC',
    'qs.rotation': '\u56DE\u8EE2',
    'qs.flashlight': '\u30E9\u30A4\u30C8',
    'notif.empty': '\u901A\u77E5\u306F\u3042\u308A\u307E\u305B\u3093'
  });

  zylI18n.addTranslations('zh', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': '\u84DD\u7259',
    'qs.silent': '\u9759\u97F3',
    'qs.rotation': '\u65CB\u8F6C',
    'qs.flashlight': '\u624B\u7535\u7B52',
    'notif.empty': '\u6CA1\u6709\u901A\u77E5'
  });

  zylI18n.addTranslations('es', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': 'Silencio',
    'qs.rotation': 'Rotaci\u00F3n',
    'qs.flashlight': 'Linterna',
    'notif.empty': 'Sin notificaciones'
  });
})();
