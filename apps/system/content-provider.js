// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Content Provider
//
// Role: Android-style ContentProvider — structured inter-app data sharing
// Scope: 앱이 자신의 데이터를 다른 앱에 노출하는 게이트웨이.
//        직접 파일 접근 대신 쿼리 기반 접근 제어.
// Dependency Direction: Domain -> ZylPermissions (권한 체크)
// SOLID: SRP — 데이터 공유 라우팅만, OCP — 새 프로바이더 등록으로 확장
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylContentProvider = (function () {
  'use strict';

  var _providers = {}; /* authority → { query, insert, update, delete, getType } */

  /**
   * 프로바이더 등록. 각 앱이 자신의 데이터를 공유할 때 호출.
   * @param {string} authority - "com.zylos.contacts" 같은 고유 식별자
   * @param {Object} impl - { query, insert, update, delete, getType }
   */
  function registerProvider(authority, impl) {
    if (!authority || !impl) return;
    _providers[authority] = impl;
  }

  /**
   * URI 파싱: "content://authority/path?query"
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
   * 데이터 쿼리 — 권한 체크 후 프로바이더에 위임.
   * @param {string} callerAppId - 호출하는 앱 ID (권한 체크)
   * @param {string} uri - "content://com.zylos.contacts/all"
   * @param {Object} [projection] - 반환할 필드 목록
   * @returns {Promise} query 결과
   */
  function query(callerAppId, uri, projection) {
    var parsed = parseUri(uri);
    if (!parsed) return Promise.resolve({ error: 'Invalid URI' });

    var provider = _providers[parsed.authority];
    if (!provider || !provider.query) {
      return Promise.resolve({ error: 'Provider not found: ' + parsed.authority });
    }

    /* 권한 체크: 호출 앱이 해당 데이터에 접근 가능한지 */
    if (typeof ZylPermissions !== 'undefined' && callerAppId) {
      var requiredPerm = _getRequiredPermission(parsed.authority);
      if (requiredPerm && !ZylPermissions.hasPermission(callerAppId, requiredPerm)) {
        return Promise.resolve({ error: 'Permission denied', permission: requiredPerm });
      }
    }

    return provider.query(parsed.path, parsed.query, projection);
  }

  /**
   * 데이터 삽입.
   */
  function insert(callerAppId, uri, values) {
    var parsed = parseUri(uri);
    if (!parsed) return Promise.resolve({ error: 'Invalid URI' });
    var provider = _providers[parsed.authority];
    if (!provider || !provider.insert) return Promise.resolve({ error: 'Not supported' });

    var requiredPerm = _getRequiredPermission(parsed.authority);
    if (typeof ZylPermissions !== 'undefined' && callerAppId && requiredPerm) {
      if (!ZylPermissions.hasPermission(callerAppId, requiredPerm)) {
        return Promise.resolve({ error: 'Permission denied' });
      }
    }

    return provider.insert(parsed.path, values);
  }

  /* ─── 권한 매핑 ─── */
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
    parseUri: parseUri
  };
})();
