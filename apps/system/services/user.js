// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: User service — current user and user list
// Scope: getCurrent, listUsers
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — user info only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.user = function (deps) {
    var invoke = deps.invoke;

    return {
      getCurrent: function () { return invoke('user_get_current'); },
      listUsers:  function () { return invoke('user_list'); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
