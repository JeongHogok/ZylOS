// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Notification service — post, cancel, list, clear
// Scope: OS notification management
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — notification management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.notification = function (deps) {
    var invoke = deps.invoke;

    return {
      post: function (p) {
        return invoke('notif_post', {
          app_id: p.appId || '', title: p.title || '',
          body: p.body || '', icon: p.icon || '', priority: p.priority || 1
        });
      },
      cancel:    function (p) { return invoke('notif_cancel', { id: p.id }); },
      getActive: function ()  { return invoke('notif_get_active'); },
      clearAll:  function ()  { return invoke('notif_clear_all'); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
