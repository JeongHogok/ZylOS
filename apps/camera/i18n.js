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
    'camera.access_denied': '\uCE74\uBA54\uB77C \uC811\uADFC\uC774 \uAC70\uBD80\uB418\uC5C8\uC2B5\uB2C8\uB2E4',
    'camera.photo': '\uC0AC\uC9C4',
    'camera.video': '\uBE44\uB514\uC624',
    'camera.portrait': '\uC778\uBB3C',
    'camera.pano': '\uD30C\uB178\uB77C\uB9C8',
    'camera.flash_off': '\uD50C\uB798\uC2DC \uB044\uAE30',
    'camera.flash_on': '\uCF1C\uAE30',
    'camera.flash_auto': '\uC790\uB3D9',
    'camera.timer_off': '\uD0C0\uC774\uBA38 \uB044\uAE30',
    'camera.retake': '\uB2E4\uC2DC \uCC0D\uAE30',
    'camera.save': '\uC800\uC7A5'
  });

  zylI18n.addTranslations('en', {
    'camera.recording': 'Recording',
    'camera.capture': 'Capture',
    'camera.access_denied': 'Camera access denied',
    'camera.photo': 'Photo',
    'camera.video': 'Video',
    'camera.portrait': 'Portrait',
    'camera.pano': 'Pano',
    'camera.flash_off': 'Flash Off',
    'camera.flash_on': 'On',
    'camera.flash_auto': 'Auto',
    'camera.timer_off': 'Timer Off',
    'camera.retake': 'Retake',
    'camera.save': 'Save'
  });

  zylI18n.addTranslations('ja', {
    'camera.recording': '\u9332\u753B\u4E2D',
    'camera.capture': '\u64AE\u5F71',
    'camera.access_denied': '\u30AB\u30E1\u30E9\u3078\u306E\u30A2\u30AF\u30BB\u30B9\u304C\u62D2\u5426\u3055\u308C\u307E\u3057\u305F',
    'camera.photo': '\u5199\u771F',
    'camera.video': '\u30D3\u30C7\u30AA',
    'camera.portrait': '\u30DD\u30FC\u30C8\u30EC\u30FC\u30C8',
    'camera.pano': '\u30D1\u30CE\u30E9\u30DE',
    'camera.flash_off': '\u30D5\u30E9\u30C3\u30B7\u30E5\u30AA\u30D5',
    'camera.flash_on': '\u30AA\u30F3',
    'camera.flash_auto': '\u81EA\u52D5',
    'camera.timer_off': '\u30BF\u30A4\u30DE\u30FC\u30AA\u30D5',
    'camera.retake': '\u64AE\u308A\u76F4\u3057',
    'camera.save': '\u4FDD\u5B58'
  });

  zylI18n.addTranslations('zh', {
    'camera.recording': '\u5F55\u5236\u4E2D',
    'camera.capture': '\u62CD\u6444',
    'camera.access_denied': '\u76F8\u673A\u8BBF\u95EE\u88AB\u62D2\u7EDD',
    'camera.photo': '\u62CD\u7167',
    'camera.video': '\u89C6\u9891',
    'camera.portrait': '\u4EBA\u50CF',
    'camera.pano': '\u5168\u666F',
    'camera.flash_off': '\u5173\u95ED\u95EA\u5149\u706F',
    'camera.flash_on': '\u5F00',
    'camera.flash_auto': '\u81EA\u52A8',
    'camera.timer_off': '\u5173\u95ED\u5B9A\u65F6\u5668',
    'camera.retake': '\u91CD\u62CD',
    'camera.save': '\u4FDD\u5B58'
  });

  zylI18n.addTranslations('es', {
    'camera.recording': 'Grabando',
    'camera.capture': 'Capturar',
    'camera.access_denied': 'Acceso a la c\u00E1mara denegado',
    'camera.photo': 'Foto',
    'camera.video': 'Video',
    'camera.portrait': 'Retrato',
    'camera.pano': 'Panor\u00E1mica',
    'camera.flash_off': 'Flash apagado',
    'camera.flash_on': 'Encendido',
    'camera.flash_auto': 'Auto',
    'camera.timer_off': 'Temporizador apagado',
    'camera.retake': 'Repetir',
    'camera.save': 'Guardar'
  });
})();
