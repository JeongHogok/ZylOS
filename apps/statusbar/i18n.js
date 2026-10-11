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
    'qs.airplane': '\uBE44\uD589\uAE30',
    'qs.dnd': '\uBC29\uD574\uAE08\uC9C0',
    'qs.location': '\uC704\uCE58',
    'notif.empty': '\uC54C\uB9BC\uC774 \uC5C6\uC2B5\uB2C8\uB2E4',
    'notif.title': '\uC54C\uB9BC',
    'notif.clear_all': '\uBAA8\uB450 \uC9C0\uC6B0\uAE30',
    'notif.data_warning_title': '\uB370\uC774\uD130 \uACBD\uACE0',
    'notif.data_warning_body': '\uC774\uBC88 \uC8FC\uAE30\uC5D0 \uBAA8\uBC14\uC77C \uB370\uC774\uD130 {used}\uB97C \uC0AC\uC6A9\uD588\uC2B5\uB2C8\uB2E4',
    'notif.data_limit_title': '\uBAA8\uBC14\uC77C \uB370\uC774\uD130 \uC911\uC9C0\uB428',
    'notif.data_limit_body': '\uC124\uC815\uD55C \uB370\uC774\uD130 \uD55C\uB3C4\uC5D0 \uB3C4\uB2EC\uD588\uC2B5\uB2C8\uB2E4. \uC124\uC815\uC5D0\uC11C \uBC14\uAFC0 \uC218 \uC788\uC2B5\uB2C8\uB2E4',
    'notif.time_now': '\uBC29\uAE08',
    'notif.time_min': '{n}\uBD84',
    'notif.time_hour': '{n}\uC2DC\uAC04',
    'notif.time_day': '{n}\uC77C'
  });

  zylI18n.addTranslations('en', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': 'Silent',
    'qs.rotation': 'Rotation',
    'qs.flashlight': 'Flashlight',
    'qs.airplane': 'Airplane',
    'qs.dnd': 'DND',
    'qs.location': 'Location',
    'notif.empty': 'No notifications',
    'notif.title': 'Notifications',
    'notif.clear_all': 'Clear all',
    'notif.data_warning_title': 'Data warning',
    'notif.data_warning_body': 'You have used {used} of mobile data this cycle',
    'notif.data_limit_title': 'Mobile data paused',
    'notif.data_limit_body': 'You reached your data limit. You can change it in Settings',
    'notif.time_now': 'now',
    'notif.time_min': '{n}m',
    'notif.time_hour': '{n}h',
    'notif.time_day': '{n}d'
  });

  zylI18n.addTranslations('ja', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': '\u30DE\u30CA\u30FC',
    'qs.rotation': '\u56DE\u8EE2',
    'qs.flashlight': '\u30E9\u30A4\u30C8',
    'qs.airplane': '\u6A5F\u5185\u30E2\u30FC\u30C9',
    'qs.dnd': '\u304A\u3084\u3059\u307F',
    'qs.location': '\u4F4D\u7F6E',
    'notif.empty': '\u901A\u77E5\u306F\u3042\u308A\u307E\u305B\u3093',
    'notif.title': '\u901A\u77E5',
    'notif.clear_all': '\u3059\u3079\u3066\u524A\u9664',
    'notif.data_warning_title': '\u30C7\u30FC\u30BF\u8B66\u544A',
    'notif.data_warning_body': '\u4ECA\u56DE\u306E\u671F\u9593\u306B\u30E2\u30D0\u30A4\u30EB\u30C7\u30FC\u30BF\u3092{used}\u4F7F\u7528\u3057\u307E\u3057\u305F',
    'notif.data_limit_title': '\u30E2\u30D0\u30A4\u30EB\u30C7\u30FC\u30BF\u3092\u505C\u6B62\u3057\u307E\u3057\u305F',
    'notif.data_limit_body': '\u8A2D\u5B9A\u3057\u305F\u30C7\u30FC\u30BF\u4E0A\u9650\u306B\u9054\u3057\u307E\u3057\u305F\u3002\u8A2D\u5B9A\u3067\u5909\u66F4\u3067\u304D\u307E\u3059',
    'notif.time_now': '\u4ECA',
    'notif.time_min': '{n}\u5206',
    'notif.time_hour': '{n}\u6642\u9593',
    'notif.time_day': '{n}\u65E5'
  });

  zylI18n.addTranslations('zh', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': '\u84DD\u7259',
    'qs.silent': '\u9759\u97F3',
    'qs.rotation': '\u65CB\u8F6C',
    'qs.flashlight': '\u624B\u7535\u7B52',
    'qs.airplane': '\u98DE\u884C\u6A21\u5F0F',
    'qs.dnd': '\u514D\u6253\u6270',
    'qs.location': '\u4F4D\u7F6E',
    'notif.empty': '\u6CA1\u6709\u901A\u77E5',
    'notif.title': '\u901A\u77E5',
    'notif.clear_all': '\u5168\u90E8\u6E05\u9664',
    'notif.data_warning_title': '\u6570\u636E\u8B66\u544A',
    'notif.data_warning_body': '\u672C\u5468\u671F\u5DF2\u4F7F\u7528 {used} \u79FB\u52A8\u6570\u636E',
    'notif.data_limit_title': '\u79FB\u52A8\u6570\u636E\u5DF2\u6682\u505C',
    'notif.data_limit_body': '\u5DF2\u8FBE\u5230\u8BBE\u7F6E\u7684\u6570\u636E\u4E0A\u9650\uFF0C\u53EF\u5728\u8BBE\u7F6E\u4E2D\u66F4\u6539',
    'notif.time_now': '\u521A\u521A',
    'notif.time_min': '{n}\u5206\u949F',
    'notif.time_hour': '{n}\u5C0F\u65F6',
    'notif.time_day': '{n}\u5929'
  });

  zylI18n.addTranslations('es', {
    'qs.wifi': 'WiFi',
    'qs.bluetooth': 'BT',
    'qs.silent': 'Silencio',
    'qs.rotation': 'Rotaci\u00F3n',
    'qs.flashlight': 'Linterna',
    'qs.airplane': 'Avi\u00F3n',
    'qs.dnd': 'No molestar',
    'qs.location': 'Ubicaci\u00F3n',
    'notif.empty': 'Sin notificaciones',
    'notif.title': 'Notificaciones',
    'notif.clear_all': 'Borrar todo',
    'notif.data_warning_title': 'Aviso de datos',
    'notif.data_warning_body': 'Has usado {used} de datos m\u00F3viles en este ciclo',
    'notif.data_limit_title': 'Datos m\u00F3viles en pausa',
    'notif.data_limit_body': 'Alcanzaste tu l\u00EDmite de datos. Puedes cambiarlo en Ajustes',
    'notif.time_now': 'ahora',
    'notif.time_min': '{n}min',
    'notif.time_hour': '{n}h',
    'notif.time_day': '{n}d'
  });
})();
