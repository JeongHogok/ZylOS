// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Content Provider
//
// Role: Android-style ContentProvider — structured inter-app data sharing
// Scope: Full CRUD (query/insert/update/delete), URI parsing,
//        permission-gated access, change observers
// Dependency Direction: Domain -> ZylPermissions (permission check)
// SOLID: SRP — data sharing routing only, OCP — new providers via registration
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylContentProvider = (function () {
  'use strict';

  var _providers = {}; /* authority → { query, insert, update, delete, getType } */

  /* ─── Change observers ─── */
  var _observers = []; /* { id, uri, callback, active } */
  var _nextObserverId = 1;

  /**
   * Register a provider. Each app exposes its data through this.
   * @param {string} authority - "com.zylos.contacts" unique identifier
   * @param {Object} impl - { query, insert, update, delete, getType }
   */
  function registerProvider(authority, impl) {
    if (!authority || !impl) return;
    _providers[authority] = impl;
  }

  /**
   * URI parsing: "content://authority/path?query"
   */
  function parseUri(uri) {
    if (!uri || uri.indexOf('content://') !== 0) return null;
    var rest = uri.substring(10); /* remove "content://" */
    var authEnd = rest.indexOf('/');
    if (authEnd === -1) return { authority: rest, path: '', query: '' };
    var authority = rest.substring(0, authEnd);
    var pathAndQuery = rest.substring(authEnd + 1);
    var qmark = pathAndQuery.indexOf('?');
    var path = qmark === -1 ? pathAndQuery : pathAndQuery.substring(0, qmark);
    var query = qmark === -1 ? '' : pathAndQuery.substring(qmark + 1);
    return { authority: authority, path: path, query: query };
  }

  /**
   * Check permission for caller app against authority.
   * @returns {string|null} error string, or null if allowed
   */
  function _checkPermission(callerAppId, authority) {
    if (typeof ZylPermissions === 'undefined' || !callerAppId) return null;
    var requiredPerm = _getRequiredPermission(authority);
    if (requiredPerm && !ZylPermissions.hasPermission(callerAppId, requiredPerm)) {
      return 'Permission denied';
    }
    return null;
  }

  /**
   * Notify change observers for a URI.
   */
  function _notifyChange(uri) {
    var parsed = parseUri(uri);
    if (!parsed) return;
    for (var i = 0; i < _observers.length; i++) {
      var obs = _observers[i];
      if (!obs.active) continue;
      var obsParsed = parseUri(obs.uri);
      if (!obsParsed) continue;
      /* Match by authority, and optionally by path prefix */
      if (obsParsed.authority === parsed.authority) {
        if (!obsParsed.path || parsed.path.indexOf(obsParsed.path) === 0) {
          try {
            obs.callback({ uri: uri, timestamp: Date.now() });
          } catch (e) { /* ignore observer errors */ }
        }
      }
    }
  }

  /**
   * Query data — permission check then delegate to provider.
   * @param {string} callerAppId - Caller app ID (for permission check)
   * @param {string} uri - "content://com.zylos.contacts/all"
   * @param {Object} [projection] - Fields to return
   * @returns {Promise}
   */
  function query(callerAppId, uri, projection) {
    var parsed = parseUri(uri);
    if (!parsed) return Promise.resolve({ error: 'Invalid URI' });

    var provider = _providers[parsed.authority];
    if (!provider || !provider.query) {
      return Promise.resolve({ error: 'Provider not found: ' + parsed.authority });
    }

    var permErr = _checkPermission(callerAppId, parsed.authority);
    if (permErr) return Promise.resolve({ error: permErr });

    return provider.query(parsed.path, parsed.query, projection);
  }

  /**
   * Insert data.
   * @param {string} callerAppId
   * @param {string} uri
   * @param {Object} values
   * @returns {Promise}
   */
  function insert(callerAppId, uri, values) {
    var parsed = parseUri(uri);
    if (!parsed) return Promise.resolve({ error: 'Invalid URI' });
    var provider = _providers[parsed.authority];
    if (!provider || !provider.insert) return Promise.resolve({ error: 'Not supported' });

    var permErr = _checkPermission(callerAppId, parsed.authority);
    if (permErr) return Promise.resolve({ error: permErr });

    var result = provider.insert(parsed.path, values);
    /* Notify observers of data change */
    Promise.resolve(result).then(function () { _notifyChange(uri); });
    return result;
  }

  /**
   * Update data.
   * @param {string} callerAppId
   * @param {string} uri
   * @param {Object} values - Fields to update
   * @param {Object} [selection] - Filter criteria { where, args }
   * @returns {Promise}
   */
  function update(callerAppId, uri, values, selection) {
    var parsed = parseUri(uri);
    if (!parsed) return Promise.resolve({ error: 'Invalid URI' });
    var provider = _providers[parsed.authority];
    if (!provider || !provider.update) return Promise.resolve({ error: 'Not supported' });

    var permErr = _checkPermission(callerAppId, parsed.authority);
    if (permErr) return Promise.resolve({ error: permErr });

    var result = provider.update(parsed.path, values, selection);
    Promise.resolve(result).then(function () { _notifyChange(uri); });
    return result;
  }

  /**
   * Delete data.
   * @param {string} callerAppId
   * @param {string} uri
   * @param {Object} [selection] - Filter criteria { where, args }
   * @returns {Promise}
   */
  function del(callerAppId, uri, selection) {
    var parsed = parseUri(uri);
    if (!parsed) return Promise.resolve({ error: 'Invalid URI' });
    var provider = _providers[parsed.authority];
    if (!provider || !provider['delete']) return Promise.resolve({ error: 'Not supported' });

    var permErr = _checkPermission(callerAppId, parsed.authority);
    if (permErr) return Promise.resolve({ error: permErr });

    var result = provider['delete'](parsed.path, selection);
    Promise.resolve(result).then(function () { _notifyChange(uri); });
    return result;
  }

  /**
   * Get MIME type for a URI.
   * @param {string} uri
   * @returns {string|null}
   */
  function getType(uri) {
    var parsed = parseUri(uri);
    if (!parsed) return null;
    var provider = _providers[parsed.authority];
    if (!provider || !provider.getType) return null;
    return provider.getType(parsed.path);
  }

  /* ─── Change Observer API ─── */

  /**
   * Register a content change observer.
   * @param {string} uri - URI pattern to observe (authority + optional path prefix)
   * @param {Function} callback - function({ uri, timestamp }) called on change
   * @returns {number} observerId for unregistration
   */
  function registerContentObserver(uri, callback) {
    var id = _nextObserverId++;
    _observers.push({ id: id, uri: uri, callback: callback, active: true });
    return id;
  }

  /**
   * Unregister a content change observer.
   * @param {number} observerId
   */
  function unregisterContentObserver(observerId) {
    for (var i = 0; i < _observers.length; i++) {
      if (_observers[i].id === observerId) {
        _observers[i].active = false;
        return;
      }
    }
  }

  /**
   * Manually notify observers of a content change.
   * Providers can call this after batch operations.
   * @param {string} uri
   */
  function notifyChange(uri) {
    _notifyChange(uri);
  }

  /* ─── Permission mapping ─── */
  var _permissionMap = {
    'com.zylos.contacts':  'contacts',
    'com.zylos.messaging': 'messaging',
    'com.zylos.gallery':   'storage',
    'com.zylos.music':     'storage',
    'com.zylos.files':     'storage'
  };

  function _getRequiredPermission(authority) {
    return _permissionMap[authority] || null;
  }

  return {
    registerProvider: registerProvider,
    query: query,
    insert: insert,
    update: update,
    'delete': del,
    getType: getType,
    parseUri: parseUri,
    registerContentObserver: registerContentObserver,
    unregisterContentObserver: unregisterContentObserver,
    notifyChange: notifyChange
  };
})();
