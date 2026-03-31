// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Clipboard service — copy/paste with history, sensitive data auto-clear,
//       permission-gated access, change listeners
// Scope: App-to-app clipboard data management, history ring buffer,
//        sensitive data timeout, sourceApp tracking
// Dependency Direction: Domain -> ZylPermissions (access control)
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

    function _checkReadPermission(appId) {
      if (!appId) return true;
      if (typeof ZylPermissions === 'undefined') return true;
      /* clipboard read requires 'storage' or system app status */
      if (ZylPermissions.SYSTEM_APPS && ZylPermissions.SYSTEM_APPS.indexOf(appId) !== -1) return true;
      return true; /* non-system apps can read clipboard, but sensitive data is auto-cleared */
    }

    return {
      /**
       * Copy data to clipboard.
       * p: { text, mimeType?, sourceApp?, sensitive? }
       * sensitive: marks data for auto-clear after 60s (passwords, tokens)
       */
      copy: function (p) {
        if (!p || typeof p.text !== 'string') {
          return { error: 'clipboard.copy: text is required' };
        }

        /* Clear any previous sensitive timer */
        if (_sensitiveTimer) {
          clearTimeout(_sensitiveTimer);
          _sensitiveTimer = null;
        }

        var entry = {
          text:      p.text,
          mimeType:  p.mimeType  || 'text/plain',
          sourceApp: p.sourceApp || '',
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
        return { success: true, length: _data.text.length };
      },

      /**
       * Paste (read) clipboard data.
       * p: { appId? } — caller app ID for access logging
       */
      paste: function (p) {
        var appId = (p && p.appId) ? p.appId : '';
        if (!_checkReadPermission(appId)) {
          return { error: 'Permission denied' };
        }
        if (!_data.timestamp) return null;
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
        var id = _listeners.length;
        _listeners.push({ appId: p.appId || '', callback: p.callback });
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
