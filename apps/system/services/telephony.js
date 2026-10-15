// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Telephony service — SIM, call state, dialer, call log, emergency alerts
// Scope: getState, dial, answer, hangup, sendDTMF, call log (recorded from call
//        states, read and delete), Cell Broadcast history
//        and receive settings (same contract as runtime/services/telephony)
// Dependency Direction: Domain -> settings service
// SOLID: SRP — telephony only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.telephony = function (deps) {
    var settingsSvc = deps.settingsSvc;

    var _callState = { state: 'IDLE', number: '', startTime: 0 };
    var _call = null;            /* { number, incoming, start, answered } for the log */
    var LOG_MAX = 500;

    /* Mirrors cbs.c: presidential alerts are always on, tests off until chosen */
    var ALERT_CATEGORIES = ['presidential', 'extreme', 'severe', 'amber',
                            'public_safety', 'earthquake', 'test'];

    function loadLog() {
      return settingsSvc._getSetting('callLog').then(function (log) {
        return (log && log.entries) ? JSON.parse(log.entries) : [];
      }).catch(function () { return []; });
    }

    /* Same rule as telephony.c: what a call was follows from the states it
       went through — apps never write the log */
    function recordCall() {
      var c = _call;
      _call = null;
      if (!c) return Promise.resolve();
      var now = Date.now();
      return loadLog().then(function (entries) {
        var id = c.start;
        if (entries.length && id <= entries[0].id) id = entries[0].id + 1;
        entries.unshift({
          id: id, number: c.number, time: c.start,
          type: !c.incoming ? 'outgoing' : c.answered ? 'incoming' : 'missed',
          duration: c.answered ? Math.floor((now - c.answered) / 1000) : 0
        });
        settingsSvc._updateSetting('callLog', 'entries', JSON.stringify(entries.slice(0, LOG_MAX)));
      });
    }

    return {
      /* No modem in the emulator: nothing to report */
      getState: function () {
        return Promise.resolve({ simPresent: false, operator: '', networkType: '', signal: 0, imei: '', phoneNumber: '' });
      },
      getCallState: function () { return Promise.resolve(_callState); },
      dial: function (p) {
        _callState = { state: 'DIALING', number: p.number || '', startTime: Date.now() };
        _call = { number: _callState.number, incoming: false, start: Date.now(), answered: 0 };
        setTimeout(function () {
          if (_callState.state !== 'DIALING') return;
          _callState.state = 'ACTIVE';
          if (_call) _call.answered = Date.now();
        }, 2000);
        return Promise.resolve(_callState);
      },
      answer: function () {
        if (_callState.state !== 'RINGING') return Promise.resolve(_callState);
        _callState.state = 'ACTIVE';
        _callState.startTime = Date.now();
        if (_call) _call.answered = Date.now();
        return Promise.resolve(_callState);
      },
      hangup: function () {
        var ended = { number: _callState.number, state: _callState.state, duration: _callState.startTime ? Math.floor((Date.now() - _callState.startTime) / 1000) : 0 };
        _callState = { state: 'IDLE', number: '', startTime: 0 };
        return recordCall().then(function () { return ended; });
      },
      /* Newest first: [{ id, number, type, time, duration }] */
      getCallLog: function () { return loadLog(); },
      deleteCallLog: function (p) {
        return loadLog().then(function (entries) {
          var kept = entries.filter(function (e) { return e.id !== Number(p && p.id); });
          if (kept.length === entries.length) return false;
          settingsSvc._updateSetting('callLog', 'entries', JSON.stringify(kept));
          return true;
        });
      },
      /* No network to carry the tones */
      sendDTMF: function () { return Promise.resolve(false); },
      /* No modem in the emulator: there is no mobile data to switch */
      getMobileData: function () { return Promise.resolve({ supported: false }); },
      setMobileData: function () { return Promise.resolve(false); },
      /* Emergency alerts arrive from the modem; the emulator has none, so the
         history is always empty — never invented */
      getEmergencyAlerts: function () { return Promise.resolve([]); },
      acknowledgeEmergencyAlert: function () { return Promise.resolve(false); },
      getEmergencyAlertSettings: function () {
        return Promise.resolve(settingsSvc._getSetting('emergencyAlerts')).then(function (saved) {
          saved = saved || {};
          var out = {};
          ALERT_CATEGORIES.forEach(function (c) {
            out[c] = c === 'presidential' ? true
              : (typeof saved[c] === 'boolean' ? saved[c] : c !== 'test');
          });
          return out;
        });
      },
      setEmergencyAlertEnabled: function (p) {
        var c = p && p.category;
        var on = !!(p && p.enabled === true);
        if (ALERT_CATEGORIES.indexOf(c) === -1 || (c === 'presidential' && !on)) {
          return Promise.resolve(false);
        }
        settingsSvc._updateSetting('emergencyAlerts', c, on);
        return Promise.resolve(true);
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
