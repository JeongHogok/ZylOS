// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Policy
//
// Role: First-use consent — the OS asks the user before an app first uses a
//       dangerous permission (ZylPermissions.DANGEROUS_PERMISSIONS)
// Scope: ask(appId, permission) → Promise<boolean>. One question per app and
//        permission at a time (later asks wait for the same answer), announced
//        as the window event zyl:permission-ask { token, appId, name, permission }
//        that the container relays to the system UI, which draws the sheet.
//        answer(from, token, allow) counts only from the system UI and only for a
//        question still open; the answer is recorded (useRecorder — settings
//        app_grants / app_permissions) so it is not asked again. A question no
//        one answers in ASK_TIMEOUT_MS is a no this time, not recorded.
//        The sheet is never the app's own page: there the app could answer for
//        the user — the device's WAM (wam.c consent) follows the same rules
// Dependency Direction: Domain -> ZylShell (who the system UI is), ZylAppRegistry (names)
// SOLID: SRP — asking and answering only; what needs asking is ZylPermissions'
// ----------------------------------------------------------

window.ZylConsent = (function () {
  'use strict';

  var ASK_TIMEOUT_MS = 120000;
  var _open = {};        /* token → { appId, permission, waiters: [fn], timer } */
  var _byKey = {};       /* appId + '\n' + permission → token */
  var _record = null;    /* function (appId, permission, allow) */

  function newToken() {
    var bytes = new Uint8Array(16), out = '';
    window.crypto.getRandomValues(bytes);
    for (var i = 0; i < bytes.length; i++) out += (bytes[i] < 16 ? '0' : '') + bytes[i].toString(16);
    return out;
  }

  function nameOf(appId) {
    var a = typeof ZylAppRegistry !== 'undefined' && ZylAppRegistry.getApp ? ZylAppRegistry.getApp(appId) : null;
    return (a && a.name) || appId;
  }

  function settle(token, allow, record) {
    var q = _open[token];
    if (!q) return false;
    delete _open[token];
    delete _byKey[q.appId + '\n' + q.permission];
    clearTimeout(q.timer);
    if (record && _record) _record(q.appId, q.permission, allow);
    q.waiters.forEach(function (fn) { fn(allow); });
    return true;
  }

  function ask(appId, permission) {
    return new Promise(function (resolve) {
      var key = appId + '\n' + permission, token = _byKey[key];
      if (token) { _open[token].waiters.push(resolve); return; }
      token = newToken();
      _byKey[key] = token;
      _open[token] = { appId: appId, permission: permission, waiters: [resolve],
                       timer: setTimeout(function () { settle(token, false, false); }, ASK_TIMEOUT_MS) };
      if (typeof window.dispatchEvent === 'function' && typeof CustomEvent === 'function') {
        window.dispatchEvent(new CustomEvent('zyl:permission-ask', { detail:
          { token: token, appId: appId, name: nameOf(appId), permission: permission } }));
      }
    });
  }

  /* The system UI's answer; anyone else's is nothing */
  function answer(from, token, allow) {
    if (typeof ZylShell === 'undefined' || from !== ZylShell.systemUi()) return false;
    return settle(String(token || ''), allow === true, true);
  }

  return {
    ask: ask,
    answer: answer,
    useRecorder: function (fn) { _record = fn; }
  };
})();
