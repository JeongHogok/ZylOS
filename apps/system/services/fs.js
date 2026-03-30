// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: FileSystem service — mounted disk image I/O
// Scope: readDir, readFile, writeFile, mkdir, remove, rename
// Dependency Direction: Domain -> invoker abstraction (injected)
// SOLID: SRP — FS operations only, DIP — depends on invoke abstraction
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.fs = function (deps) {
    var invoke = deps.invoke;

    function getDirectory(path) {
      return invoke('fs_read_dir', { path: path || '/' }).then(function (entries) {
        if (!entries || typeof ZylSecurity === 'undefined') return entries;
        return entries.filter(function (e) {
          return !ZylSecurity.isHiddenFromListing(e.name);
        });
      });
    }

    function getUnixDirectory(path) {
      var p = invoke('fs_read_dir', { path: path || '/' });
      if (p && typeof p.then === 'function') {
        return p.then(function (entries) {
          return (entries || []).filter(function (e) {
            return typeof ZylSecurity === 'undefined' || !ZylSecurity.isHiddenFromListing(e.name);
          }).map(function (e) {
            return (e.is_dir ? 'drwxr-xr-x' : '-rw-r--r--') +
              '  user user  ' + (e.size || 0) + '  ' + e.name;
          }).join('\n');
        });
      }
      return null;
    }

    function getFileContent(path) {
      return invoke('fs_read_file', { path: path || '' });
    }

    function getAllData() {
      return invoke('fs_read_dir', { path: '/' }).then(function (entries) {
        var filtered = (entries || []).filter(function (e) {
          return typeof ZylSecurity === 'undefined' || !ZylSecurity.isHiddenFromListing(e.name);
        });
        return { tree: { '/': filtered }, unixTree: {}, fileContents: {} };
      }).catch(function () {
        return { tree: { '/': [] }, unixTree: {}, fileContents: {} };
      });
    }

    function writeFile(params) {
      return invoke('fs_write_file', { path: params.path, content: params.content });
    }

    function mkdirFn(params) {
      return invoke('fs_mkdir', { path: params.path });
    }

    function remove(params) {
      return invoke('fs_remove', { path: params.path });
    }

    function rename(params) {
      return invoke('fs_rename', { old_path: params.oldPath, new_path: params.newPath });
    }

    return {
      getDirectory:     function (p) { return getDirectory(p.path); },
      getUnixDirectory: function (p) { return getUnixDirectory(p.path); },
      getFileContent:   function (p) { return getFileContent(p.path); },
      readBinary:       function (p) { return invoke('fs_read_binary', { path: p.path }); },
      getAllData:        function ()  { return getAllData(); },
      writeFile:        function (p) { return writeFile(p); },
      mkdir:            function (p) { return mkdirFn(p); },
      remove:           function (p) { return remove(p); },
      rename:           function (p) { return rename(p); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
