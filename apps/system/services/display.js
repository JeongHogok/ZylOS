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
        displayState.rotation = parseInt(p.rotation, 10) || 0;
        return Promise.resolve(displayState.rotation);
      },
      getScale: function () { return Promise.resolve(displayState.scale); },
      setScale: function (p) {
        displayState.scale = parseFloat(p.scale) || 1.0;
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
