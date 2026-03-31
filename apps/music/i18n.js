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
    'music.volume': '\uC74C\uB7C9',
    'music.all_tracks': '\uC804\uCCB4 \uD2B8\uB799',
    'music.playlists': '\uC7AC\uC0DD\uBAA9\uB85D',
    'music.no_playlists': '\uC7AC\uC0DD\uBAA9\uB85D \uC5C6\uC74C',
    'music.create_playlist': '\uC0C8 \uC7AC\uC0DD\uBAA9\uB85D',
    'music.playlist_name': '\uC7AC\uC0DD\uBAA9\uB85D \uC774\uB984',
    'music.cancel': '\uCDE8\uC18C',
    'music.save': '\uC800\uC7A5',
    'music.done': '\uC644\uB8CC',
    'music.add_tracks': '\uD2B8\uB799 \uCD94\uAC00',
    'music.tracks_count': '\uACE1'
  });

  zylI18n.addTranslations('en', {
    'music.title': 'Music',
    'music.no_tracks': 'No music files',
    'music.shuffle': 'Shuffle',
    'music.repeat': 'Repeat',
    'music.now_playing': 'Now Playing',
    'music.volume': 'Volume',
    'music.all_tracks': 'Tracks',
    'music.playlists': 'Playlists',
    'music.no_playlists': 'No playlists',
    'music.create_playlist': 'New Playlist',
    'music.playlist_name': 'Playlist name',
    'music.cancel': 'Cancel',
    'music.save': 'Save',
    'music.done': 'Done',
    'music.add_tracks': 'Add Tracks',
    'music.tracks_count': 'tracks'
  });

  zylI18n.addTranslations('ja', {
    'music.title': '\u30DF\u30E5\u30FC\u30B8\u30C3\u30AF',
    'music.no_tracks': '\u97F3\u697D\u30D5\u30A1\u30A4\u30EB\u304C\u3042\u308A\u307E\u305B\u3093',
    'music.shuffle': '\u30B7\u30E3\u30C3\u30D5\u30EB',
    'music.repeat': '\u30EA\u30D4\u30FC\u30C8',
    'music.now_playing': '\u518D\u751F\u4E2D',
    'music.volume': '\u97F3\u91CF',
    'music.all_tracks': '\u5168\u66F2',
    'music.playlists': '\u30D7\u30EC\u30A4\u30EA\u30B9\u30C8',
    'music.no_playlists': '\u30D7\u30EC\u30A4\u30EA\u30B9\u30C8\u306A\u3057',
    'music.create_playlist': '\u65B0\u898F\u30D7\u30EC\u30A4\u30EA\u30B9\u30C8',
    'music.playlist_name': '\u30D7\u30EC\u30A4\u30EA\u30B9\u30C8\u540D',
    'music.cancel': '\u30AD\u30E3\u30F3\u30BB\u30EB',
    'music.save': '\u4FDD\u5B58',
    'music.done': '\u5B8C\u4E86',
    'music.add_tracks': '\u66F2\u3092\u8FFD\u52A0',
    'music.tracks_count': '\u66F2'
  });

  zylI18n.addTranslations('zh', {
    'music.title': '\u97F3\u4E50',
    'music.no_tracks': '\u6CA1\u6709\u97F3\u4E50\u6587\u4EF6',
    'music.shuffle': '\u968F\u673A\u64AD\u653E',
    'music.repeat': '\u5FAA\u73AF\u64AD\u653E',
    'music.now_playing': '\u6B63\u5728\u64AD\u653E',
    'music.volume': '\u97F3\u91CF',
    'music.all_tracks': '\u5168\u90E8\u66F2\u76EE',
    'music.playlists': '\u64AD\u653E\u5217\u8868',
    'music.no_playlists': '\u65E0\u64AD\u653E\u5217\u8868',
    'music.create_playlist': '\u65B0\u5EFA\u64AD\u653E\u5217\u8868',
    'music.playlist_name': '\u64AD\u653E\u5217\u8868\u540D\u79F0',
    'music.cancel': '\u53D6\u6D88',
    'music.save': '\u4FDD\u5B58',
    'music.done': '\u5B8C\u6210',
    'music.add_tracks': '\u6DFB\u52A0\u66F2\u76EE',
    'music.tracks_count': '\u9996'
  });

  zylI18n.addTranslations('es', {
    'music.title': 'M\u00FAsica',
    'music.no_tracks': 'No hay archivos de m\u00FAsica',
    'music.shuffle': 'Aleatorio',
    'music.repeat': 'Repetir',
    'music.now_playing': 'Reproduciendo',
    'music.volume': 'Volumen',
    'music.all_tracks': 'Pistas',
    'music.playlists': 'Listas',
    'music.no_playlists': 'Sin listas de reproducci\u00F3n',
    'music.create_playlist': 'Nueva lista',
    'music.playlist_name': 'Nombre de la lista',
    'music.cancel': 'Cancelar',
    'music.save': 'Guardar',
    'music.done': 'Listo',
    'music.add_tracks': 'Agregar pistas',
    'music.tracks_count': 'pistas'
  });
})();
