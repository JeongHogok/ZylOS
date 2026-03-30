// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Display service — screen mode, rotation, scale
// Scope: stateful display mode management
// Dependency Direction: Domain -> none (in-memory state)
// SOLID: SRP — display mode only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.display = function (/* deps */) {
    var displayState = {
      width: 1080, height: 2400, refresh: 60,
      rotation: 0, scale: 1.0
    };

    return {
      getMode: function () {
        return Promise.resolve({
          width: displayState.width, height: displayState.height,
          refresh: displayState.refresh
        });
      },
      getRotation: function () { return Promise.resolve(displayState.rotation); },
      setRotation: function (p) {
        var VALID_ROTATIONS = [0, 90, 180, 270];
        var requested = parseInt(p.rotation, 10);
        var rotation = VALID_ROTATIONS.indexOf(requested) !== -1 ? requested : 0;
        displayState.rotation = rotation;
        return Promise.resolve(displayState.rotation);
      },
      getScale: function () { return Promise.resolve(displayState.scale); },
      setScale: function (p) {
        var raw = parseFloat(p.scale);
        var scale = isNaN(raw) ? 1.0 : Math.min(3.0, Math.max(0.5, raw));
        displayState.scale = scale;
        return Promise.resolve(displayState.scale);
      },
      _applyProfile: function (profile) {
        if (!profile) return;
        displayState.width = profile.width || displayState.width;
        displayState.height = profile.height || displayState.height;
      },
      _getState: function () { return displayState; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
