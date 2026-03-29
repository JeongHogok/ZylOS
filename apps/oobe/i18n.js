// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: OOBE app translation data
// Scope: oobe.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — OOBE 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'oobe.welcome': '\uD658\uC601\uD569\uB2C8\uB2E4',
    'oobe.welcome_desc': '\uC0C8\uB85C\uC6B4 \uBAA8\uBC14\uC77C \uACBD\uD5D8\uC744 \uC2DC\uC791\uD558\uC138\uC694',
    'oobe.get_started': '\uC2DC\uC791\uD558\uAE30',
    'oobe.language_title': '\uC5B8\uC5B4 \uC120\uD0DD',
    'oobe.next': '\uB2E4\uC74C',
    'oobe.back': '\uB4A4\uB85C',
    'oobe.wifi_title': 'Wi-Fi \uC5F0\uACB0',
    'oobe.wifi_searching': '\uB124\uD2B8\uC6CC\uD06C \uAC80\uC0C9 \uC911...',
    'oobe.skip': '\uAC74\uB108\uB6F0\uAE30',
    'oobe.pin_title': '\uD654\uBA74 \uC7A0\uAE08 \uC124\uC815',
    'oobe.pin_desc': '4\uC790\uB9AC PIN\uC744 \uC124\uC815\uD558\uC138\uC694',
    'oobe.pin_set': '\uC124\uC815',
    'oobe.pin_later': '\uB098\uC911\uC5D0',
    'oobe.terms_title': '\uC774\uC6A9 \uC57D\uAD00',
    'oobe.terms_body': 'Zyl OS\uB294 MIT \uB77C\uC774\uC120\uC2A4\uB85C \uBC30\uD3EC\uB418\uB294 \uC624\uD508\uC18C\uC2A4 \uBAA8\uBC14\uC77C \uC6B4\uC601\uCCB4\uC81C\uC785\uB2C8\uB2E4. \uBCF8 \uC18C\uD504\uD2B8\uC6E8\uC5B4\uB294 \uC5B4\uB5A0\uD55C \uBCF4\uC99D\uB3C4 \uC5C6\uC774 "\uC788\uB294 \uADF8\uB300\uB85C" \uC81C\uACF5\uB429\uB2C8\uB2E4.',
    'oobe.terms_agree': '\uC774\uC6A9 \uC57D\uAD00\uC5D0 \uB3D9\uC758\uD569\uB2C8\uB2E4',
    'oobe.agree': '\uB3D9\uC758',
    'oobe.complete_title': '\uC124\uC815 \uC644\uB8CC',
    'oobe.complete_desc': 'Zyl OS\uB97C \uC990\uACA8\uBCF4\uC138\uC694!',
    'oobe.start': '\uC2DC\uC791'
  });

  zylI18n.addTranslations('en', {
    'oobe.welcome': 'Welcome',
    'oobe.welcome_desc': 'Start your new mobile experience',
    'oobe.get_started': 'Get Started',
    'oobe.language_title': 'Select Language',
    'oobe.next': 'Next',
    'oobe.back': 'Back',
    'oobe.wifi_title': 'Wi-Fi Connection',
    'oobe.wifi_searching': 'Searching for networks...',
    'oobe.skip': 'Skip',
    'oobe.pin_title': 'Screen Lock Setup',
    'oobe.pin_desc': 'Set a 4-digit PIN',
    'oobe.pin_set': 'Set',
    'oobe.pin_later': 'Later',
    'oobe.terms_title': 'Terms of Service',
    'oobe.terms_body': 'Zyl OS is an open-source mobile operating system distributed under the MIT License. This software is provided "as is" without warranty of any kind.',
    'oobe.terms_agree': 'I agree to the Terms of Service',
    'oobe.agree': 'Agree',
    'oobe.complete_title': 'Setup Complete',
    'oobe.complete_desc': 'Enjoy Zyl OS!',
    'oobe.start': 'Start'
  });

  zylI18n.addTranslations('ja', {
    'oobe.welcome': '\u3088\u3046\u3053\u305D',
    'oobe.welcome_desc': '\u65B0\u3057\u3044\u30E2\u30D0\u30A4\u30EB\u4F53\u9A13\u3092\u59CB\u3081\u307E\u3057\u3087\u3046',
    'oobe.get_started': '\u306F\u3058\u3081\u308B',
    'oobe.language_title': '\u8A00\u8A9E\u3092\u9078\u629E',
    'oobe.next': '\u6B21\u3078',
    'oobe.back': '\u623B\u308B',
    'oobe.wifi_title': 'Wi-Fi\u63A5\u7D9A',
    'oobe.wifi_searching': '\u30CD\u30C3\u30C8\u30EF\u30FC\u30AF\u3092\u691C\u7D22\u4E2D...',
    'oobe.skip': '\u30B9\u30AD\u30C3\u30D7',
    'oobe.pin_title': '\u753B\u9762\u30ED\u30C3\u30AF\u8A2D\u5B9A',
    'oobe.pin_desc': '4\u6841\u306EPIN\u3092\u8A2D\u5B9A\u3057\u3066\u304F\u3060\u3055\u3044',
    'oobe.pin_set': '\u8A2D\u5B9A',
    'oobe.pin_later': '\u5F8C\u3067',
    'oobe.terms_title': '\u5229\u7528\u898F\u7D04',
    'oobe.terms_body': 'Zyl OS\u306FMIT\u30E9\u30A4\u30BB\u30F3\u30B9\u3067\u914D\u5E03\u3055\u308C\u308B\u30AA\u30FC\u30D7\u30F3\u30BD\u30FC\u30B9\u30E2\u30D0\u30A4\u30EBOS\u3067\u3059\u3002\u672C\u30BD\u30D5\u30C8\u30A6\u30A7\u30A2\u306F\u3044\u304B\u306A\u308B\u4FDD\u8A3C\u3082\u306A\u304F\u300C\u73FE\u72B6\u306E\u307E\u307E\u300D\u63D0\u4F9B\u3055\u308C\u307E\u3059\u3002',
    'oobe.terms_agree': '\u5229\u7528\u898F\u7D04\u306B\u540C\u610F\u3057\u307E\u3059',
    'oobe.agree': '\u540C\u610F',
    'oobe.complete_title': '\u30BB\u30C3\u30C8\u30A2\u30C3\u30D7\u5B8C\u4E86',
    'oobe.complete_desc': 'Zyl OS\u3092\u304A\u697D\u3057\u307F\u304F\u3060\u3055\u3044\uFF01',
    'oobe.start': '\u958B\u59CB'
  });

  zylI18n.addTranslations('zh', {
    'oobe.welcome': '\u6B22\u8FCE',
    'oobe.welcome_desc': '\u5F00\u542F\u5168\u65B0\u7684\u79FB\u52A8\u4F53\u9A8C',
    'oobe.get_started': '\u5F00\u59CB\u4F7F\u7528',
    'oobe.language_title': '\u9009\u62E9\u8BED\u8A00',
    'oobe.next': '\u4E0B\u4E00\u6B65',
    'oobe.back': '\u8FD4\u56DE',
    'oobe.wifi_title': 'Wi-Fi\u8FDE\u63A5',
    'oobe.wifi_searching': '\u6B63\u5728\u641C\u7D22\u7F51\u7EDC...',
    'oobe.skip': '\u8DF3\u8FC7',
    'oobe.pin_title': '\u5C4F\u5E55\u9501\u5B9A\u8BBE\u7F6E',
    'oobe.pin_desc': '\u8BBE\u7F6E4\u4F4DPIN\u7801',
    'oobe.pin_set': '\u8BBE\u7F6E',
    'oobe.pin_later': '\u7A0D\u540E',
    'oobe.terms_title': '\u670D\u52A1\u6761\u6B3E',
    'oobe.terms_body': 'Zyl OS\u662F\u4E00\u6B3E\u57FA\u4E8EMIT\u8BB8\u53EF\u8BC1\u53D1\u5E03\u7684\u5F00\u6E90\u79FB\u52A8\u64CD\u4F5C\u7CFB\u7EDF\u3002\u672C\u8F6F\u4EF6\u6309\u201C\u539F\u6837\u201D\u63D0\u4F9B\uFF0C\u4E0D\u63D0\u4F9B\u4EFB\u4F55\u5F62\u5F0F\u7684\u4FDD\u8BC1\u3002',
    'oobe.terms_agree': '\u6211\u540C\u610F\u670D\u52A1\u6761\u6B3E',
    'oobe.agree': '\u540C\u610F',
    'oobe.complete_title': '\u8BBE\u7F6E\u5B8C\u6210',
    'oobe.complete_desc': '\u5C3D\u60C5\u4EAB\u53D7Zyl OS\uFF01',
    'oobe.start': '\u5F00\u59CB'
  });

  zylI18n.addTranslations('es', {
    'oobe.welcome': 'Bienvenido',
    'oobe.welcome_desc': 'Comienza tu nueva experiencia m\u00F3vil',
    'oobe.get_started': 'Empezar',
    'oobe.language_title': 'Seleccionar idioma',
    'oobe.next': 'Siguiente',
    'oobe.back': 'Atr\u00E1s',
    'oobe.wifi_title': 'Conexi\u00F3n Wi-Fi',
    'oobe.wifi_searching': 'Buscando redes...',
    'oobe.skip': 'Omitir',
    'oobe.pin_title': 'Bloqueo de pantalla',
    'oobe.pin_desc': 'Establece un PIN de 4 d\u00EDgitos',
    'oobe.pin_set': 'Establecer',
    'oobe.pin_later': 'M\u00E1s tarde',
    'oobe.terms_title': 'T\u00E9rminos de servicio',
    'oobe.terms_body': 'Zyl OS es un sistema operativo m\u00F3vil de c\u00F3digo abierto distribuido bajo la licencia MIT. Este software se proporciona "tal cual" sin garant\u00EDa de ning\u00FAn tipo.',
    'oobe.terms_agree': 'Acepto los t\u00E9rminos de servicio',
    'oobe.agree': 'Aceptar',
    'oobe.complete_title': 'Configuraci\u00F3n completa',
    'oobe.complete_desc': '\u00A1Disfruta de Zyl OS!',
    'oobe.start': 'Iniciar'
  });
})();
