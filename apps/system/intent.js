// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Intent Resolution
//
// Role: Which app handles an intent an app started — the OS's decision
// Scope: Explicit (targetApp) or implicit (action + mimeType against the
//        "intents" each app.json declares — declared, not registered at run
//        time, so an app that is not running can still be chosen). The
//        starting app is not offered itself. One handler → launch it, several
//        → a choice the starting app shows (the pick must be one offered, by
//        token), none → "no app". What the handler gets: action, data,
//        mimeType, extras and sourceApp (the OS's, not the request's).
//        Same rules as runtime/wam/src/intent/intent.c
// Dependency Direction: Domain -> ZylAppRegistry
// SOLID: SRP — intent resolution only
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylIntent = (function () {
  'use strict';

  var _choice = null;   /* the one open choice: { token, source, targets, intent } */
  var _nextToken = 1;

  /* "*\/*", exact, or "type/*" for any subtype */
  function mimeMatches(filter, mime) {
    if (filter === '*/*' || filter === mime) return true;
    var slash = filter.indexOf('/');
    return slash > 0 && filter.slice(slash) === '/*' && mime.split('/')[0] === filter.slice(0, slash);
  }

  function resolve(source, intent) {
    if (intent.targetApp) {
      return ZylAppRegistry.getApp(intent.targetApp) ? [String(intent.targetApp)] : [];
    }
    var mime = intent.mimeType || '*/*';
    return ZylAppRegistry.getAllIds().filter(function (id) {
      if (id === source) return false;
      var filters = ZylAppRegistry.getApp(id).intents || [];
      for (var i = 0; i < filters.length; i++) {
        if (filters[i].action === intent.action && mimeMatches(filters[i].mimeType || '*/*', mime)) return true;
      }
      return false;
    }).sort();
  }

  function deliverable(source, intent) {
    return {
      action: String(intent.action || ''),
      data: intent.data == null ? '' : String(intent.data),
      mimeType: intent.mimeType ? String(intent.mimeType) : '',
      extras: (intent.extras && typeof intent.extras === 'object') ? intent.extras : {},
      sourceApp: source || ''
    };
  }

  /**
   * An app started an intent.
   * @returns {Object} { launch, intent } | { choose: { token, choices: [{ appId, name }] } } | { none: true }
   */
  function start(source, intent) {
    if (!intent || (!intent.action && !intent.targetApp)) return { none: true };
    var targets = resolve(source, intent);
    if (targets.length === 0) return { none: true };
    var d = deliverable(source, intent);
    if (targets.length === 1) return { launch: targets[0], intent: d };
    var token = String(_nextToken++);
    _choice = { token: token, source: source, targets: targets, intent: d };
    return {
      choose: {
        token: token,
        choices: targets.map(function (id) { return { appId: id, name: ZylAppRegistry.getApp(id).name }; })
      }
    };
  }

  /**
   * The starting app reports the user's pick. Only the latest choice, from the
   * app it was offered to, for an app that was offered.
   * @returns {Object|null} { launch, intent }
   */
  function choose(source, token, appId) {
    var c = _choice;
    if (!c || c.token !== token || c.source !== source || c.targets.indexOf(appId) === -1) return null;
    _choice = null;
    return { launch: appId, intent: c.intent };
  }

  return {
    start: start,
    choose: choose
  };
})();
