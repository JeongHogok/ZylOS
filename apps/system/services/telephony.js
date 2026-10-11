// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Telephony service — SIM, call state, dialer, call log, emergency alerts
// Scope: getState, dial, answer, hangup, callLog CRUD, Cell Broadcast history
//        and receive settings (same contract as runtime/services/telephony)
// Dependency Direction: Domain -> settings service
// SOLID: SRP — telephony only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.telephony = function (deps) {
    var settingsSvc = deps.settingsSvc;

    var _callState = { state: 'IDLE', number: '', startTime: 0 };

    /* Mirrors cbs.c: presidential alerts are always on, tests off until chosen */
    var ALERT_CATEGORIES = ['presidential', 'extreme', 'severe', 'amber',
                            'public_safety', 'earthquake', 'test'];

    return {
      getState: function () {
        return settingsSvc._getSetting('telephony').then(function (tel) {
          return {
            simPresent:  (tel && tel.simPresent !== undefined) ? tel.simPresent : true,
            operator:    (tel && tel.operator) || 'Zyl Mobile',
            networkType: (tel && tel.networkType) || 'LTE',
            signal:      (tel && tel.signal !== undefined) ? tel.signal : 3,
            imei:        (tel && tel.imei) || '000000000000000',
            phoneNumber: (tel && tel.phoneNumber) || ''
          };
        }).catch(function () { return null; });
      },
      getCallState: function () { return Promise.resolve(_callState); },
      dial: function (p) {
        _callState = { state: 'DIALING', number: p.number || '', startTime: Date.now() };
        setTimeout(function () {
          if (_callState.state === 'DIALING') _callState.state = 'ACTIVE';
        }, 2000);
        return Promise.resolve(_callState);
      },
      answer: function () {
        _callState.state = 'ACTIVE';
        _callState.startTime = Date.now();
        return Promise.resolve(_callState);
      },
      hangup: function () {
        var ended = { number: _callState.number, state: _callState.state, duration: _callState.startTime ? Math.floor((Date.now() - _callState.startTime) / 1000) : 0 };
        _callState = { state: 'IDLE', number: '', startTime: 0 };
        return Promise.resolve(ended);
      },
      getCallLog: function () {
        return settingsSvc._getSetting('callLog').then(function (log) {
          return (log && log.entries) ? JSON.parse(log.entries) : [];
        }).catch(function () { return []; });
      },
      addCallLog: function (p) {
        return settingsSvc._getSetting('callLog').then(function (log) {
          var entries = (log && log.entries) ? JSON.parse(log.entries) : [];
          entries.unshift({ number: p.number, type: p.type || 'outgoing', time: Date.now(), duration: p.duration || 0, name: p.name || '' });
          if (entries.length > 100) entries = entries.slice(0, 100);
          settingsSvc._updateSetting('callLog', 'entries', JSON.stringify(entries));
          return entries;
        }).catch(function () { return []; });
      },
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
