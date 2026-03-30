// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Accessibility service — high contrast, font scale, screen reader
// Scope: getState, setHighContrast, setFontScale
// Dependency Direction: Domain -> settings service
// SOLID: SRP — accessibility features only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.accessibility = function (deps) {
    var settingsSvc = deps.settingsSvc;

    var a11yState = { highContrast: false, fontScale: 1.0, screenReader: false };

    return {
      getState: function () {
        return settingsSvc._getSetting('accessibility').then(function (a11y) {
          if (a11y) {
            a11yState.highContrast = !!a11y.highContrast;
            a11yState.fontScale = parseFloat(a11y.fontScale) || 1.0;
            a11yState.screenReader = !!a11y.screenReader;
          }
          return {
            highContrast: a11yState.highContrast,
            fontScale: a11yState.fontScale,
            screenReader: a11yState.screenReader
          };
        }).catch(function () { return null; });
      },
      setHighContrast: function (p) {
        a11yState.highContrast = !!p.enabled;
        settingsSvc._updateSetting('accessibility', 'highContrast', a11yState.highContrast);
        return Promise.resolve(a11yState.highContrast);
      },
      setFontScale: function (p) {
        a11yState.fontScale = parseFloat(p.scale) || 1.0;
        settingsSvc._updateSetting('accessibility', 'fontScale', a11yState.fontScale);
        return Promise.resolve(a11yState.fontScale);
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
