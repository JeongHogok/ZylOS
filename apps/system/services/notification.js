// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Notification service — post, cancel, list, clear, DND,
//       history, grouping, badge counts
// Scope: OS notification management (actions, DND, history, groups).
//        An action the user presses goes to the posting app (invokeAction →
//        zyl:notification-action: the OS brings that app forward and hands it
//        ActionInvoked), and the notification is done — runtime/services/notification
//        A notification stays until dismissed or cancelled, as on the device;
//        how long a banner shows is the UI's own.
//        Posted and dismissed are announced as zyl:notification-posted /
//        zyl:notification-dismissed window events — the device service's
//        NotificationPosted / NotificationDismissed signals — and whatever
//        draws notifications (the emulator's shade, the lock screen) follows them
// Dependency Direction: Domain -> none (state kept here)
// SOLID: SRP — notification management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.notification = function (deps) {

    /* ─── Notification history (in-memory ring buffer) ─── */
    var MAX_HISTORY = 100;
    var _history = [];       /* { id, appId, channelId, title, body, icon, priority, timestamp, group, actions } */
    var _nextId = 1;
    var _activeMap = {};     /* id → notification object (active/unread) */

    /* ─── DND state ─── */
    var _dndEnabled = false;

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

    function _announce(type, detail) {
      if (typeof window === 'undefined' || typeof window.dispatchEvent !== 'function' ||
          typeof CustomEvent !== 'function') return;
      window.dispatchEvent(new CustomEvent(type, { detail: detail }));
    }

    function _dismiss(id) {
      var notif = _activeMap[id];
      if (!notif) return;
      delete _activeMap[id];
      _removeFromGroup(notif);
      _updateBadge(notif.appId, -1);
      _announce('zyl:notification-dismissed', notif.id);
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
      _announce('zyl:notification-posted', notif);

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

      /* The app was removed: nothing of it stays — active, history, badge */
      _forgetApp: function (appId) {
        Object.keys(_activeMap).forEach(function (k) {
          var n = _activeMap[k];
          if (n.appId !== appId) return;
          _dismiss(n.id);
        });
        _history = _history.filter(function (n) { return n.appId !== appId; });
        delete _badgeCounts[appId];
      },

      /**
       * Cancel (dismiss) a notification by ID — only if p.appId posted it.
       * No appId (the OS, a system app's '') cancels any app's; a third-party
       * app's appId is always its own (services.js), as the device's Cancel(id, app_id).
       */
      cancel: function (p) {
        var notif = _activeMap[p.id];
        if (notif && (!p.appId || notif.appId === p.appId)) _dismiss(p.id);
        return { success: true };
      },

      /**
       * The user pressed an action button (the system UI's call): only one the
       * notification offered. Its app gets ActionInvoked; the notification is done.
       * p: { id, actionId }
       */
      invokeAction: function (p) {
        var notif = _activeMap[p && p.id];
        var offered = notif && (notif.actions || []).some(function (a) { return a && a.actionId === p.actionId; });
        if (!offered) return false;
        _announce('zyl:notification-action', { id: notif.id, appId: notif.appId, actionId: p.actionId });
        _dismiss(notif.id);
        return true;
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
        return { success: true };
      },

      /**
       * Enable or disable Do Not Disturb mode.
       * When DND is active, notifications with priority < 3 (URGENT) are dropped.
       */
      setDndMode: function (p) {
        var enabled = !!(p && p.enabled);
        _dndEnabled = enabled;
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
