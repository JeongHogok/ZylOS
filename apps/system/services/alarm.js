// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Alarm service — a trigger an app registers for a future time, which
//       fires even when that app is not running (emulator contract of
//       runtime/services/alarm)
// Scope: set, cancel, list, _forgetApp; timer per alarm, notification on fire.
//        An alarm is its app's tag (appId — the OS's for a third-party app):
//        another app's same tag is not replaced, cancelled or listed
//        (same rule as runtime/services/alarm)
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
  /* The device's limits (runtime/services/alarm/alarm.h, verify-all C5c): no app
     holds them all, and a repeating one wakes the device at most each minute */
  var MAX_ALARMS = 1024;
  var MAX_PER_APP = 128;
  var MIN_INTERVAL_MS = 60000;
  var MAX_INTERVAL_MS = 31622400000;

  function intervalOk(a) {
    return !a.repeating || (a.intervalMs >= MIN_INTERVAL_MS && a.intervalMs <= MAX_INTERVAL_MS);
  }

  ns.alarm = function (deps) {
    var notificationSvc = deps.notificationSvc;
    var settingsSvc = deps.settingsSvc;

    /* appId + '\n' + tag → { tag, triggerAt, intervalMs, appId, repeating, title, timer } */
    var entries = {};

    function keyOf(appId, tag) { return (appId || '') + '\n' + tag; }

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
      return Object.keys(entries).map(function (k) { return record(entries[k]); });
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
        delete entries[keyOf(e.appId, e.tag)];
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
          if (!a || !a.tag || !intervalOk(a)) return;
          if (entries[keyOf(a.appId, a.tag)]) return;   /* set while loading: the live one wins */
          /* A one-shot alarm whose time passed while the OS was down is gone */
          if (!a.repeating && a.triggerAt < now) return;
          if (a.repeating && a.intervalMs > 0 && a.triggerAt < now) {
            var periods = Math.floor((now - a.triggerAt) / a.intervalMs) + 1;
            a.triggerAt = a.triggerAt + periods * a.intervalMs;
          }
          var k = keyOf(a.appId, a.tag);
          entries[k] = record(a);
          arm(entries[k]);
        });
        return null;
      }).catch(function () { return null; });
    }

    /* The stored copy is read asynchronously; the OS (and the tests) can wait */
    var ready = load();

    return {
      /* p: { tag, triggerAt, intervalMs, appId, repeating, title } — the same
         app's same tag replaces */
      set: function (p) {
        if (!p || !p.tag) return Promise.resolve(false);
        var existing = entries[keyOf(p.appId, String(p.tag))];
        var e = {
          tag: String(p.tag),
          triggerAt: Number(p.triggerAt) || 0,
          intervalMs: Number(p.intervalMs) || 0,
          appId: p.appId || '',
          repeating: !!p.repeating,
          title: p.title || '',
          timer: null
        };
        if (!intervalOk(e)) return Promise.resolve(false);
        if (!existing) {
          var all = Object.keys(entries);
          var own = all.filter(function (k) { return entries[k].appId === e.appId; }).length;
          if (all.length >= MAX_ALARMS || own >= MAX_PER_APP) return Promise.resolve(false);
        }
        if (existing && existing.timer) clearTimeout(existing.timer);
        entries[keyOf(e.appId, e.tag)] = e;
        arm(e);
        save();
        return Promise.resolve(true);
      },

      /* p: { tag, appId } — its own alarm only */
      cancel: function (p) {
        var k = p && keyOf(p.appId, p.tag);
        var e = k && entries[k];
        if (!e) return Promise.resolve(false);
        if (e.timer) clearTimeout(e.timer);
        delete entries[k];
        save();
        return Promise.resolve(true);
      },

      /* p: { appId } — that app's alarms ('' — a system app asking for all) */
      list: function (p) {
        var only = p && p.appId;
        return Promise.resolve(listRecords().filter(function (r) { return !only || r.appId === only; }));
      },

      /* Resolves once the stored alarms have been read and armed */
      _ready: ready,

      /* The app was removed: its alarms go with it, memory and stored copy */
      _forgetApp: function (appId) {
        Object.keys(entries).forEach(function (k) {
          if (entries[k].appId !== appId) return;
          if (entries[k].timer) clearTimeout(entries[k].timer);
          delete entries[k];
        });
        save();
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
