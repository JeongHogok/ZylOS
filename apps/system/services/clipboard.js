// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Clipboard service — copy/paste with history, sensitive data auto-clear,
//       change listeners (same copy/paste contract as runtime/services/clipboard)
// Scope: App-to-app clipboard data management, history ring buffer,
//        sensitive data timeout, sourceApp tracking (the caller the OS names)
// Dependency Direction: Domain -> none
// SOLID: SRP — clipboard data storage/retrieval only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.clipboard = function (deps) {
    void deps;

    /* ─── Clipboard state ─── */
    var _data = {
      text:      '',
      mimeType:  'text/plain',
      sourceApp: '',
      timestamp: 0,
      sensitive: false
    };

    /* ─── History ring buffer ─── */
    var MAX_HISTORY = 20;
    var MAX_TEXT = 1024 * 1024;   /* as clipboard.c */
    var _history = [];

    /* ─── Sensitive data auto-clear timer ─── */
    var SENSITIVE_CLEAR_MS = 60000; /* 60 seconds */
    var _sensitiveTimer = null;

    /* ─── Change listeners ─── */
    var _listeners = []; /* { appId, callback } — for internal use */

    function _pushHistory(entry) {
      /* Do not store sensitive data in history */
      if (entry.sensitive) return;
      _history.push({
        text:      entry.text,
        mimeType:  entry.mimeType,
        sourceApp: entry.sourceApp,
        timestamp: entry.timestamp
      });
      if (_history.length > MAX_HISTORY) {
        _history.shift();
      }
    }

    function _notifyChange() {
      for (var i = 0; i < _listeners.length; i++) {
        try {
          if (typeof _listeners[i].callback === 'function') {
            _listeners[i].callback({
              sourceApp: _data.sourceApp,
              mimeType: _data.mimeType,
              timestamp: _data.timestamp,
              hasData: _data.timestamp > 0 && _data.text.length > 0
            });
          }
        } catch (e) { /* ignore listener errors */ }
      }
    }

    function _scheduleSensitiveClear() {
      if (_sensitiveTimer) clearTimeout(_sensitiveTimer);
      _sensitiveTimer = setTimeout(function () {
        _data = { text: '', mimeType: 'text/plain', sourceApp: '', timestamp: 0, sensitive: false };
        _sensitiveTimer = null;
        _notifyChange();
      }, SENSITIVE_CLEAR_MS);
    }

    return {
      /**
       * Copy data to clipboard (same contract as runtime/services/clipboard).
       * p: { text, mimeType?, sensitive? }; caller: the copying app, from the OS
       * sensitive: marks data for auto-clear after 60s (passwords, tokens)
       */
      copy: function (p, caller) {
        if (!p || typeof p.text !== 'string' || p.text.length > MAX_TEXT) return false;

        /* Clear any previous sensitive timer */
        if (_sensitiveTimer) {
          clearTimeout(_sensitiveTimer);
          _sensitiveTimer = null;
        }

        var entry = {
          text:      p.text,
          mimeType:  p.mimeType  || 'text/plain',
          sourceApp: caller || '',
          timestamp: Date.now(),
          sensitive: !!p.sensitive
        };

        /* Store in history before replacing current */
        if (_data.timestamp > 0 && _data.text.length > 0) {
          _pushHistory(_data);
        }

        _data = entry;

        /* Schedule auto-clear for sensitive data */
        if (entry.sensitive) {
          _scheduleSensitiveClear();
        }

        _notifyChange();
        return true;
      },

      /**
       * Paste (read) clipboard data: { text, mimeType, sourceApp, timestamp }, {} when empty.
       * Every app may read; sensitive data is auto-cleared.
       */
      paste: function () {
        if (!_data.timestamp) return {};
        return {
          text:      _data.text,
          mimeType:  _data.mimeType,
          sourceApp: _data.sourceApp,
          timestamp: _data.timestamp
        };
      },

      /**
       * Clear current clipboard data.
       */
      clear: function () {
        if (_sensitiveTimer) {
          clearTimeout(_sensitiveTimer);
          _sensitiveTimer = null;
        }
        _data = { text: '', mimeType: 'text/plain', sourceApp: '', timestamp: 0, sensitive: false };
        _notifyChange();
        return { success: true };
      },

      /**
       * Check if clipboard has data.
       */
      hasData: function () {
        return _data.timestamp > 0 && _data.text.length > 0;
      },

      /* ─── NEW: History API ─── */

      /**
       * Get clipboard history (excludes sensitive entries).
       * p: { limit? }
       */
      getHistory: function (p) {
        var limit = (p && p.limit) ? p.limit : _history.length;
        return _history.slice(-limit);
      },

      /**
       * Clear clipboard history.
       */
      clearHistory: function () {
        _history = [];
        return { success: true };
      },

      /**
       * Paste from history by index (0 = most recent history entry).
       * p: { index }
       */
      pasteFromHistory: function (p) {
        var idx = (p && typeof p.index === 'number') ? p.index : 0;
        var pos = _history.length - 1 - idx;
        if (pos < 0 || pos >= _history.length) return null;
        return _history[pos];
      },

      /* ─── NEW: Change listener API ─── */

      /**
       * Register a clipboard change listener.
       * p: { appId, callback }
       * Returns { listenerId }
       */
      addChangeListener: function (p) {
        if (!p || typeof p.callback !== 'function') {
          return { error: 'callback is required' };
        }
        /* Reuse nullified slots to prevent unbounded growth */
        var id = -1;
        for (var j = 0; j < _listeners.length; j++) {
          if (_listeners[j].callback === null) { id = j; break; }
        }
        if (id === -1) {
          id = _listeners.length;
          _listeners.push({ appId: p.appId || '', callback: p.callback });
        } else {
          _listeners[id] = { appId: p.appId || '', callback: p.callback };
        }
        return { listenerId: id };
      },

      /**
       * Remove a clipboard change listener.
       * p: { listenerId }
       */
      removeChangeListener: function (p) {
        if (p && typeof p.listenerId === 'number' && _listeners[p.listenerId]) {
          _listeners[p.listenerId] = { appId: '', callback: null };
        }
        return { success: true };
      }
    };
  };

})(window.ZylServiceModules = window.ZylServiceModules || {});
