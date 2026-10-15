// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Messaging service — text message threads
// Scope: getThreads (newest first), getMessages, send, markRead, deleteThread
//        (same contract as runtime/services/messaging). The emulator has no
//        modem, so a sent message is recorded as not sent. Kept in the OS's
//        private settings store, not the user's Documents folder; threads left
//        there by older builds are taken in once
// Dependency Direction: Domain -> settings service, invoker abstraction
// SOLID: SRP — messaging only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.messaging = function (deps) {
    var invoke = deps.invoke;
    var settingsSvc = deps.settingsSvc;
    /* { threadId: { id, number, lastTime, lastMessage, unread, messages: [{ id, text, sent, time, status }] } } */
    var _threads = null;   /* one copy, so two writes in flight never undo each other */

    /* thread_<letters and digits of the number>, as messaging.c names them */
    function threadIdFor(number) {
      return 'thread_' + (String(number).replace(/[^0-9A-Za-z]/g, '') || 'unknown');
    }

    function save() { settingsSvc._updateSetting('messaging', 'threads', JSON.stringify(_threads)); }

    function record(threads, number, text, sent, status, time) {
      var id = threadIdFor(number);
      var t = threads[id] || (threads[id] = { id: id, number: number, lastTime: 0, lastMessage: '', unread: 0, messages: [] });
      var m = { id: 'm_' + time, text: text, sent: sent, time: time, status: status };
      t.messages.push(m);
      t.lastTime = time;
      t.lastMessage = text;
      if (!sent) t.unread++;
      return m;
    }

    /* Older builds kept one JSON file per thread in Documents/Messages */
    function readOld() {
      return invoke('fs_read_dir', { path: 'Documents/Messages' }).then(function (entries) {
        return Promise.all((entries || []).filter(function (e) { return /\.json$/.test(e.name); }).map(function (e) {
          return invoke('fs_read_file', { path: 'Documents/Messages/' + e.name })
            .then(JSON.parse).catch(function () { return null; });
        }));
      }).then(function (list) { return list.filter(Boolean); }, function () { return []; });
    }

    function load() {
      if (_threads) return Promise.resolve(_threads);
      return settingsSvc._getSetting('messaging').then(function (cat) {
        if (cat && cat.threads) return JSON.parse(cat.threads);
        return readOld().then(function (old) {
          var threads = {};
          old.forEach(function (t) {
            (t.messages || []).forEach(function (m) {
              record(threads, String(t.number || ''), String(m.text || ''), !!m.sent, m.sent ? '' : 'received', m.time || 0);
            });
          });
          Object.keys(threads).forEach(function (id) { threads[id].unread = 0; });
          if (!_threads) { _threads = threads; save(); }
          return _threads;
        });
      }).then(function (threads) {
        _threads = _threads || threads;
        return _threads;
      });
    }

    return {
      getThreads: function () {
        return load().then(function (threads) {
          return Object.keys(threads).map(function (id) {
            var t = threads[id];
            return { id: t.id, number: t.number, lastTime: t.lastTime, lastMessage: t.lastMessage, unread: t.unread };
          }).sort(function (a, b) { return b.lastTime - a.lastTime; });
        });
      },
      getMessages: function (p) {
        return load().then(function (threads) {
          return threads[p.threadId] ? threads[p.threadId].messages : [];
        });
      },
      send: function (p) {
        var number = String(p.number || '');
        var text = String(p.text || '');
        if (!number || !text) return Promise.reject(new Error('number and text required'));
        return load().then(function (threads) {
          var m = record(threads, number, text, true, 'failed', Date.now());   /* no modem: nothing left */
          save();
          return { id: m.id, text: m.text, sent: true, time: m.time, status: m.status, threadId: threadIdFor(number) };
        });
      },
      markRead: function (p) {
        return load().then(function (threads) {
          if (!threads[p.threadId]) return false;
          threads[p.threadId].unread = 0;
          save();
          return true;
        });
      },
      deleteThread: function (p) {
        return load().then(function (threads) {
          if (!threads[p.threadId]) return false;
          delete threads[p.threadId];
          save();
          return true;
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
