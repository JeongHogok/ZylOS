// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - i18n Data
//
// Role: Clock app translation data
// Scope: clock.* keys for ko/en/ja/zh/es
// Dependency: zylI18n (shared/i18n.js)
// SOLID: SRP — Clock 번역 데이터만 담당
// ──────────────────────────────────────────────────────────
(function () {
  if (typeof zylI18n === 'undefined') return;

  zylI18n.addTranslations('ko', {
    'clock.title': '\uC2DC\uACC4',
    'clock.alarm': '\uC54C\uB78C',
    'clock.timer': '\uD0C0\uC774\uBA38',
    'clock.stopwatch': '\uC2A4\uD1B1\uC6CC\uCE58',
    'clock.add_alarm': '\uC54C\uB78C \uCD94\uAC00',
    'clock.start': '\uC2DC\uC791',
    'clock.stop': '\uC815\uC9C0',
    'clock.reset': '\uCD08\uAE30\uD654',
    'clock.lap': '\uB7A9',
    'clock.set_time': '\uC2DC\uAC04 \uC124\uC815',
    'clock.alarm_label': '\uB77C\uBCA8',
    'clock.repeat_days': '\uBC18\uBCF5',
    'clock.delete': '\uC0AD\uC81C'
  });

  zylI18n.addTranslations('en', {
    'clock.title': 'Clock',
    'clock.alarm': 'Alarm',
    'clock.timer': 'Timer',
    'clock.stopwatch': 'Stopwatch',
    'clock.add_alarm': 'Add Alarm',
    'clock.start': 'Start',
    'clock.stop': 'Stop',
    'clock.reset': 'Reset',
    'clock.lap': 'Lap',
    'clock.set_time': 'Set Time',
    'clock.alarm_label': 'Label',
    'clock.repeat_days': 'Repeat',
    'clock.delete': 'Delete'
  });

  zylI18n.addTranslations('ja', {
    'clock.title': '\u6642\u8A08',
    'clock.alarm': '\u30A2\u30E9\u30FC\u30E0',
    'clock.timer': '\u30BF\u30A4\u30DE\u30FC',
    'clock.stopwatch': '\u30B9\u30C8\u30C3\u30D7\u30A6\u30A9\u30C3\u30C1',
    'clock.add_alarm': '\u30A2\u30E9\u30FC\u30E0\u3092\u8FFD\u52A0',
    'clock.start': '\u958B\u59CB',
    'clock.stop': '\u505C\u6B62',
    'clock.reset': '\u30EA\u30BB\u30C3\u30C8',
    'clock.lap': '\u30E9\u30C3\u30D7',
    'clock.set_time': '\u6642\u523B\u8A2D\u5B9A',
    'clock.alarm_label': '\u30E9\u30D9\u30EB',
    'clock.repeat_days': '\u7E70\u308A\u8FD4\u3057',
    'clock.delete': '\u524A\u9664'
  });

  zylI18n.addTranslations('zh', {
    'clock.title': '\u65F6\u949F',
    'clock.alarm': '\u95F9\u949F',
    'clock.timer': '\u5B9A\u65F6\u5668',
    'clock.stopwatch': '\u79D2\u8868',
    'clock.add_alarm': '\u6DFB\u52A0\u95F9\u949F',
    'clock.start': '\u5F00\u59CB',
    'clock.stop': '\u505C\u6B62',
    'clock.reset': '\u91CD\u7F6E',
    'clock.lap': '\u8BA1\u6B21',
    'clock.set_time': '\u8BBE\u7F6E\u65F6\u95F4',
    'clock.alarm_label': '\u6807\u7B7E',
    'clock.repeat_days': '\u91CD\u590D',
    'clock.delete': '\u5220\u9664'
  });

  zylI18n.addTranslations('es', {
    'clock.title': 'Reloj',
    'clock.alarm': 'Alarma',
    'clock.timer': 'Temporizador',
    'clock.stopwatch': 'Cron\u00F3metro',
    'clock.add_alarm': 'Agregar alarma',
    'clock.start': 'Iniciar',
    'clock.stop': 'Detener',
    'clock.reset': 'Reiniciar',
    'clock.lap': 'Vuelta',
    'clock.set_time': 'Ajustar hora',
    'clock.alarm_label': 'Etiqueta',
    'clock.repeat_days': 'Repetir',
    'clock.delete': 'Eliminar'
  });
})();
