// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Messaging service — SMS threads and messages
// Scope: getThreads, getMessages, send, delete
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — messaging only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.messaging = function (deps) {
    var invoke = deps.invoke;

    return {
      getThreads: function () {
        return invoke('fs_read_dir', { path: 'Documents/Messages' }).then(function (entries) {
          if (!entries || entries.length === 0) return [];
          var promises = entries.filter(function (e) { return e.name.indexOf('.json') !== -1; }).map(function (e) {
            return invoke('fs_read_file', { path: 'Documents/Messages/' + e.name }).then(function (c) {
              try { return JSON.parse(c); } catch (err) { return null; }
            }).catch(function () { return null; });
          });
          return Promise.all(promises).then(function (results) {
            return results.filter(Boolean).sort(function (a, b) { return (b.lastTime || 0) - (a.lastTime || 0); });
          }).catch(function () { return []; });
        }).catch(function () { return []; });
      },
      getMessages: function (p) {
        return invoke('fs_read_file', { path: 'Documents/Messages/' + p.threadId + '.json' }).then(function (c) {
          try { var thread = JSON.parse(c); return thread.messages || []; } catch (e) { return []; }
        }).catch(function () { return []; });
      },
      send: function (p) {
        var threadId = 'thread_' + (p.number || '').replace(/[^0-9]/g, '');
        return invoke('fs_mkdir', { path: 'Documents/Messages' }).then(function () {
          return invoke('fs_read_file', { path: 'Documents/Messages/' + threadId + '.json' }).catch(function () { return null; });
        }).then(function (existing) {
          var thread;
          try { thread = existing ? JSON.parse(existing) : null; } catch (e) { thread = null; }
          if (!thread) thread = { id: threadId, number: p.number, name: p.name || '', messages: [], lastTime: 0 };
          var msg = { id: 'm_' + Date.now(), text: p.text, sent: true, time: Date.now() };
          thread.messages.push(msg);
          thread.lastTime = msg.time;
          thread.lastMessage = p.text;
          return invoke('fs_write_file', { path: 'Documents/Messages/' + threadId + '.json', content: JSON.stringify(thread) }).then(function () { return msg; }).catch(function () { return null; });
        }).catch(function () { return null; });
      },
      'delete': function (p) {
        if (p.threadId) return invoke('fs_remove', { path: 'Documents/Messages/' + p.threadId + '.json' });
        return Promise.resolve(false);
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
