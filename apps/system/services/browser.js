// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Browser service — bookmark/quick-link CRUD via fs persistence
// Scope: getBookmarks, addBookmark, removeBookmark, getQuickLinks,
//        addQuickLink, removeQuickLink
// Dependency Direction: Domain -> invoke (fs backend)
// SOLID: SRP — browser bookmark/quick-link data only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  var BOOKMARKS_PATH = '/data/browser/bookmarks.json';
  var QUICKLINKS_PATH = '/data/browser/quicklinks.json';

  ns.browser = function (deps) {
    var invoke = deps.invoke;

    function readJson(path) {
      return invoke('fs_read_file', { path: path }).then(function (raw) {
        if (!raw) return [];
        try { return JSON.parse(raw); } catch (e) { return []; }
      });
    }

    function writeJson(path, data) {
      return invoke('fs_write_file', { path: path, content: JSON.stringify(data) });
    }

    return {
      getBookmarks: function () {
        return readJson(BOOKMARKS_PATH);
      },

      addBookmark: function (params) {
        var url = params && params.url;
        var title = params && params.title;
        if (!url) return Promise.resolve(false);
        return readJson(BOOKMARKS_PATH).then(function (list) {
          list.push({ url: url, title: title || url, createdAt: Date.now() });
          return writeJson(BOOKMARKS_PATH, list);
        });
      },

      removeBookmark: function (params) {
        var url = params && params.url;
        if (!url) return Promise.resolve(false);
        return readJson(BOOKMARKS_PATH).then(function (list) {
          var filtered = list.filter(function (b) { return b.url !== url; });
          return writeJson(BOOKMARKS_PATH, filtered);
        });
      },

      getQuickLinks: function () {
        return readJson(QUICKLINKS_PATH);
      },

      addQuickLink: function (params) {
        var url = params && params.url;
        var title = params && params.title;
        if (!url) return Promise.resolve(false);
        return readJson(QUICKLINKS_PATH).then(function (list) {
          list.push({ url: url, title: title || url, createdAt: Date.now() });
          return writeJson(QUICKLINKS_PATH, list);
        });
      },

      removeQuickLink: function (params) {
        var url = params && params.url;
        if (!url) return Promise.resolve(false);
        return readJson(QUICKLINKS_PATH).then(function (list) {
          var filtered = list.filter(function (q) { return q.url !== url; });
          return writeJson(QUICKLINKS_PATH, filtered);
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
