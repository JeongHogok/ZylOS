// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Intent Resolution
//
// Role: Which app handles an intent an app started — the OS's decision
// Scope: Explicit (targetApp) or implicit (action + mimeType against the
//        "intents" each app.json declares — declared, not registered at run
//        time, so an app that is not running can still be chosen). The
//        starting app is not offered itself; a PICK only by a system app (what
//        is picked is picked on an OS screen). One handler → launch it, several
//        → a choice the starting app shows (the pick must be one offered, by
//        token), none → "no app". What the handler gets: action, data,
//        mimeType, extras, files and sourceApp (the OS's, not the request's).
//        Files reach the handler as its own copies (deliver, the file
//        service's handoff). A PICK's handler answers once (result), and the
//        app that asked gets { requestCode, files } the same way.
//        Same rules as runtime/wam/src/intent/intent.c and wam.c
// Dependency Direction: Domain -> ZylAppRegistry, the file service's handoff (useHandoff)
// SOLID: SRP — intent resolution only
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylIntent = (function () {
  'use strict';

  var PICK = 'zyl.intent.action.PICK';
  var MAX_FILES = 10;

  var _choice = null;   /* the one open choice: { token, source, targets, intent, requestCode } */
  var _pending = null;  /* the one PICK waiting for its answer: { token, requester, handler, requestCode } */
  var _nextToken = 1;
  var _handoff = null;  /* (fromApp, paths, toApp) → Promise<paths in toApp's root> */

  /* "*\/*", exact, or "type/*" for any subtype */
  function mimeMatches(filter, mime) {
    if (filter === '*/*' || filter === mime) return true;
    var slash = filter.indexOf('/');
    return slash > 0 && filter.slice(slash) === '/*' && mime.split('/')[0] === filter.slice(0, slash);
  }

  function resolve(source, intent) {
    var pick = intent.action === PICK;
    if (intent.targetApp) {
      var t = ZylAppRegistry.getApp(intent.targetApp);
      return t && (!pick || t.system) ? [String(intent.targetApp)] : [];
    }
    var mime = intent.mimeType || '*/*';
    return ZylAppRegistry.getAllIds().filter(function (id) {
      if (id === source || (pick && !ZylAppRegistry.getApp(id).system)) return false;
      var filters = ZylAppRegistry.getApp(id).intents || [];
      for (var i = 0; i < filters.length; i++) {
        if (filters[i].action === intent.action && mimeMatches(filters[i].mimeType || '*/*', mime)) return true;
      }
      return false;
    }).sort();
  }

  /* Strings only, MAX_FILES at most — anything else is dropped */
  function files(list) {
    return (Array.isArray(list) ? list : []).filter(function (f) {
      return typeof f === 'string' && f !== '';
    }).slice(0, MAX_FILES);
  }

  function deliverable(source, intent) {
    return {
      action: String(intent.action || ''),
      data: intent.data == null ? '' : String(intent.data),
      mimeType: intent.mimeType ? String(intent.mimeType) : '',
      extras: (intent.extras && typeof intent.extras === 'object') ? intent.extras : {},
      files: files(intent.files),
      sourceApp: source || ''
    };
  }

  /* The handler gets it; a PICK waits for its answer, from that handler only */
  function handOver(source, target, intent, requestCode) {
    if (intent.action === PICK) {
      _pending = { token: 'r' + (_nextToken++), requester: source, handler: target,
                   requestCode: String(requestCode || '') };
      intent.resultToken = _pending.token;
    }
    return { launch: target, intent: intent, from: source };
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
    if (targets.length === 1) return handOver(source, targets[0], d, intent.requestCode);
    var token = String(_nextToken++);
    _choice = { token: token, source: source, targets: targets, intent: d, requestCode: intent.requestCode };
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
   * @returns {Object|null} { launch, intent, from }
   */
  function choose(source, token, appId) {
    var c = _choice;
    if (!c || c.token !== token || c.source !== source || c.targets.indexOf(appId) === -1) return null;
    _choice = null;
    return handOver(source, appId, c.intent, c.requestCode);
  }

  /**
   * A PICK's handler answers: only the pending one's, with its token.
   * @returns {Object|null} { launch: the app that asked, result: { requestCode, files }, from }
   */
  function result(source, token, picked) {
    var p = _pending;
    if (!p || p.token !== token || p.handler !== source) return null;
    _pending = null;
    return { launch: p.requester, result: { requestCode: p.requestCode, files: files(picked) }, from: source };
  }

  /**
   * What start/choose/result decided, ready to hand over: the files the sender
   * named become the receiver's own copies. Without the file service, none.
   * @returns {Promise} the same plan, its files now the receiver's
   */
  function deliver(plan) {
    var body = plan && (plan.intent || plan.result);
    if (!body || !body.files.length) return Promise.resolve(plan);
    var copy = _handoff ? _handoff(plan.from, body.files, plan.launch) : Promise.resolve([]);
    return copy.then(function (got) {
      body.files = files(got);
      return plan;
    }, function () {
      body.files = [];
      return plan;
    });
  }

  return {
    start: start,
    choose: choose,
    result: result,
    deliver: deliver,
    /* The OS wires the file service in (services.js) */
    useHandoff: function (fn) { _handoff = fn; }
  };
})();
