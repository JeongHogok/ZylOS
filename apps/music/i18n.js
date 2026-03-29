// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Music app translation data
// Scope: music.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Music 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'music.title': '\uC74C\uC545',
    'music.no_tracks': '\uC74C\uC545 \uD30C\uC77C\uC774 \uC5C6\uC2B5\uB2C8\uB2E4',
    'music.shuffle': '\uC154\uD50C',
    'music.repeat': '\uBC18\uBCF5',
    'music.now_playing': '\uD604\uC7AC \uC7AC\uC0DD \uC911',
    'music.volume': '\uC74C\uB7C9'
  });

  zylI18n.addTranslations('en', {
    'music.title': 'Music',
    'music.no_tracks': 'No music files',
    'music.shuffle': 'Shuffle',
    'music.repeat': 'Repeat',
    'music.now_playing': 'Now Playing',
    'music.volume': 'Volume'
  });

  zylI18n.addTranslations('ja', {
    'music.title': '\u30DF\u30E5\u30FC\u30B8\u30C3\u30AF',
    'music.no_tracks': '\u97F3\u697D\u30D5\u30A1\u30A4\u30EB\u304C\u3042\u308A\u307E\u305B\u3093',
    'music.shuffle': '\u30B7\u30E3\u30C3\u30D5\u30EB',
    'music.repeat': '\u30EA\u30D4\u30FC\u30C8',
    'music.now_playing': '\u518D\u751F\u4E2D',
    'music.volume': '\u97F3\u91CF'
  });

  zylI18n.addTranslations('zh', {
    'music.title': '\u97F3\u4E50',
    'music.no_tracks': '\u6CA1\u6709\u97F3\u4E50\u6587\u4EF6',
    'music.shuffle': '\u968F\u673A\u64AD\u653E',
    'music.repeat': '\u5FAA\u73AF\u64AD\u653E',
    'music.now_playing': '\u6B63\u5728\u64AD\u653E',
    'music.volume': '\u97F3\u91CF'
  });

  zylI18n.addTranslations('es', {
    'music.title': 'M\u00FAsica',
    'music.no_tracks': 'No hay archivos de m\u00FAsica',
    'music.shuffle': 'Aleatorio',
    'music.repeat': 'Repetir',
    'music.now_playing': 'Reproduciendo',
    'music.volume': 'Volumen'
  });
})();
