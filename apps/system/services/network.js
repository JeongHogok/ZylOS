// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Network service — HTTP fetch with sandbox domain check
// Scope: fetch URL through sandbox whitelist
// Dependency Direction: Domain -> invoker, ZylSandbox
// SOLID: SRP — network fetch proxy only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.network = function (deps) {
    var invoke = deps.invoke;

    return {
      fetch: function (p) {
        if (typeof ZylSandbox !== 'undefined' && !ZylSandbox.isAllowedDomain(p.url || '')) {
          return Promise.resolve({ error: 'Domain not in whitelist' });
        }
        return invoke('http_fetch', { url: p.url || '' });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
