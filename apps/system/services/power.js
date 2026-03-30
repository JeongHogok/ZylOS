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

    return {
      getState: function () {
        return invoke('power_get_state').then(function (state) {
          if (state) {
            state.brightness = powerState.brightness;
          }
          if (state) {
            if (state.battery && state.battery.level !== undefined) {
              state.batteryLevel = state.battery.level;
              state.charging = state.battery.charging || false;
            }
          }
          return state || { state: 'ACTIVE', brightness: powerState.brightness, screenOn: true, batteryLevel: 85, level: 85, charging: false };
        });
      },
      setBrightness: function (p) {
        var pct = Math.max(0, Math.min(100, parseInt(p.percent, 10) || 80));
        powerState.brightness = pct;
        return Promise.resolve({ brightness: pct });
      },
      _getState: function () { return powerState; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
