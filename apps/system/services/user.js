// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: User service — current user and user list
// Scope: getCurrent, listUsers. The emulated device has what a device has
//        before anyone adds a user: the owner runtime/services/user creates
//        at start (uid 1000, "Owner", /data/users/1000). Adding and switching
//        users is the device service's; no app calls it today
// Dependency Direction: Domain -> none (the device's facts, not the host's)
// SOLID: SRP — user info only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.user = function () {
    function owner() {
      return { uid: 1000, name: 'Owner', type: 'OWNER', avatar: '', dataDir: '/data/users/1000', isActive: true };
    }

    return {
      getCurrent: function () { return Promise.resolve(owner()); },
      listUsers:  function () { return Promise.resolve([owner()]); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
