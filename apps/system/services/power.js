// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Power service — brightness sync and battery state
// Scope: getState, setBrightness
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — power state only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.power = function (deps) {
    var invoke = deps.invoke;
    var powerState = { brightness: 80 };

    /* Same fields as the device's GetState: a battery that is not there is
       batteryPresent false, never a made-up level */
    function stateFrom(raw) {
      var bat = (raw && raw.battery) || {};
      var present = bat.present !== false && bat.level !== undefined;
      var st = {
        state: (raw && raw.state) || 'ACTIVE', screenOn: !raw || raw.screenOn !== false,
        brightness: powerState.brightness, batteryPresent: present,
        charging: present && !!bat.charging, plugged: !present || !!bat.charging
      };
      if (present) st.batteryLevel = bat.level;
      return st;
    }

    return {
      getState: function () {
        return invoke('power_get_state').then(stateFrom).catch(function () { return null; });
      },
      /* The emulator dims its virtual screen (a backlight); the battery is the host's */
      getCapabilities: function () {
        return invoke('power_get_state').then(function (raw) {
          return { backlight: true, battery: stateFrom(raw).batteryPresent };
        }).catch(function () { return { backlight: true, battery: false }; });
      },
      setBrightness: function (p) {
        powerState.brightness = Math.max(0, Math.min(100, parseInt(p.percent, 10) || 0));
        return Promise.resolve(true);
      },
      _getState: function () { return powerState; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
