// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Intent System
//
// Role: Android-style Intent for inter-app communication
// Scope: Explicit (target app) and implicit (action matching) intents,
//        data passing between apps, intent resolution
// Dependency Direction: Domain -> ZylAppRegistry (app lookup)
// SOLID: SRP — intent routing only, OCP — new actions via registration
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylIntent = (function () {
  'use strict';

  /* ─── Standard Actions ─── */
  var ACTION = {
    VIEW:    'zyl.intent.action.VIEW',
    SEND:    'zyl.intent.action.SEND',
    PICK:    'zyl.intent.action.PICK',
    EDIT:    'zyl.intent.action.EDIT',
    DIAL:    'zyl.intent.action.DIAL',
    CAPTURE: 'zyl.intent.action.CAPTURE',
    SEARCH:  'zyl.intent.action.SEARCH',
  };

  /* ─── Intent Filter Registry ─── */
  var _filters = []; /* { appId, action, mimeType, handler } */

  /**
   * Register an intent filter — declares what intents an app can handle.
   * Called by each app during initialization.
   */
  function registerFilter(appId, action, mimeType) {
    _filters.push({ appId: appId, action: action, mimeType: mimeType || '*/*' });
  }

  /**
   * Resolve an intent — find apps that can handle it.
   * @param {Object} intent - { action, data, mimeType, targetApp }
   * @returns {Array} matching app IDs
   */
  function resolve(intent) {
    if (!intent || !intent.action) return [];

    /* Explicit intent: specific target app */
    if (intent.targetApp) {
      return [intent.targetApp];
    }

    /* Implicit intent: match by action + mimeType */
    var matches = [];
    var mime = intent.mimeType || '*/*';
    for (var i = 0; i < _filters.length; i++) {
      var f = _filters[i];
      if (f.action !== intent.action) continue;
      if (f.mimeType === '*/*' || f.mimeType === mime ||
          (mime.indexOf('/') !== -1 && f.mimeType === mime.split('/')[0] + '/*')) {
        matches.push(f.appId);
      }
    }
    return matches;
  }

  /**
   * Start an intent — resolve + launch target app with data.
   * @param {Object} intent - { action, data, mimeType, targetApp, extras }
   * @returns {boolean} true if handled
   */
  function startIntent(intent) {
    if (!intent) return false;

    var targets = resolve(intent);
    if (targets.length === 0) return false;

    /* Single match: launch directly */
    var targetApp = targets[0];

    /* Deliver intent data via postMessage after app loads */
    var msg = {
      type: 'intent.deliver',
      data: {
        action: intent.action,
        data: intent.data || null,
        mimeType: intent.mimeType || null,
        extras: intent.extras || {},
        sourceApp: intent.sourceApp || ''
      }
    };

    /* Launch app + queue intent delivery */
    if (typeof ZylBridge !== 'undefined') {
      ZylBridge.sendToSystem({
        type: 'app.launch',
        appId: targetApp,
        intent: msg.data
      });
    }

    return true;
  }

  /**
   * Create a chooser for implicit intents with multiple handlers.
   * Returns array of { appId, appName } for UI display.
   */
  function createChooser(intent) {
    var targets = resolve(intent);
    var choices = [];
    for (var i = 0; i < targets.length; i++) {
      var app = (typeof ZylAppRegistry !== 'undefined')
        ? ZylAppRegistry.getApp(targets[i]) : null;
      choices.push({
        appId: targets[i],
        appName: app ? app.name : targets[i]
      });
    }
    return choices;
  }

  /* ─── Default filters for system apps ─── */
  registerFilter('com.zylos.browser',  ACTION.VIEW,    'text/html');
  registerFilter('com.zylos.browser',  ACTION.VIEW,    'text/uri');
  registerFilter('com.zylos.browser',  ACTION.SEARCH,  '*/*');
  registerFilter('com.zylos.gallery',  ACTION.VIEW,    'image/*');
  registerFilter('com.zylos.music',    ACTION.VIEW,    'audio/*');
  registerFilter('com.zylos.files',    ACTION.VIEW,    '*/*');
  registerFilter('com.zylos.files',    ACTION.PICK,    '*/*');
  registerFilter('com.zylos.phone',    ACTION.DIAL,    'tel/*');
  registerFilter('com.zylos.messages', ACTION.SEND,    'text/plain');
  registerFilter('com.zylos.camera',   ACTION.CAPTURE, 'image/*');
  registerFilter('com.zylos.notes',    ACTION.EDIT,    'text/plain');
  registerFilter('com.zylos.contacts', ACTION.VIEW,    'vcard/*');

  return {
    ACTION: ACTION,
    registerFilter: registerFilter,
    resolve: resolve,
    startIntent: startIntent,
    createChooser: createChooser
  };
})();
