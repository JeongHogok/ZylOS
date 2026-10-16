// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: FileSystem service — mounted disk image I/O
// Scope: readDir, readFile, writeFile, mkdir, remove, rename. A third-party
//        app's paths are its own container's (iOS-like: it never sees the
//        user's shared folders, and its files go with it); system apps use the
//        shared files (same rule as runtime/services/fs, the bridge's container).
//        _handoff (the OS's alone — apps cannot call _ methods): files one app
//        gives another become the receiver's copies (fs.c Handoff)
// Dependency Direction: Domain -> invoker abstraction (injected), SYSTEM_APPS
// SOLID: SRP — FS operations only, DIP — depends on invoke abstraction
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.fs = function (deps) {
    var SYSTEM_APPS = deps.SYSTEM_APPS || [];

    /* Whose files an app's paths are: a third-party app's container, else the user's */
    function containerOf(appId) {
      return appId && SYSTEM_APPS.indexOf(appId) === -1 ? appId : null;
    }

    /* A file command in the caller's root: a third-party app's container */
    function fsCall(caller, cmd, args) {
      var c = containerOf(caller);
      if (c) args.container = c;
      return deps.invoke(cmd, args);
    }

    function visible(entries) {
      return (entries || []).filter(function (e) {
        return typeof ZylSecurity === 'undefined' || !ZylSecurity.isHiddenFromListing(e.name);
      });
    }

    return {
      getDirectory: function (p, caller) {
        return fsCall(caller, 'fs_read_dir', { path: p.path || '/' }).then(visible)
          .catch(function () { return []; });
      },
      getFileContent: function (p, caller) {
        return fsCall(caller, 'fs_read_file', { path: p.path || '' });
      },
      readBinary: function (p, caller) {
        return fsCall(caller, 'fs_read_binary', { path: p.path });
      },
      getAllData: function (p, caller) {
        return fsCall(caller, 'fs_read_dir', { path: '/' }).then(function (entries) {
          return { tree: { '/': visible(entries) }, unixTree: {}, fileContents: {} };
        }).catch(function () {
          return { tree: { '/': [] }, unixTree: {}, fileContents: {} };
        });
      },
      /* encoding 'base64': binary content (a photo); otherwise text as given */
      writeFile: function (p, caller) {
        return fsCall(caller, 'fs_write_file', { path: p.path, content: p.content, encoding: p.encoding || null });
      },
      mkdir: function (p, caller) {
        return fsCall(caller, 'fs_mkdir', { path: p.path });
      },
      remove: function (p, caller) {
        return fsCall(caller, 'fs_remove', { path: p.path });
      },
      rename: function (p, caller) {
        return fsCall(caller, 'fs_rename', { old_path: p.oldPath, new_path: p.newPath });
      },
      /* The OS delivering an intent: what fromApp named, as toApp's own copies —
         the paths in toApp's root, '' for one that could not be given */
      _handoff: function (fromApp, paths, toApp) {
        return deps.invoke('fs_handoff', { from: containerOf(fromApp), paths: paths, to: containerOf(toApp) })
          .then(function (got) { return Array.isArray(got) ? got : []; });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
