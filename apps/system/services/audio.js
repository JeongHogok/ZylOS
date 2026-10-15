// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Audio service — volume, vibration, silent mode, sounds
// Scope: per-stream volume, notification/beep/key sounds, alert tones, vibrate,
//        capabilities (speakerphone, vibrator), speakerphone, microphone mute
// Dependency Direction: Domain -> settings service
// SOLID: SRP — audio control only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.audio = function (deps) {
    var settingsSvc = deps.settingsSvc;

    var audioState = {
      mediaVolume: 70, notifVolume: 80, alarmVolume: 90,
      ringtoneVolume: 80, systemVolume: 50, silentMode: false
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
      if (s.silentMode !== undefined) audioState.silentMode = !!s.silentMode;
    }

    /* AudioContext singleton — avoid re-creating per sound */
    var _audioCtx = null;
    function getAudioCtx() {
      if (_audioCtx && _audioCtx.state !== 'closed') return _audioCtx;
      var Ctx = window.AudioContext || window.webkitAudioContext;
      _audioCtx = Ctx ? new Ctx() : null;
      return _audioCtx;
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
          silentMode: audioState.silentMode
        });
      },
      getSilentMode: function () { return Promise.resolve(audioState.silentMode); },
      setSilentMode: function (p) {
        audioState.silentMode = !!p.enabled;
        settingsSvc._updateSetting('sound', 'silentMode', audioState.silentMode);
        return Promise.resolve(audioState.silentMode);
      },
      /* What this device has — same answer as the device's GetCapabilities.
         The emulator carries no call audio (no earpiece to switch from) */
      getCapabilities: function () {
        return Promise.resolve({ speakerphone: false, vibrator: typeof navigator.vibrate === 'function' });
      },
      setSpeakerphone: function () { return Promise.resolve(false); },
      /* Call uplink mute — the emulator carries no call audio */
      setMicMute: function () { return Promise.resolve(false); },
      playNotificationSound: function () {
        if (audioState.silentMode) return Promise.resolve(false);
        try {
          var ctx = getAudioCtx();
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
          var ctx = getAudioCtx();
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
        } catch (e) { /* Web Audio unavailable */ }
        return Promise.resolve(true);
      },
      /* One tone at full alarm volume, silent mode or not — the emergency
         alert attention signal must be heard (same contract as the
         device's PlayTone(freq_hz, duration_ms)) */
      playTone: function (p) {
        var freq = parseInt(p && p.freqHz, 10) || 853;
        var dur = parseInt(p && p.durationMs, 10) || 250;
        try {
          var ctx = getAudioCtx();
          var osc = ctx.createOscillator();
          var gain = ctx.createGain();
          gain.gain.value = 0.5;
          osc.type = 'sine'; osc.frequency.value = freq;
          osc.connect(gain); gain.connect(ctx.destination);
          osc.start(); osc.stop(ctx.currentTime + dur / 1000);
        } catch (e) { /* Web Audio unavailable */ }
        return Promise.resolve(true);
      },
      playKeyClick: function () {
        if (audioState.silentMode) return Promise.resolve(false);
        try {
          var ctx = getAudioCtx();
          var osc = ctx.createOscillator();
          var g = ctx.createGain();
          g.gain.value = (audioState.systemVolume / 100) * 0.1;
          osc.type = 'sine'; osc.frequency.value = 800;
          osc.connect(g); g.connect(ctx.destination);
          osc.start(); osc.stop(ctx.currentTime + 0.03);
        } catch (e) { /* Web Audio unavailable */ }
        return Promise.resolve(true);
      },
      /* Whether to vibrate is a setting (Settings > Sound writes sound.vibration):
         read when needed, so a change applies at once with no second copy */
      vibrate: function (p) {
        return Promise.resolve(settingsSvc._getSetting('sound')).then(function (s) {
          if (s && s.vibration === false) return false;
          if (navigator.vibrate) navigator.vibrate((p && p.pattern) || [200]);
          return true;
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
