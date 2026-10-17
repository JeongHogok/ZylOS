// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Recovery screen translation data
// Scope: recovery.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — recovery translation data only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'recovery.title': '\uC800\uC7A5\uC18C\uB97C \uC5F4 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4',
    'recovery.body': '\uAE30\uAE30\uC5D0 \uC800\uC7A5\uB41C \uB370\uC774\uD130\uAC00 \uC190\uC0C1\uB418\uC5C8\uC744 \uC218 \uC788\uC2B5\uB2C8\uB2E4. \uB2E4\uC2DC \uC2DC\uC791\uD558\uBA74 \uB2E4\uC2DC \uC5F4\uC5B4 \uBD05\uB2C8\uB2E4. \uACC4\uC18D \uC774 \uD654\uBA74\uC774 \uB098\uC624\uBA74 \uAE30\uAE30\uB97C \uCD08\uAE30\uD654\uD560 \uC218 \uC788\uC2B5\uB2C8\uB2E4.',
    'recovery.restart': '\uB2E4\uC2DC \uC2DC\uC791',
    'recovery.reset': '\uAE30\uAE30 \uCD08\uAE30\uD654',
    'recovery.confirm_title': '\uBAA8\uB4E0 \uB370\uC774\uD130\uB97C \uC9C0\uC6B8\uAE4C\uC694?',
    'recovery.confirm_body': '\uC571\uACFC \uADF8 \uB370\uC774\uD130, \uC0AC\uC9C4\uACFC \uD30C\uC77C, \uC5F0\uB77D\uCC98, \uBA54\uC2DC\uC9C0, \uC800\uC7A5\uB41C \uBE44\uBC00\uBC88\uD638, \uC124\uC815\uC774 \uBAA8\uB450 \uC9C0\uC6CC\uC9D1\uB2C8\uB2E4. \uB418\uB3CC\uB9B4 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4.',
    'recovery.erase': '\uBAA8\uB450 \uC9C0\uC6B0\uAE30',
    'recovery.cancel': '\uCDE8\uC18C',
    'recovery.erasing': '\uC9C0\uC6B0\uB294 \uC911\uC785\uB2C8\uB2E4. \uB05D\uB098\uBA74 \uB2E4\uC2DC \uC2DC\uC791\uD569\uB2C8\uB2E4.',
    'recovery.failed': '\uC9C0\uC6B0\uC9C0 \uBABB\uD588\uC2B5\uB2C8\uB2E4. \uB2E4\uC2DC \uC2DC\uC791\uD55C \uB4A4 \uB2E4\uC2DC \uC2DC\uB3C4\uD558\uC138\uC694.'
  });

  zylI18n.addTranslations('en', {
    'recovery.title': 'Your data can\'t be opened',
    'recovery.body': 'The data stored on this device may be damaged. Restarting tries to open it again. If you keep seeing this screen, you can reset the device.',
    'recovery.restart': 'Restart',
    'recovery.reset': 'Reset device',
    'recovery.confirm_title': 'Erase all data?',
    'recovery.confirm_body': 'Apps and their data, photos and files, contacts, messages, saved passwords and settings will all be erased. This can\'t be undone.',
    'recovery.erase': 'Erase everything',
    'recovery.cancel': 'Cancel',
    'recovery.erasing': 'Erasing. The device restarts when it\'s done.',
    'recovery.failed': 'Couldn\'t erase. Restart and try again.'
  });

  zylI18n.addTranslations('ja', {
    'recovery.title': '\u30C7\u30FC\u30BF\u3092\u958B\u3051\u307E\u305B\u3093',
    'recovery.body': '\u3053\u306E\u7AEF\u672B\u306B\u4FDD\u5B58\u3055\u308C\u305F\u30C7\u30FC\u30BF\u304C\u7834\u640D\u3057\u3066\u3044\u308B\u53EF\u80FD\u6027\u304C\u3042\u308A\u307E\u3059\u3002\u518D\u8D77\u52D5\u3059\u308B\u3068\u3082\u3046\u4E00\u5EA6\u958B\u3053\u3046\u3068\u3057\u307E\u3059\u3002\u3053\u306E\u753B\u9762\u304C\u7D9A\u304F\u5834\u5408\u306F\u3001\u7AEF\u672B\u3092\u30EA\u30BB\u30C3\u30C8\u3067\u304D\u307E\u3059\u3002',
    'recovery.restart': '\u518D\u8D77\u52D5',
    'recovery.reset': '\u7AEF\u672B\u3092\u30EA\u30BB\u30C3\u30C8',
    'recovery.confirm_title': '\u3059\u3079\u3066\u306E\u30C7\u30FC\u30BF\u3092\u6D88\u53BB\u3057\u307E\u3059\u304B\uFF1F',
    'recovery.confirm_body': '\u30A2\u30D7\u30EA\u3068\u305D\u306E\u30C7\u30FC\u30BF\u3001\u5199\u771F\u3068\u30D5\u30A1\u30A4\u30EB\u3001\u9023\u7D61\u5148\u3001\u30E1\u30C3\u30BB\u30FC\u30B8\u3001\u4FDD\u5B58\u3057\u305F\u30D1\u30B9\u30EF\u30FC\u30C9\u3001\u8A2D\u5B9A\u304C\u3059\u3079\u3066\u6D88\u53BB\u3055\u308C\u307E\u3059\u3002\u5143\u306B\u623B\u3059\u3053\u3068\u306F\u3067\u304D\u307E\u305B\u3093\u3002',
    'recovery.erase': '\u3059\u3079\u3066\u6D88\u53BB',
    'recovery.cancel': '\u30AD\u30E3\u30F3\u30BB\u30EB',
    'recovery.erasing': '\u6D88\u53BB\u3057\u3066\u3044\u307E\u3059\u3002\u7D42\u308F\u308B\u3068\u518D\u8D77\u52D5\u3057\u307E\u3059\u3002',
    'recovery.failed': '\u6D88\u53BB\u3067\u304D\u307E\u305B\u3093\u3067\u3057\u305F\u3002\u518D\u8D77\u52D5\u3057\u3066\u304B\u3089\u3082\u3046\u4E00\u5EA6\u304A\u8A66\u3057\u304F\u3060\u3055\u3044\u3002'
  });

  zylI18n.addTranslations('zh', {
    'recovery.title': '\u65E0\u6CD5\u6253\u5F00\u60A8\u7684\u6570\u636E',
    'recovery.body': '\u6B64\u8BBE\u5907\u4E0A\u5B58\u50A8\u7684\u6570\u636E\u53EF\u80FD\u5DF2\u635F\u574F\u3002\u91CD\u65B0\u542F\u52A8\u5C06\u518D\u6B21\u5C1D\u8BD5\u6253\u5F00\u3002\u5982\u679C\u4E00\u76F4\u770B\u5230\u6B64\u5C4F\u5E55\uFF0C\u53EF\u4EE5\u91CD\u7F6E\u8BBE\u5907\u3002',
    'recovery.restart': '\u91CD\u65B0\u542F\u52A8',
    'recovery.reset': '\u91CD\u7F6E\u8BBE\u5907',
    'recovery.confirm_title': '\u8981\u6E05\u9664\u6240\u6709\u6570\u636E\u5417\uFF1F',
    'recovery.confirm_body': '\u5E94\u7528\u53CA\u5176\u6570\u636E\u3001\u7167\u7247\u548C\u6587\u4EF6\u3001\u8054\u7CFB\u4EBA\u3001\u4FE1\u606F\u3001\u5DF2\u4FDD\u5B58\u7684\u5BC6\u7801\u548C\u8BBE\u7F6E\u90FD\u5C06\u88AB\u6E05\u9664\u3002\u6B64\u64CD\u4F5C\u65E0\u6CD5\u64A4\u6D88\u3002',
    'recovery.erase': '\u5168\u90E8\u6E05\u9664',
    'recovery.cancel': '\u53D6\u6D88',
    'recovery.erasing': '\u6B63\u5728\u6E05\u9664\u3002\u5B8C\u6210\u540E\u8BBE\u5907\u5C06\u91CD\u65B0\u542F\u52A8\u3002',
    'recovery.failed': '\u65E0\u6CD5\u6E05\u9664\u3002\u8BF7\u91CD\u65B0\u542F\u52A8\u540E\u91CD\u8BD5\u3002'
  });

  zylI18n.addTranslations('es', {
    'recovery.title': 'No se pueden abrir tus datos',
    'recovery.body': 'Es posible que los datos guardados en este dispositivo est\u00E9n da\u00F1ados. Al reiniciar se intentar\u00E1 abrirlos de nuevo. Si sigues viendo esta pantalla, puedes restablecer el dispositivo.',
    'recovery.restart': 'Reiniciar',
    'recovery.reset': 'Restablecer dispositivo',
    'recovery.confirm_title': '\u00BFBorrar todos los datos?',
    'recovery.confirm_body': 'Se borrar\u00E1n las apps y sus datos, las fotos y archivos, los contactos, los mensajes, las contrase\u00F1as guardadas y los ajustes. No se puede deshacer.',
    'recovery.erase': 'Borrar todo',
    'recovery.cancel': 'Cancelar',
    'recovery.erasing': 'Borrando. El dispositivo se reiniciar\u00E1 al terminar.',
    'recovery.failed': 'No se pudo borrar. Reinicia e int\u00E9ntalo de nuevo.'
  });
})();
