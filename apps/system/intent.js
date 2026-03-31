// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Intent System
//
// Role: Android-style Intent for inter-app communication
// Scope: Explicit/implicit intents, broadcast intents, receivers,
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
    SEARCH:  'zyl.intent.action.SEARCH'
  };

  /* ─── Standard Broadcast Actions ─── */
  var BROADCAST = {
    BOOT_COMPLETED:       'zyl.intent.action.BOOT_COMPLETED',
    SCREEN_ON:            'zyl.intent.action.SCREEN_ON',
    SCREEN_OFF:           'zyl.intent.action.SCREEN_OFF',
    BATTERY_LOW:          'zyl.intent.action.BATTERY_LOW',
    BATTERY_OKAY:         'zyl.intent.action.BATTERY_OKAY',
    CONNECTIVITY_CHANGE:  'zyl.intent.action.CONNECTIVITY_CHANGE',
    LOCALE_CHANGED:       'zyl.intent.action.LOCALE_CHANGED',
    TIMEZONE_CHANGED:     'zyl.intent.action.TIMEZONE_CHANGED',
    PACKAGE_INSTALLED:    'zyl.intent.action.PACKAGE_INSTALLED',
    PACKAGE_REMOVED:      'zyl.intent.action.PACKAGE_REMOVED',
    USER_PRESENT:         'zyl.intent.action.USER_PRESENT',
    AIRPLANE_MODE_CHANGED:'zyl.intent.action.AIRPLANE_MODE_CHANGED',
    DND_MODE_CHANGED:     'zyl.intent.action.DND_MODE_CHANGED'
  };

  /* ─── Intent Filter Registry ─── */
  var _filters = []; /* { appId, action, mimeType } */

  /* ─── Broadcast Receiver Registry ─── */
  var _receivers = []; /* { appId, action, handler, priority } */

  /**
   * Register an intent filter — declares what intents an app can handle.
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

  /* ═══════════════════════════════════════════════
   *  NEW: Broadcast Intent System
   * ═══════════════════════════════════════════════ */

  /**
   * Register a broadcast receiver for a specific action.
   * @param {string} appId - Receiving app ID
   * @param {string} action - Broadcast action to listen for
   * @param {Function} handler - function(intent) called on broadcast
   * @param {number} [priority] - Higher priority receivers called first (default 0)
   * @returns {number} receiverId for unregistration
   */
  function registerReceiver(appId, action, handler, priority) {
    var id = _receivers.length;
    _receivers.push({
      id: id,
      appId: appId,
      action: action,
      handler: handler,
      priority: priority || 0,
      active: true
    });
    return id;
  }

  /**
   * Unregister a broadcast receiver.
   * @param {number} receiverId
   */
  function unregisterReceiver(receiverId) {
    if (_receivers[receiverId]) {
      _receivers[receiverId].active = false;
    }
  }

  /**
   * Send a broadcast intent to all registered receivers.
   * Receivers are called in priority order (highest first).
   * @param {Object} intent - { action, extras }
   * @param {boolean} [ordered] - If true, receivers are called sequentially
   *        and can abort further processing by returning false.
   */
  function sendBroadcast(intent) {
    if (!intent || !intent.action) return;

    /* Collect matching receivers, sorted by priority descending */
    var matched = [];
    for (var i = 0; i < _receivers.length; i++) {
      var r = _receivers[i];
      if (!r.active) continue;
      if (r.action === intent.action) {
        matched.push(r);
      }
    }

    matched.sort(function (a, b) { return b.priority - a.priority; });

    /* Dispatch to all receivers */
    for (var j = 0; j < matched.length; j++) {
      try {
        matched[j].handler({
          action: intent.action,
          extras: intent.extras || {},
          sourceApp: intent.sourceApp || 'system',
          timestamp: Date.now()
        });
      } catch (e) {
        /* Receiver error should not break broadcast chain */
      }
    }
  }

  /**
   * Send an ordered broadcast — receivers called sequentially.
   * A receiver can abort by returning { abort: true }.
   * @param {Object} intent - { action, extras }
   * @returns {Object} { aborted: boolean, handlerCount: number }
   */
  function sendOrderedBroadcast(intent) {
    if (!intent || !intent.action) return { aborted: false, handlerCount: 0 };

    var matched = [];
    for (var i = 0; i < _receivers.length; i++) {
      var r = _receivers[i];
      if (!r.active) continue;
      if (r.action === intent.action) {
        matched.push(r);
      }
    }

    matched.sort(function (a, b) { return b.priority - a.priority; });

    var aborted = false;
    for (var j = 0; j < matched.length; j++) {
      try {
        var result = matched[j].handler({
          action: intent.action,
          extras: intent.extras || {},
          sourceApp: intent.sourceApp || 'system',
          timestamp: Date.now()
        });
        if (result && result.abort) {
          aborted = true;
          break;
        }
      } catch (e) { /* ignore */ }
    }

    return { aborted: aborted, handlerCount: matched.length };
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
    BROADCAST: BROADCAST,
    registerFilter: registerFilter,
    resolve: resolve,
    startIntent: startIntent,
    createChooser: createChooser,
    /* Broadcast API */
    registerReceiver: registerReceiver,
    unregisterReceiver: unregisterReceiver,
    sendBroadcast: sendBroadcast,
    sendOrderedBroadcast: sendOrderedBroadcast
  };
})();
