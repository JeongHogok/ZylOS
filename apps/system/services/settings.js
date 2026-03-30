// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Settings service — persistent settings (mount point JSON)
// Scope: load, get, update settings by category
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — settings persistence only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.settings = function (deps) {
    var invoke = deps.invoke;

    var state = {};
    var _loaded = false;

    function _loadFromBackend() {
      if (_loaded) return Promise.resolve(state);
      return invoke('load_settings').then(function (data) {
        if (data) {
          Object.keys(data).forEach(function (cat) {
            state[cat] = data[cat];
          });
          _loaded = true;
        }
        return state;
      }).catch(function () { return state; });
    }

    function getSetting(category) {
      if (_loaded) {
        return Promise.resolve(state[category] || null);
      }
      return _loadFromBackend().then(function () {
        return state[category] || null;
      });
    }

    function updateSetting(category, key, value) {
      if (!state[category]) {
        state[category] = {};
      }
      state[category][key] = value;
      invoke('save_settings', {
        category: category,
        key: key,
        value: value
      }).catch(function () {});
      return state[category];
    }

    return {
      get:    function (p) { return getSetting(p.category); },
      update: function (p) { return updateSetting(p.category, p.key, p.value); },
      _loadFromBackend: _loadFromBackend,
      _getSetting: getSetting,
      _updateSetting: updateSetting,
      _getState: function () { return state; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
