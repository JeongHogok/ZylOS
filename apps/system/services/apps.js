// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: AppRegistry service — installed apps from OS image
// Scope: getInstalled, getById. A store-installed app is listed only while its
//        seal holds (app-seal.js) — as the device's WAM registers
// Dependency Direction: Domain -> invoker, app seal, ZylPermissions, ZylAppRegistry
// SOLID: SRP — app registry only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.apps = function (deps) {
    var invoke = deps.invoke;
    var SYSTEM_APPS = deps.SYSTEM_APPS;
    var seal = deps.seal;

    var _cache = null;

    function getInstalled() {
      if (_cache) return Promise.resolve(_cache);
      return invoke('list_installed_apps').then(function (list) {
        return seal ? seal.sealedOnly(list) : list;
      }).then(function (list) {
        if (list && list.length > 0) {
          _cache = list.map(function (m) {
            var appName = (m.id || '').split('.').pop();
            var system = SYSTEM_APPS.indexOf(m.id) !== -1;
            return {
              id: m.id,
              name: m.name || appName,
              nameKey: 'app.' + appName,
              icon: m.iconKey || appName,
              color: m.color || 'icon-blue',
              version: m.version || '1.0.0',
              description: m.description || '',
              system: system,
              permissions: m.permissions || [],
              formFactors: m.formFactors || [],
              iconSvg: m.iconSvg || '',
              /* false for system UI (lock screen, status bar, setup, home, keyboard);
                 only a system app may say so — a third-party app would hide from
                 every place the user could remove it (manifest.h zyl_manifest_in_launcher) */
              launcher: !system || m.launcher !== false,
              intents: Array.isArray(m.intents) ? m.intents : [],
              entry: m.entry || 'index.html',
              /* 'store': installed from the app store, served as zyl-app://<id>/ */
              source: m.source === 'store' ? 'store' : 'image'
            };
          });
          if (typeof ZylPermissions !== 'undefined' && ZylPermissions.registerFromAppList) {
            ZylPermissions.registerFromAppList(_cache);
          }
          if (typeof ZylAppRegistry !== 'undefined' && ZylAppRegistry.register) {
            ZylAppRegistry.register(_cache);
          }
        }
        return _cache || [];
      }).catch(function () { return []; });
    }

    function getById(id) {
      if (!_cache) return null;
      return _cache.find(function (a) { return a.id === id; }) || null;
    }

    function invalidateCache() { _cache = null; }

    return {
      getInstalled: function () { return getInstalled(); },
      getById:      function (p) { return getById(p.id); },
      _invalidate: invalidateCache,
      _getRef: function () { return { getInstalled: getInstalled, getById: getById, _cache: _cache }; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
