// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Push service — an app's server reaching it through the OS (emulator
//       contract of runtime/services/push)
// Scope: register (a random topic per app — ntfy's UnifiedPush form, the same endpoint each
//        time, <url>/<topic>?up=1 — UnifiedPush, ntfy protocol), unregister,
//        _poll (what came since the last message: the device holds one
//        stream, the emulator asks now and then), _forgetApp. A message for
//        the app in front: PushReceived (zyl:push); otherwise the notification
//        that app registered. The server is system/push.json (empty: push off)
// Dependency Direction: Domain -> invoker (push_config, push_poll), settings
//        service (registrations survive a reload), notification service
// SOLID: SRP — receive and hand over; what a message means is the app's
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// The notification text is the app's own translated string
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  var STORE_CATEGORY = 'push';
  var STORE_KEY = 'state';

  ns.push = function (deps) {
    var invoke = deps.invoke;
    var settingsSvc = deps.settingsSvc;
    var notificationSvc = deps.notificationSvc;

    var url = '';            /* the server; '' — push is off */
    var regs = {};           /* appId → { topic, title, body } */
    var since = '';          /* the last message's id */

    function save() {
      if (settingsSvc) settingsSvc._updateSetting(STORE_CATEGORY, STORE_KEY, JSON.stringify({ since: since, apps: regs }));
    }

    function load() {
      var config = Promise.resolve(invoke('push_config')).then(function (text) {
        var u = '';
        try { u = String(JSON.parse(text || '{}').url || ''); } catch (e) { u = ''; }
        url = u.indexOf('https://') === 0 ? u.replace(/\/+$/, '') : '';
      });
      var stored = settingsSvc ? settingsSvc._getSetting(STORE_CATEGORY).then(function (s) {
        try {
          var st = JSON.parse((s && s[STORE_KEY]) || '{}');
          since = String(st.since || '');
          regs = (st.apps && typeof st.apps === 'object') ? st.apps : {};
        } catch (e) { /* none kept */ }
      }) : Promise.resolve();
      return Promise.all([config, stored]).catch(function () { return null; });
    }
    var ready = load();

    /* 'up' + 12 random letters and digits (~71 bits): nobody can guess an app's
       topic. ntfy's UnifiedPush form — the server counts its rate limits on the
       subscriber, not on the app server (runtime/services/push push_new_topic) */
    var ALNUM = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    function newTopic() {
      var t = 'up';
      while (t.length < 14) {
        var b = new Uint8Array(16);
        crypto.getRandomValues(b);
        for (var i = 0; i < b.length && t.length < 14; i++) if (b[i] < 248) t += ALNUM.charAt(b[i] % 62);
      }
      return t;
    }

    function ownerOf(topic) {
      var ids = Object.keys(regs);
      for (var i = 0; i < ids.length; i++) if (regs[ids[i]].topic === topic) return ids[i];
      return null;
    }

    /* The app in front is the only one whose page is alive here */
    function inFront(appId) {
      return typeof ZylShell !== 'undefined' && ZylShell.front && ZylShell.front() === appId;
    }

    function arrive(msg) {
      since = String(msg.id);
      var owner = ownerOf(msg.topic);
      if (!owner) return;
      if (inFront(owner)) {
        if (typeof window !== 'undefined' && typeof window.dispatchEvent === 'function' && typeof CustomEvent === 'function') {
          window.dispatchEvent(new CustomEvent('zyl:push', { detail: { topic: msg.topic, appId: owner, message: String(msg.message || '') } }));
        }
      } else if (notificationSvc && regs[owner].title) {
        notificationSvc.post({ appId: owner, channelId: 'push', title: regs[owner].title, body: regs[owner].body, priority: 1 });
      }
    }

    return {
      /* p: { title, body } — the app's words for a message while it is not running.
         The app is the caller (the OS's), never a parameter */
      register: function (p, caller) {
        return ready.then(function () {
          if (!url || !caller) throw new Error('push unavailable');
          var r = regs[caller] || { topic: newTopic() };
          r.title = String((p && p.title) || '');
          r.body = String((p && p.body) || '');
          regs[caller] = r;
          save();
          return url + '/' + r.topic + '?up=1';
        });
      },

      unregister: function (p, caller) {
        return ready.then(function () {
          if (!regs[caller]) return false;
          delete regs[caller];
          save();
          return true;
        });
      },

      /* What came since the last message, handed over */
      _poll: function () {
        return ready.then(function () {
          var topics = Object.keys(regs).map(function (id) { return regs[id].topic; }).sort();
          if (!url || !topics.length) return 0;
          return Promise.resolve(invoke('push_poll', { topics: topics.join(','), since: since })).then(function (list) {
            var got = (Array.isArray(list) ? list : []).filter(function (m) { return m && m.event === 'message' && m.id && m.topic; });
            got.forEach(arrive);
            if (got.length) save();
            return got.length;
          });
        }).catch(function () { return 0; });
      },

      /* The app was removed: its topic goes */
      _forgetApp: function (appId) {
        if (!regs[appId]) return;
        delete regs[appId];
        save();
      },

      _ready: ready
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
