// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Storage service — disk usage (mount point)
// Scope: getUsage, prefetch (bytes — apps format them for their locale)
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — storage info only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.storage = function (deps) {
    var invoke = deps.invoke;

    var _cache = null;
    var _cacheTime = 0;

    function _fetchFromBackend() {
      return invoke('fs_get_usage').then(function (s) {
        _cache = s;
        _cacheTime = Date.now();
        return s;
      }).catch(function () {
        return _cache || { total: 0, used: 0, available: 0, percent: 0 };
      });
    }

    function getUsage() {
      if (_cache && Date.now() - _cacheTime < 30000) {
        return Promise.resolve(_cache);
      }
      return _fetchFromBackend();
    }

    return {
      getUsage:     function () { return getUsage(); },
      prefetch: function () { return _fetchFromBackend(); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
