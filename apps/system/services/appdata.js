// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: App data service — each app's private key-value storage
// Scope: getItem ('' when absent), setItem (false over the 5 MiB quota or with
//        a bad key), removeItem, keys, clear. Whose data it is comes only from
//        the caller the OS names, so an app cannot even name another app's
//        data. One container per app (same contract as runtime/services/appdata)
// Dependency Direction: Domain -> invoker abstraction (appdata_load/appdata_save)
// SOLID: SRP — app private data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.appdata = function (deps) {
    var invoke = deps.invoke;
    var QUOTA = 5 * 1024 * 1024;
    var KEY_MAX = 256;
    var _apps = {};   /* app id → its data; one copy so two writes in flight never undo each other */
    var has = Object.prototype.hasOwnProperty;

    function load(app) {
      if (_apps[app]) return Promise.resolve(_apps[app]);
      return invoke('appdata_load', { appId: app }).then(function (text) {
        var data;
        try { data = text ? JSON.parse(text) : {}; } catch (e) { data = {}; }
        _apps[app] = _apps[app] || data;
        return _apps[app];
      });
    }

    function used(data) {
      var n = 0;
      Object.keys(data).forEach(function (k) { n += k.length + data[k].length; });
      return n;
    }

    function save(app) {
      return invoke('appdata_save', { appId: app, data: JSON.stringify(_apps[app]) });
    }

    function noCaller() { return Promise.reject(new Error('appdata needs the calling app')); }

    return {
      getItem: function (p, caller) {
        if (!caller) return noCaller();
        return load(caller).then(function (d) { return has.call(d, p.key) ? d[p.key] : ''; });
      },
      setItem: function (p, caller) {
        if (!caller) return noCaller();
        var key = p.key;
        if (typeof key !== 'string' || !key || key.length > KEY_MAX || typeof p.value !== 'string') {
          return Promise.resolve(false);
        }
        return load(caller).then(function (d) {
          var had = has.call(d, key), old = d[key];
          d[key] = p.value;
          if (used(d) > QUOTA) {
            if (had) d[key] = old; else delete d[key];
            return false;
          }
          return save(caller);
        });
      },
      removeItem: function (p, caller) {
        if (!caller) return noCaller();
        return load(caller).then(function (d) {
          if (!has.call(d, p.key)) return false;
          delete d[p.key];
          return save(caller);
        });
      },
      keys: function (p, caller) {
        if (!caller) return noCaller();
        return load(caller).then(function (d) { return Object.keys(d); });
      },
      clear: function (p, caller) {
        if (!caller) return noCaller();
        _apps[caller] = {};
        return save(caller);
      },
      /* The app was removed with its container: drop the copy held here */
      _forgetApp: function (appId) { delete _apps[appId]; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
