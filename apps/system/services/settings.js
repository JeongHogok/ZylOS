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
    var _debounceTimers = {};

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

      /* 200ms debounce — 같은 category의 연속 쓰기를 병합 */
      if (_debounceTimers[category]) {
        clearTimeout(_debounceTimers[category]);
      }
      _debounceTimers[category] = setTimeout(function () {
        delete _debounceTimers[category];
        var snapshot = state[category];
        var keys = Object.keys(snapshot);
        /* 변경된 category 전체를 단일 호출로 flush */
        for (var i = 0; i < keys.length; i++) {
          (function (k) {
            invoke('save_settings', {
              category: category,
              key: k,
              value: snapshot[k]
            }).catch(function () { /* fire-and-forget: non-critical persist */ });
          })(keys[i]);
        }
      }, 200);

      return state[category];
    }

    /* Permissions the user turned off for an app (app_permissions: appId →
       "perm,perm") are refused by the permission gate from now on — the
       device WAM follows the same key through the settings CategoryChanged */
    function applyRevoked(appId, value) {
      if (typeof ZylPermissions === 'undefined') return;
      ZylPermissions.setAppOverride(appId, String(value || '').split(',').filter(Boolean));
    }

    return {
      get:    function (p) { return getSetting(p.category); },
      update: function (p) {
        /* The PIN lives in the lock service as a hash, never in settings */
        if (p.category === 'security' && p.key === 'pin') {
          return Promise.reject(new Error('the PIN belongs to the lock service'));
        }
        var updated = updateSetting(p.category, p.key, p.value);
        if (p.category === 'app_permissions') applyRevoked(p.key, p.value);
        return updated;
      },
      _loadFromBackend: function () {
        return _loadFromBackend().then(function (s) {
          var perms = s.app_permissions || {};
          Object.keys(perms).forEach(function (appId) { applyRevoked(appId, perms[appId]); });
          return s;
        });
      },
      /* A key gone, not set to null (the backend removes it from settings.json) */
      _removeSetting: function (category, key) {
        if (state[category]) delete state[category][key];
        return invoke('save_settings', { category: category, key: key, value: null })
          .catch(function () { /* fire-and-forget: non-critical persist */ });
      },
      _getSetting: getSetting,
      _updateSetting: updateSetting,
      _getState: function () { return state; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
