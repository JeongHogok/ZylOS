// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Sensors service — accelerometer, gyroscope, proximity, light, magnetometer
// Scope: Scenario-based sensor simulation with time-varying data,
//        mouse/device-orientation mapping, micro-noise injection.
// Dependency Direction: Domain -> none (uses Date.now() for time-based scenarios)
// SOLID: SRP — sensor data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.sensors = function (/* deps */) {
    /* ─── Noise generation ─── */
    var noise = function (amplitude) {
      var a = amplitude || 0.02;
      return (Math.random() - 0.5) * 2 * a;
    };

    /* ─── Time-based scenario state ─── */
    var bootTime = Date.now();
    var orientationState = { alpha: 0, beta: 0, gamma: 0 }; /* degrees from DeviceOrientation */
    var mouseState = { x: 0.5, y: 0.5 }; /* normalized 0..1 */

    /* Listen for DeviceOrientation (mobile/tablet emulation) */
    if (typeof window !== 'undefined' && window.addEventListener) {
      window.addEventListener('deviceorientation', function (e) {
        if (e.alpha !== null) orientationState.alpha = e.alpha;
        if (e.beta !== null)  orientationState.beta = e.beta;
        if (e.gamma !== null) orientationState.gamma = e.gamma;
      }, false);

      /* Mouse position → simulated tilt (desktop fallback) */
      window.addEventListener('mousemove', function (e) {
        if (window.innerWidth > 0)  mouseState.x = e.clientX / window.innerWidth;
        if (window.innerHeight > 0) mouseState.y = e.clientY / window.innerHeight;
      }, false);
    }

    /* ─── Scenario generators ─── */
    function elapsed() { return (Date.now() - bootTime) / 1000; }

    function accelFromOrientation() {
      /* Map orientation/mouse to gravitational components.
         beta → forward/back tilt (pitch), gamma → left/right tilt (roll).
         Desktop: mouse x → roll, mouse y → pitch. */
      var pitch, roll;
      if (orientationState.beta !== 0 || orientationState.gamma !== 0) {
        pitch = orientationState.beta * Math.PI / 180;
        roll  = orientationState.gamma * Math.PI / 180;
      } else {
        /* Mouse fallback: center = flat, edges = tilted */
        pitch = (mouseState.y - 0.5) * Math.PI / 3;  /* ±60° max */
        roll  = (mouseState.x - 0.5) * Math.PI / 3;
      }

      var g = 9.80665;
      /* Rotate gravity vector by pitch and roll */
      var ax = g * Math.sin(roll) + noise(0.05);
      var ay = -g * Math.sin(pitch) * Math.cos(roll) + noise(0.05);
      var az = -g * Math.cos(pitch) * Math.cos(roll) + noise(0.05);

      return [ax, ay, az];
    }

    function gyroFromOrientation() {
      /* Simulate angular velocity — derivative of orientation */
      var t = elapsed();
      /* Small oscillation to simulate hand tremor */
      var wx = Math.sin(t * 2.1) * 0.003 + noise(0.001);
      var wy = Math.cos(t * 1.7) * 0.003 + noise(0.001);
      var wz = Math.sin(t * 0.8) * 0.002 + noise(0.001);
      return [wx, wy, wz];
    }

    function ambientLight() {
      /* Simulate day/night cycle: uses real clock.
         6am=sunrise, noon=peak, 6pm=sunset, midnight=dark. */
      var now = new Date();
      var hour = now.getHours() + now.getMinutes() / 60;
      /* Cosine curve: 0 at midnight, 1 at noon */
      var dayFactor = Math.max(0, Math.cos((hour - 12) * Math.PI / 12));
      var lux = 50 + dayFactor * 950; /* 50-1000 lux range */
      /* Indoor variation */
      lux += Math.sin(elapsed() * 0.3) * 15;
      lux += noise(5);
      return [Math.max(0, lux)];
    }

    function magnetometerField() {
      /* Earth's magnetic field ≈ 25-65 µT, orientation-dependent */
      var t = elapsed();
      var mx = 22 + Math.sin(t * 0.1) * 3 + noise(0.5);
      var my = -5 + Math.cos(t * 0.15) * 2 + noise(0.5);
      var mz = -42 + Math.sin(t * 0.08) * 2 + noise(0.5);

      /* Respond to mouse/orientation for compass simulation */
      if (orientationState.alpha !== 0) {
        var heading = orientationState.alpha * Math.PI / 180;
        mx = 25 * Math.cos(heading) + noise(0.5);
        my = 25 * Math.sin(heading) + noise(0.5);
      }
      return [mx, my, mz];
    }

    function proximityData() {
      /* Simulate object approach: farther normally, near during scroll-up gesture.
         Use mouse Y as proxy — top of screen = near. */
      var dist = 5.0; /* max range cm */
      if (mouseState.y < 0.1) {
        dist = 0; /* object very near */
      } else if (mouseState.y < 0.3) {
        dist = mouseState.y * 10;
      }
      dist += noise(0.1);
      if (dist < 0) dist = 0;
      return [dist < 0.5 ? 1 : 0, Math.min(5.0, Math.max(0, dist))];
    }

    /* ─── Pre-built scenario: "walking" motion pattern ─── */
    function walkingAccel() {
      var t = elapsed();
      var stepFreq = 1.8; /* Hz, ~108 steps/min */
      var stepPhase = t * stepFreq * 2 * Math.PI;
      var ax = Math.sin(stepPhase * 0.5) * 0.4 + noise(0.15);
      var ay = Math.sin(stepPhase) * 0.8 + noise(0.15);
      var az = -9.8 + Math.abs(Math.sin(stepPhase)) * 1.2 + noise(0.2);
      return [ax, ay, az];
    }

    /* ─── Scenario selection ─── */
    var _activeScenario = 'default'; /* 'default' | 'walking' | 'driving' */

    return {
      getLatest: function (p) {
        var type = (p && p.type) || 'accelerometer';
        var now = Date.now();
        var data;

        switch (type) {
          case 'accelerometer':
            data = (_activeScenario === 'walking') ? walkingAccel() : accelFromOrientation();
            break;
          case 'gyroscope':
            data = gyroFromOrientation();
            break;
          case 'proximity':
            data = proximityData();
            break;
          case 'light':
            data = ambientLight();
            break;
          case 'magnetometer':
            data = magnetometerField();
            break;
          default:
            data = accelFromOrientation();
            type = 'accelerometer';
        }

        return Promise.resolve({ type: type, values: data, timestamp: now });
      },

      /**
       * Set active scenario for accelerometer/gyroscope simulation.
       * @param {string} scenario - 'default' | 'walking' | 'driving'
       */
      setScenario: function (p) {
        _activeScenario = (p && p.scenario) || 'default';
        return Promise.resolve({ scenario: _activeScenario });
      },

      getScenario: function () {
        return Promise.resolve({ scenario: _activeScenario });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
