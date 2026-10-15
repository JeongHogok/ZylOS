// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Alarm service — a trigger an app registers for a future time, which
//       fires even when that app is not running (emulator contract of
//       runtime/services/alarm)
// Scope: set, cancel, list, _forgetApp; timer per alarm, notification on fire
// Dependency Direction: Domain -> notification service, settings service
// SOLID: SRP — scheduling only; what an alarm means belongs to the app
//        DIP — posts through the notification service, not the host
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// The title is the app's own translated string: the OS has no text of its own
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  /* setTimeout truncates to a 32-bit delay, so a longer wait is re-armed */
  var MAX_DELAY_MS = 2147483647;
  /* Registered alarms survive a reload, as the device daemon's file does */
  var STORE_CATEGORY = 'alarms';
  var STORE_KEY = 'scheduled';
  /* An alarm must sound through Do Not Disturb (notification.js drops < 3) */
  var ALARM_PRIORITY = 3;

  ns.alarm = function (deps) {
    var notificationSvc = deps.notificationSvc;
    var settingsSvc = deps.settingsSvc;

    /* tag → { tag, triggerAt, intervalMs, appId, repeating, title, timer } */
    var entries = {};

    function record(e) {
      return {
        tag: e.tag,
        triggerAt: e.triggerAt,
        intervalMs: e.intervalMs,
        appId: e.appId,
        repeating: e.repeating,
        title: e.title
      };
    }

    function listRecords() {
      return Object.keys(entries).map(function (tag) { return record(entries[tag]); });
    }

    function save() {
      if (!settingsSvc) return;
      settingsSvc._updateSetting(STORE_CATEGORY, STORE_KEY, JSON.stringify(listRecords()));
    }

    function fire(e) {
      e.timer = null;
      if (notificationSvc && e.title) {
        notificationSvc.post({
          appId: e.appId,
          channelId: 'alarm',
          title: e.title,
          body: '',
          icon: '',
          priority: ALARM_PRIORITY
        });
      }
      if (e.repeating && e.intervalMs > 0) {
        e.triggerAt = Date.now() + e.intervalMs;
        arm(e);
      } else {
        delete entries[e.tag];
      }
      save();
    }

    function arm(e) {
      if (e.timer) clearTimeout(e.timer);
      var remaining = Math.max(0, e.triggerAt - Date.now());
      e.timer = setTimeout(function () {
        if (remaining > MAX_DELAY_MS) arm(e); /* woke early: wait out the rest */
        else fire(e);
      }, Math.min(remaining, MAX_DELAY_MS));
    }

    function load() {
      if (!settingsSvc) return Promise.resolve(null);
      return settingsSvc._getSetting(STORE_CATEGORY).then(function (stored) {
        var list;
        try {
          list = JSON.parse((stored && stored[STORE_KEY]) || '[]');
        } catch (err) {
          return null;
        }
        if (!Array.isArray(list)) return null;
        var now = Date.now();
        list.forEach(function (a) {
          if (!a || !a.tag) return;
          if (entries[a.tag]) return;   /* set while loading: the live one wins */
          /* A one-shot alarm whose time passed while the OS was down is gone */
          if (!a.repeating && a.triggerAt < now) return;
          if (a.repeating && a.intervalMs > 0 && a.triggerAt < now) {
            var periods = Math.floor((now - a.triggerAt) / a.intervalMs) + 1;
            a.triggerAt = a.triggerAt + periods * a.intervalMs;
          }
          entries[a.tag] = record(a);
          arm(entries[a.tag]);
        });
        return null;
      }).catch(function () { return null; });
    }

    load();

    return {
      /* p: { tag, triggerAt, intervalMs, appId, repeating, title } — same tag replaces */
      set: function (p) {
        if (!p || !p.tag) return Promise.resolve(false);
        var existing = entries[p.tag];
        if (existing && existing.timer) clearTimeout(existing.timer);
        var e = {
          tag: String(p.tag),
          triggerAt: Number(p.triggerAt) || 0,
          intervalMs: Number(p.intervalMs) || 0,
          appId: p.appId || '',
          repeating: !!p.repeating,
          title: p.title || '',
          timer: null
        };
        entries[e.tag] = e;
        arm(e);
        save();
        return Promise.resolve(true);
      },

      cancel: function (p) {
        var e = p && entries[p.tag];
        if (!e) return Promise.resolve(false);
        if (e.timer) clearTimeout(e.timer);
        delete entries[p.tag];
        save();
        return Promise.resolve(true);
      },

      list: function () {
        return Promise.resolve(listRecords());
      },

      /* The app was removed: its alarms go with it, memory and stored copy */
      _forgetApp: function (appId) {
        Object.keys(entries).forEach(function (tag) {
          if (entries[tag].appId !== appId) return;
          if (entries[tag].timer) clearTimeout(entries[tag].timer);
          delete entries[tag];
        });
        save();
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
