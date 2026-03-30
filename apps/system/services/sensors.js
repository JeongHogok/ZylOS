// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Sensors service — accelerometer, gyroscope, proximity, light, magnetometer
// Scope: getLatest sensor data with micro-noise simulation
// Dependency Direction: Domain -> none
// SOLID: SRP — sensor data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.sensors = function (/* deps */) {
    return {
      getLatest: function (p) {
        var type = (p && p.type) || 'accelerometer';
        var noise = function () { return (Math.random() - 0.5) * 0.02; };
        var now = Date.now();
        var defaults = {
          accelerometer: { type: 'accelerometer', values: [noise(), noise(), -9.8 + noise()], timestamp: now },
          gyroscope:     { type: 'gyroscope', values: [noise(), noise(), noise()], timestamp: now },
          proximity:     { type: 'proximity', values: [0, 5.0], timestamp: now },
          light:         { type: 'light', values: [300 + Math.random() * 10 - 5], timestamp: now },
          magnetometer:  { type: 'magnetometer', values: [noise() * 10, 25 + noise() * 5, -45 + noise() * 5], timestamp: now }
        };
        return Promise.resolve(defaults[type] || defaults.accelerometer);
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
