// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Notification service — post, cancel, list, clear, DND
// Scope: OS notification management (actions, DND mode)
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — notification management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.notification = function (deps) {
    var invoke = deps.invoke;

    return {
      /**
       * Post a simple notification.
       * p: { appId, channelId, title, body, icon, priority }
       */
      post: function (p) {
        return invoke('notif_post', {
          app_id:     p.appId     || '',
          channel_id: p.channelId || '',
          title:      p.title     || '',
          body:       p.body      || '',
          icon:       p.icon      || '',
          priority:   p.priority  || 1
        });
      },

      /**
       * Post a notification with action buttons (inline reply support).
       * p: { appId, channelId, title, body, icon, priority, actions }
       * actions: Array of { label, actionId } objects.
       * Returns a Promise that resolves with the notification id.
       */
      postWithActions: function (p) {
        var actions = [];
        if (p.actions && p.actions.length) {
          var i;
          for (i = 0; i < p.actions.length; i++) {
            actions.push({
              label:     p.actions[i].label     || '',
              action_id: p.actions[i].actionId  || ''
            });
          }
        }
        return invoke('notif_post_with_actions', {
          app_id:      p.appId     || '',
          channel_id:  p.channelId || '',
          title:       p.title     || '',
          body:        p.body      || '',
          icon:        p.icon      || '',
          priority:    p.priority  || 1,
          actions:     actions
        });
      },

      cancel:    function (p) { return invoke('notif_cancel',     { id: p.id }); },
      getActive: function ()  { return invoke('notif_get_active');               },
      clearAll:  function ()  { return invoke('notif_clear_all');                },

      /**
       * Enable or disable Do Not Disturb mode.
       * When DND is active, notifications with priority < 3 (URGENT) are dropped.
       * enabled: Boolean
       */
      setDndMode: function (enabled) {
        return invoke('notif_set_dnd_mode', { enabled: !!enabled });
      },

      /**
       * Query the current DND state.
       * Returns a Promise that resolves with { enabled: Boolean }.
       */
      getDndMode: function () {
        return invoke('notif_get_dnd_mode');
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
