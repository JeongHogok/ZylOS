// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Credential service — secure credential store
// Scope: store, lookup, delete credentials
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — credential operations only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.credential = function (deps) {
    var invoke = deps.invoke;

    return {
      store:  function (p) { return invoke('credential_store', { service: p.service, account: p.account, secret: p.secret }); },
      lookup: function (p) { return invoke('credential_lookup', { service: p.service, account: p.account }); },
      'delete': function (p) { return invoke('credential_delete', { service: p.service, account: p.account }); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
