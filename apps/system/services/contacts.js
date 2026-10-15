// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Contacts service — CRUD for contact records
// Scope: getAll (by name), getById, create, update (only the fields given;
//        starred = favorite, shown by Phone), delete, search. Kept in the
//        OS's private settings store, not the user's Documents folder where any
//        app with file access could read it (same contract as
//        runtime/services/contacts); contacts left there by older builds are
//        taken in once
// Dependency Direction: Domain -> settings service, invoker abstraction
// SOLID: SRP — contacts management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.contacts = function (deps) {
    var invoke = deps.invoke;
    var settingsSvc = deps.settingsSvc;
    var FIELDS = ['name', 'phone', 'email'];
    var _list = null;   /* one copy, so two writes in flight never undo each other */

    /* Older builds kept one JSON file per contact in Documents/Contacts */
    function readOld() {
      return invoke('fs_read_dir', { path: 'Documents/Contacts' }).then(function (entries) {
        return Promise.all((entries || []).filter(function (e) { return /\.json$/.test(e.name); }).map(function (e) {
          return invoke('fs_read_file', { path: 'Documents/Contacts/' + e.name })
            .then(JSON.parse).catch(function () { return null; });
        }));
      }).then(function (list) { return list.filter(Boolean); }, function () { return []; });
    }

    function save() { settingsSvc._updateSetting('contacts', 'entries', JSON.stringify(_list)); }

    function load() {
      if (_list) return Promise.resolve(_list);
      return settingsSvc._getSetting('contacts').then(function (cat) {
        if (cat && cat.entries) return JSON.parse(cat.entries);
        return readOld().then(function (old) {
          var list = old.map(function (c) {
            return { id: String(c.id), name: c.name || '', phone: c.phone || '', email: c.email || '', starred: !!c.starred };
          });
          if (!_list) { _list = list; save(); }
          return _list;
        });
      }).then(function (list) {
        _list = _list || list;
        return _list;
      });
    }

    function find(list, id) {
      for (var i = 0; i < list.length; i++) if (list[i].id === id) return i;
      return -1;
    }

    /* Only the fields given change: starring from Phone keeps the name,
       editing in Contacts keeps the star */
    function apply(c, p) {
      FIELDS.forEach(function (k) { if (p[k] !== undefined) c[k] = String(p[k]); });
      if (p.starred !== undefined) c.starred = !!p.starred;
      return c;
    }

    function byName(a, b) { return a.name.localeCompare(b.name); }

    return {
      getAll: function () { return load().then(function (l) { return l.slice().sort(byName); }); },
      getById: function (p) {
        return load().then(function (l) { var i = find(l, p.id); return i < 0 ? null : l[i]; });
      },
      create: function (p) {
        return load().then(function (l) {
          var n = Date.now();
          while (find(l, 'c_' + n) >= 0) n++;
          var c = apply({ id: 'c_' + n, name: '', phone: '', email: '', starred: false }, p);
          l.push(c);
          save();
          return c;
        });
      },
      update: function (p) {
        return load().then(function (l) {
          var i = find(l, p.id);
          if (i < 0) throw new Error('no such contact');
          apply(l[i], p);
          save();
          return l[i];
        });
      },
      'delete': function (p) {
        return load().then(function (l) {
          var i = find(l, p.id);
          if (i < 0) return false;
          l.splice(i, 1);
          save();
          return true;
        });
      },
      search: function (p) {
        var q = String(p.query || '').toLowerCase();
        return load().then(function (l) {
          return l.filter(function (c) {
            return c.name.toLowerCase().indexOf(q) !== -1 || c.phone.indexOf(q) !== -1;
          }).sort(byName);
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
