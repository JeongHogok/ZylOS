// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Notification service — post, cancel, list, clear, DND,
//       history, grouping, timeout, badge counts
// Scope: OS notification management (actions, DND, history, groups)
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — notification management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.notification = function (deps) {
    var invoke = deps.invoke;

    /* ─── Notification history (in-memory ring buffer) ─── */
    var MAX_HISTORY = 100;
    var _history = [];       /* { id, appId, channelId, title, body, icon, priority, timestamp, group, actions } */
    var _nextId = 1;
    var _activeMap = {};     /* id → notification object (active/unread) */

    /* ─── DND state ─── */
    var _dndEnabled = false;

    /* ─── Auto-dismiss timers ─── */
    var _dismissTimers = {};  /* id → timer handle */
    var DEFAULT_TIMEOUT_MS = 30000;   /* 30s for normal priority */
    var URGENT_TIMEOUT_MS = 0;        /* urgent: no auto-dismiss */

    /* ─── Badge counts per app ─── */
    var _badgeCounts = {};   /* appId → number */

    /* ─── Group support ─── */
    var _groups = {};        /* "appId:groupKey" → [notification ids] */

    function _pushHistory(notif) {
      _history.push(notif);
      if (_history.length > MAX_HISTORY) {
        _history.shift();
      }
    }

    function _updateBadge(appId, delta) {
      if (!appId) return;
      if (!_badgeCounts[appId]) _badgeCounts[appId] = 0;
      _badgeCounts[appId] = Math.max(0, _badgeCounts[appId] + delta);
    }

    function _addToGroup(notif) {
      if (!notif.group) return;
      var key = notif.appId + ':' + notif.group;
      if (!_groups[key]) _groups[key] = [];
      _groups[key].push(notif.id);
    }

    function _removeFromGroup(notif) {
      if (!notif.group) return;
      var key = notif.appId + ':' + notif.group;
      if (!_groups[key]) return;
      var idx = _groups[key].indexOf(notif.id);
      if (idx !== -1) _groups[key].splice(idx, 1);
      if (_groups[key].length === 0) delete _groups[key];
    }

    function _scheduleAutoDismiss(notif) {
      var timeout = DEFAULT_TIMEOUT_MS;
      if (notif.priority >= 3) timeout = URGENT_TIMEOUT_MS;
      if (notif.persistent) timeout = 0;
      if (timeout <= 0) return;
      _dismissTimers[notif.id] = setTimeout(function () {
        _dismiss(notif.id);
      }, timeout);
    }

    function _dismiss(id) {
      var notif = _activeMap[id];
      if (!notif) return;
      delete _activeMap[id];
      if (_dismissTimers[id]) {
        clearTimeout(_dismissTimers[id]);
        delete _dismissTimers[id];
      }
      _removeFromGroup(notif);
      _updateBadge(notif.appId, -1);
    }

    function _postInternal(p, actions) {
      /* DND filtering: drop non-urgent when DND active */
      if (_dndEnabled && (p.priority || 1) < 3) {
        return { suppressed: true, reason: 'dnd' };
      }

      var notif = {
        id: _nextId++,
        appId: p.appId || '',
        channelId: p.channelId || '',
        title: p.title || '',
        body: p.body || '',
        icon: p.icon || '',
        priority: p.priority || 1,
        timestamp: Date.now(),
        group: p.group || '',
        persistent: !!p.persistent,
        actions: actions || []
      };

      _activeMap[notif.id] = notif;
      _pushHistory(notif);
      _updateBadge(notif.appId, 1);
      _addToGroup(notif);
      _scheduleAutoDismiss(notif);

      /* Delegate to native layer if available */
      var invokeParams = {
        app_id: notif.appId,
        channel_id: notif.channelId,
        title: notif.title,
        body: notif.body,
        icon: notif.icon,
        priority: notif.priority
      };
      if (actions && actions.length) {
        var nativeActions = [];
        for (var i = 0; i < actions.length; i++) {
          nativeActions.push({
            label: actions[i].label || '',
            action_id: actions[i].actionId || ''
          });
        }
        invokeParams.actions = nativeActions;
        invoke('notif_post_with_actions', invokeParams);
      } else {
        invoke('notif_post', invokeParams);
      }

      return { id: notif.id };
    }

    return {
      /**
       * Post a simple notification.
       * p: { appId, channelId, title, body, icon, priority, group?, persistent? }
       */
      post: function (p) {
        return _postInternal(p, null);
      },

      /**
       * Post a notification with action buttons.
       * p: { appId, channelId, title, body, icon, priority, group?, persistent?, actions }
       * actions: Array of { label, actionId }
       */
      postWithActions: function (p) {
        return _postInternal(p, p.actions || []);
      },

      /**
       * Cancel (dismiss) a notification by ID.
       */
      cancel: function (p) {
        _dismiss(p.id);
        invoke('notif_cancel', { id: p.id });
        return { success: true };
      },

      /**
       * Get all active (unread) notifications.
       */
      getActive: function () {
        var result = [];
        var keys = Object.keys(_activeMap);
        for (var i = 0; i < keys.length; i++) {
          result.push(_activeMap[keys[i]]);
        }
        return result;
      },

      /**
       * Clear all non-persistent active notifications.
       */
      clearAll: function () {
        var keys = Object.keys(_activeMap);
        for (var i = 0; i < keys.length; i++) {
          var n = _activeMap[keys[i]];
          if (!n.persistent) _dismiss(n.id);
        }
        invoke('notif_clear_all');
        return { success: true };
      },

      /**
       * Enable or disable Do Not Disturb mode.
       * When DND is active, notifications with priority < 3 (URGENT) are dropped.
       */
      setDndMode: function (p) {
        var enabled = !!(p && p.enabled);
        _dndEnabled = enabled;
        invoke('notif_set_dnd_mode', { enabled: enabled });
        return { success: true, enabled: enabled };
      },

      /**
       * Query the current DND state.
       */
      getDndMode: function () {
        return { enabled: _dndEnabled };
      },

      /* ─── NEW: History API ─── */

      /**
       * Get notification history (up to MAX_HISTORY entries).
       * p: { appId?, limit? }
       */
      getHistory: function (p) {
        var list = _history;
        if (p && p.appId) {
          list = [];
          for (var i = 0; i < _history.length; i++) {
            if (_history[i].appId === p.appId) list.push(_history[i]);
          }
        }
        var limit = (p && p.limit) ? p.limit : list.length;
        return list.slice(-limit);
      },

      /**
       * Clear notification history.
       */
      clearHistory: function () {
        _history = [];
        return { success: true };
      },

      /* ─── NEW: Badge Count API ─── */

      /**
       * Get badge count for an app.
       * p: { appId }
       */
      getBadgeCount: function (p) {
        return { appId: p.appId, count: _badgeCounts[p.appId] || 0 };
      },

      /**
       * Get badge counts for all apps.
       */
      getAllBadgeCounts: function () {
        var result = {};
        var keys = Object.keys(_badgeCounts);
        for (var i = 0; i < keys.length; i++) {
          if (_badgeCounts[keys[i]] > 0) result[keys[i]] = _badgeCounts[keys[i]];
        }
        return result;
      },

      /* ─── NEW: Grouping API ─── */

      /**
       * Get grouped notifications for an app.
       * p: { appId, group }
       * Returns notifications in the specified group.
       */
      getGroup: function (p) {
        var key = (p.appId || '') + ':' + (p.group || '');
        var ids = _groups[key] || [];
        var result = [];
        for (var i = 0; i < ids.length; i++) {
          if (_activeMap[ids[i]]) result.push(_activeMap[ids[i]]);
        }
        return result;
      },

      /**
       * Get summary of all active groups.
       * Returns { "appId:group": count, ... }
       */
      getGroupSummary: function () {
        var summary = {};
        var keys = Object.keys(_groups);
        for (var i = 0; i < keys.length; i++) {
          summary[keys[i]] = _groups[keys[i]].length;
        }
        return summary;
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
