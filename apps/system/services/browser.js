// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Browser service — bookmark/quick-link routing
// Scope: app-owned data passthrough
// Dependency Direction: Domain -> none
// SOLID: SRP — browser data routing only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.browser = function (/* deps */) {
    return {
      getBookmarks:  function () { return Promise.resolve(null); },
      getQuickLinks: function () { return Promise.resolve(null); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
