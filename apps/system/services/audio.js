// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Audio service — volume, vibration, silent mode, sounds
// Scope: per-stream volume, notification/beep/key sounds, vibrate
// Dependency Direction: Domain -> settings service
// SOLID: SRP — audio control only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.audio = function (deps) {
    var settingsSvc = deps.settingsSvc;

    var audioState = {
      mediaVolume: 70, notifVolume: 80, alarmVolume: 90,
      ringtoneVolume: 80, systemVolume: 50, vibration: true, silentMode: false
    };

    /* Load from settings if available */
    function clampVolume(val) {
      var n = parseInt(val, 10);
      return isNaN(n) ? 0 : Math.min(100, Math.max(0, n));
    }

    function loadFromSettings(s) {
      if (!s) return;
      if (s.mediaVolume !== undefined) audioState.mediaVolume = clampVolume(s.mediaVolume);
      if (s.notifVolume !== undefined) audioState.notifVolume = clampVolume(s.notifVolume);
      if (s.alarmVolume !== undefined) audioState.alarmVolume = clampVolume(s.alarmVolume);
      if (s.ringtoneVolume !== undefined) audioState.ringtoneVolume = clampVolume(s.ringtoneVolume);
      if (s.systemVolume !== undefined) audioState.systemVolume = clampVolume(s.systemVolume);
      if (s.vibration !== undefined) audioState.vibration = !!s.vibration;
      if (s.silentMode !== undefined) audioState.silentMode = !!s.silentMode;
    }

    return {
      _loadFromSettings: loadFromSettings,
      _getAudioState: function () { return audioState; },
      getVolume: function (p) {
        var stream = p.stream || 'media';
        return Promise.resolve(audioState[stream + 'Volume'] !== undefined ? audioState[stream + 'Volume'] : 70);
      },
      setVolume: function (p) {
        var stream = p.stream || 'media';
        var key = stream + 'Volume';
        var val = Math.max(0, Math.min(100, parseInt(p.value, 10) || 0));
        audioState[key] = val;
        settingsSvc._updateSetting('sound', key, val);
        return Promise.resolve({ stream: stream, value: val });
      },
      adjustVolume: function (p) {
        var stream = p.stream || 'media';
        var key = stream + 'Volume';
        var current = audioState[key] !== undefined ? audioState[key] : 70;
        var delta = parseInt(p.delta, 10) || 5;
        var val = Math.max(0, Math.min(100, current + delta));
        audioState[key] = val;
        settingsSvc._updateSetting('sound', key, val);
        return Promise.resolve({ stream: stream, value: val });
      },
      getState: function () {
        return Promise.resolve({
          mediaVolume: audioState.mediaVolume,
          notifVolume: audioState.notifVolume,
          alarmVolume: audioState.alarmVolume,
          ringtoneVolume: audioState.ringtoneVolume,
          systemVolume: audioState.systemVolume,
          vibration: audioState.vibration,
          silentMode: audioState.silentMode
        });
      },
      getSilentMode: function () { return Promise.resolve(audioState.silentMode); },
      setSilentMode: function (p) {
        audioState.silentMode = !!p.enabled;
        settingsSvc._updateSetting('sound', 'silentMode', audioState.silentMode);
        return Promise.resolve(audioState.silentMode);
      },
      getVibration: function () { return Promise.resolve(audioState.vibration); },
      setVibration: function (p) {
        audioState.vibration = !!p.enabled;
        settingsSvc._updateSetting('sound', 'vibration', audioState.vibration);
        return Promise.resolve(audioState.vibration);
      },
      playNotificationSound: function () {
        if (audioState.silentMode) return Promise.resolve(false);
        try {
          var ctx = new (window.AudioContext || window.webkitAudioContext)();
          var osc = ctx.createOscillator();
          var gain = ctx.createGain();
          gain.gain.value = (audioState.notifVolume / 100) * 0.5;
          osc.type = 'sine'; osc.frequency.value = 880;
          osc.connect(gain); gain.connect(ctx.destination);
          osc.start(); osc.stop(ctx.currentTime + 0.15);
          setTimeout(function () {
            var osc2 = ctx.createOscillator();
            var gain2 = ctx.createGain();
            gain2.gain.value = (audioState.notifVolume / 100) * 0.5;
            osc2.type = 'sine'; osc2.frequency.value = 1100;
            osc2.connect(gain2); gain2.connect(ctx.destination);
            osc2.start(); osc2.stop(ctx.currentTime + 0.12);
          }, 180);
        } catch (e) { /* Web Audio unavailable */ }
        return Promise.resolve(true);
      },
      playBeep: function (p) {
        if (audioState.silentMode) return Promise.resolve(false);
        var freq = parseInt(p.frequency, 10) || 880;
        var dur = parseInt(p.duration, 10) || 200;
        var rep = parseInt(p.repeat, 10) || 1;
        try {
          var ctx = new (window.AudioContext || window.webkitAudioContext)();
          var gain = ctx.createGain();
          gain.gain.value = (audioState.alarmVolume / 100) * 0.5;
          gain.connect(ctx.destination);
          for (var i = 0; i < rep; i++) {
            (function (delay) {
              var osc = ctx.createOscillator();
              osc.type = 'sine'; osc.frequency.value = freq;
              osc.connect(gain);
              osc.start(ctx.currentTime + delay);
              osc.stop(ctx.currentTime + delay + dur / 1000);
            })(i * (dur + 100) / 1000);
          }
        } catch (e) {}
        return Promise.resolve(true);
      },
      playKeyClick: function () {
        if (audioState.silentMode) return Promise.resolve(false);
        try {
          var ctx = new (window.AudioContext || window.webkitAudioContext)();
          var osc = ctx.createOscillator();
          var g = ctx.createGain();
          g.gain.value = (audioState.systemVolume / 100) * 0.1;
          osc.type = 'sine'; osc.frequency.value = 800;
          osc.connect(g); g.connect(ctx.destination);
          osc.start(); osc.stop(ctx.currentTime + 0.03);
        } catch (e) {}
        return Promise.resolve(true);
      },
      vibrate: function (p) {
        if (!audioState.vibration) return Promise.resolve(false);
        var pattern = p.pattern || [200];
        if (navigator.vibrate) navigator.vibrate(pattern);
        return Promise.resolve(true);
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
