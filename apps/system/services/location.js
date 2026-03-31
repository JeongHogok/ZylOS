// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Location service — GPS/IP-based location
// Scope: getLastKnown, requestUpdates, stopUpdates
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — location data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  var DEFAULT_INTERVAL_MS = 10000;

  ns.location = function (deps) {
    var invoke = deps.invoke;
    var _watchTimer = null;
    var _listeners = [];

    function pollLocation() {
      invoke('location_get_last_known').then(function (pos) {
        if (pos) {
          _listeners.forEach(function (cb) {
            try { cb(pos); } catch (e) { /* listener error — ignore */ }
          });
        }
      });
    }

    return {
      getLastKnown: function () {
        return invoke('location_get_last_known');
      },

      requestUpdates: function (params) {
        var interval = (params && params.interval) || DEFAULT_INTERVAL_MS;
        var callback = params && params.callback;
        if (typeof callback === 'function') {
          _listeners.push(callback);
        }
        return invoke('location_request_updates', { interval: interval }).then(function (res) {
          if (!_watchTimer) {
            _watchTimer = setInterval(pollLocation, interval);
          }
          return true;
        }, function () {
          /* C service unavailable — fall back to JS-side polling */
          if (!_watchTimer) {
            _watchTimer = setInterval(pollLocation, interval);
          }
          return true;
        });
      },

      stopUpdates: function () {
        if (_watchTimer) {
          clearInterval(_watchTimer);
          _watchTimer = null;
        }
        _listeners = [];
        return invoke('location_stop_updates').then(function () {
          return true;
        }, function () {
          return true;
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
