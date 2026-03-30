// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Logger service — in-memory log buffer
// Scope: log, getLevel, setLevel, getRecent
// Dependency Direction: Domain -> none (in-memory)
// SOLID: SRP — log collection only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.logger = function (/* deps */) {
    var logBuffer = [];
    var logLevel = 'INFO';
    var LOG_LEVELS = { 'VERBOSE': 0, 'DEBUG': 1, 'INFO': 2, 'WARN': 3, 'ERROR': 4 };
    var MAX_LOG_LINES = 1000;

    function addEntry(level, tag, message) {
      var entry = { level: level, tag: tag, message: message, timestamp: Date.now() };
      if ((LOG_LEVELS[level] || 0) >= (LOG_LEVELS[logLevel] || 0)) {
        logBuffer.push(entry);
        if (logBuffer.length > MAX_LOG_LINES) {
          logBuffer = logBuffer.slice(-MAX_LOG_LINES);
        }
      }
    }

    return {
      log: function (p) {
        var level = (p.level || 'INFO').toUpperCase();
        addEntry(level, p.tag || 'app', p.message || '');
        return Promise.resolve(true);
      },
      getLevel: function () { return Promise.resolve(logLevel); },
      setLevel: function (p) {
        var lv = (p.level || 'INFO').toUpperCase();
        if (LOG_LEVELS[lv] !== undefined) logLevel = lv;
        return Promise.resolve(logLevel);
      },
      getRecent: function (p) {
        var count = parseInt(p.count, 10) || 50;
        return Promise.resolve(logBuffer.slice(-count));
      },
      _addEntry: addEntry
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
