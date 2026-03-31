// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Sound Domain Module
//
// 역할: 설정 앱 사운드 도메인 — 볼륨 슬라이더, 진동 토글
// 수행범위: 미디어/알림/알람/벨소리/시스템 볼륨 및 진동 설정 UI
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — 사운드 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  /* ─── Apply Sound State ─── */
  function applySoundState(data) {
    var media = document.getElementById('media-volume');
    var notif = document.getElementById('notif-volume');
    var alarm = document.getElementById('alarm-volume');
    var ringtone = document.getElementById('ringtone-volume');
    var system = document.getElementById('system-volume');
    var vibration = document.getElementById('vibration-toggle');

    if (media) media.value = data.mediaVolume;
    if (notif) notif.value = data.notifVolume;
    if (alarm) alarm.value = data.alarmVolume;
    if (ringtone) ringtone.value = data.ringtoneVolume || 80;
    if (system) system.value = data.systemVolume || 50;
    if (vibration) vibration.checked = data.vibration;
  }

  /* ─── Helper: clamp and send volume ─── */
  function handleVolumeInput(sliderId, settingKey, stream) {
    var slider = document.getElementById(sliderId);
    if (!slider) return;
    slider.addEventListener('input', function () {
      var val = parseInt(slider.value, 10);
      if (isNaN(val)) return;
      val = Math.max(0, Math.min(100, val));
      core.updateSetting('sound', settingKey, val);
      core.requestService('audio', 'setVolume', { stream: stream, value: val });
    });
  }

  /* ─── Volume Sliders ─── */
  handleVolumeInput('media-volume', 'mediaVolume', 'media');
  handleVolumeInput('notif-volume', 'notifVolume', 'notif');
  handleVolumeInput('alarm-volume', 'alarmVolume', 'alarm');
  handleVolumeInput('ringtone-volume', 'ringtoneVolume', 'ringtone');
  handleVolumeInput('system-volume', 'systemVolume', 'system');

  /* ─── Vibration Toggle ─── */
  var vibrationToggle = document.getElementById('vibration-toggle');
  if (vibrationToggle) {
    vibrationToggle.addEventListener('change', function () {
      core.updateSetting('sound', 'vibration', vibrationToggle.checked);
      core.requestService('audio', 'setVibration', { enabled: vibrationToggle.checked });
    });
  }

  /* ─── Register with core ─── */
  core.handlers.sound = {
    onSettingsGet: applySoundState,
    onSettingsUpdated: null
  };

})();
