// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Contacts service — CRUD for contact records
// Scope: getAll, getById, create, update, delete, search
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — contacts management only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.contacts = function (deps) {
    var invoke = deps.invoke;

    function getAll() {
      return invoke('fs_read_dir', { path: 'Documents/Contacts' }).then(function (entries) {
        if (!entries || entries.length === 0) return [];
        var promises = entries.filter(function (e) { return e.name.indexOf('.json') !== -1; }).map(function (e) {
          return invoke('fs_read_file', { path: 'Documents/Contacts/' + e.name }).then(function (content) {
            try { return JSON.parse(content); } catch (err) { return null; }
          });
        });
        return Promise.all(promises).then(function (results) {
          return results.filter(Boolean).sort(function (a, b) { return (a.name || '').localeCompare(b.name || ''); });
        });
      }).catch(function () { return []; });
    }

    return {
      getAll: function () { return getAll(); },
      getById: function (p) {
        return invoke('fs_read_file', { path: 'Documents/Contacts/' + p.id + '.json' }).then(function (c) {
          try { return JSON.parse(c); } catch (e) { return null; }
        });
      },
      create: function (p) {
        var id = 'c_' + Date.now();
        var contact = { id: id, name: p.name || '', phone: p.phone || '', email: p.email || '' };
        return invoke('fs_mkdir', { path: 'Documents/Contacts' }).then(function () {
          return invoke('fs_write_file', { path: 'Documents/Contacts/' + id + '.json', content: JSON.stringify(contact) });
        }).then(function () { return contact; });
      },
      update: function (p) {
        var contact = { id: p.id, name: p.name || '', phone: p.phone || '', email: p.email || '' };
        return invoke('fs_write_file', { path: 'Documents/Contacts/' + p.id + '.json', content: JSON.stringify(contact) }).then(function () { return contact; });
      },
      'delete': function (p) {
        return invoke('fs_remove', { path: 'Documents/Contacts/' + p.id + '.json' });
      },
      search: function (p) {
        return getAll().then(function (all) {
          var q = (p.query || '').toLowerCase();
          return all.filter(function (c) {
            return (c.name || '').toLowerCase().indexOf(q) !== -1 || (c.phone || '').indexOf(q) !== -1;
          });
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
