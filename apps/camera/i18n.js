// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Camera app translation data
// Scope: camera.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Camera 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'camera.recording': '\uB179\uD654 \uC911',
    'camera.capture': '\uCD2C\uC601',
    'camera.photo': '\uC0AC\uC9C4',
    'camera.video': '\uBE44\uB514\uC624',
    'camera.portrait': '\uC778\uBB3C',
    'camera.pano': '\uD30C\uB178\uB77C\uB9C8',
    'camera.flash_off': '\uD50C\uB798\uC2DC \uB044\uAE30',
    'camera.timer_off': '\uD0C0\uC774\uBA38 \uB044\uAE30',
    'camera.retake': '\uB2E4\uC2DC \uCC0D\uAE30',
    'camera.save': '\uC800\uC7A5'
  });

  zylI18n.addTranslations('en', {
    'camera.recording': 'Recording',
    'camera.capture': 'Capture',
    'camera.photo': 'Photo',
    'camera.video': 'Video',
    'camera.portrait': 'Portrait',
    'camera.pano': 'Pano',
    'camera.flash_off': 'Flash Off',
    'camera.timer_off': 'Timer Off',
    'camera.retake': 'Retake',
    'camera.save': 'Save'
  });

  zylI18n.addTranslations('ja', {
    'camera.recording': '\u9332\u753B\u4E2D',
    'camera.capture': '\u64AE\u5F71',
    'camera.photo': '\u5199\u771F',
    'camera.video': '\u30D3\u30C7\u30AA',
    'camera.portrait': '\u30DD\u30FC\u30C8\u30EC\u30FC\u30C8',
    'camera.pano': '\u30D1\u30CE\u30E9\u30DE',
    'camera.flash_off': '\u30D5\u30E9\u30C3\u30B7\u30E5\u30AA\u30D5',
    'camera.timer_off': '\u30BF\u30A4\u30DE\u30FC\u30AA\u30D5',
    'camera.retake': '\u64AE\u308A\u76F4\u3057',
    'camera.save': '\u4FDD\u5B58'
  });

  zylI18n.addTranslations('zh', {
    'camera.recording': '\u5F55\u5236\u4E2D',
    'camera.capture': '\u62CD\u6444',
    'camera.photo': '\u62CD\u7167',
    'camera.video': '\u89C6\u9891',
    'camera.portrait': '\u4EBA\u50CF',
    'camera.pano': '\u5168\u666F',
    'camera.flash_off': '\u5173\u95ED\u95EA\u5149\u706F',
    'camera.timer_off': '\u5173\u95ED\u5B9A\u65F6\u5668',
    'camera.retake': '\u91CD\u62CD',
    'camera.save': '\u4FDD\u5B58'
  });

  zylI18n.addTranslations('es', {
    'camera.recording': 'Grabando',
    'camera.capture': 'Capturar',
    'camera.photo': 'Foto',
    'camera.video': 'Video',
    'camera.portrait': 'Retrato',
    'camera.pano': 'Panor\u00E1mica',
    'camera.flash_off': 'Flash apagado',
    'camera.timer_off': 'Temporizador apagado',
    'camera.retake': 'Repetir',
    'camera.save': 'Guardar'
  });
})();
