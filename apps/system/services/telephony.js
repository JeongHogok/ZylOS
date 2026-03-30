// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Telephony service — SIM, call state, dialer, call log
// Scope: getState, dial, answer, hangup, callLog CRUD
// Dependency Direction: Domain -> settings service
// SOLID: SRP — telephony only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.telephony = function (deps) {
    var settingsSvc = deps.settingsSvc;

    var _callState = { state: 'IDLE', number: '', startTime: 0 };

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
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
