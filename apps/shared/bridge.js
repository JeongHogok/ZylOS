// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Adapter
//
// 역할: 클라이언트 측 브릿지 API — navigator.system 래퍼
// 수행범위: Promise 기반 네이티브 API 호출, 브릿지 미사용 시 폴백 제공
// 의존방향: 없음 (네이티브 셸 API를 추상화)
// SOLID: DIP — 네이티브 API 구현이 아닌 추상 인터페이스에 의존
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

var ZylBridge = (function () {
  'use strict';

  var DEBUG = false;

  function log() {
    if (DEBUG) {
      console.log.apply(console, ['[ZylBridge]'].concat(Array.prototype.slice.call(arguments)));
    }
  }

  /* ═══════════════════════════════════════════════════════════
     IPC Mode Detection — runtime environment auto-detection.
     Determines whether we're running in:
     - 'iframe'  : Tauri emulator (app inside iframe, postMessage to parent)
     - 'webkit'  : Real device WAM (WebKitGTK messageHandlers → D-Bus)
     - 'standalone' : Plain browser (no parent, no bridge — fallback)
     The WAM also injects navigator.system, but that object has no transport
     of its own: checking it first sent every device message nowhere.
     ═══════════════════════════════════════════════════════════ */
  var IPC_MODE = (function () {
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.bridge) return 'webkit';
    if (window.parent && window.parent !== window) return 'iframe';
    return 'standalone';
  })();

  /**
   * Send a message to the system (compositor/WAM/emulator).
   * Automatically routes based on detected IPC mode.
   * @param {Object|string} msg - Message object or JSON string
   */
  function sendToSystem(msg) {
    /* I21: 모든 시스템 메시지에 version: 1 자동 삽입 */
    var payload = typeof msg === 'string' ? JSON.parse(msg) : msg;
    if (payload && typeof payload === 'object' && payload.version === undefined) {
      payload.version = 1;
    }
    var json = JSON.stringify(payload);
    switch (IPC_MODE) {
      case 'webkit':
        /* Real device WAM: WebKit messageHandlers → D-Bus. bridge.c answers
           only a message carrying a callbackId, by calling window._zylCb_<id>.
           The answer is re-posted as the service.response the emulator sends,
           so promise callers and response listeners work the same on both. */
        if (payload && payload.type === 'service.request') {
          var cbId = _nextCallbackId++;
          payload.callbackId = cbId;
          json = JSON.stringify(payload);
          window['_zylCb_' + cbId] = function (data) {
            delete window['_zylCb_' + cbId];
            window.postMessage(JSON.stringify({
              type: 'service.response', service: payload.service, method: payload.method,
              params: payload.params || {}, requestId: payload.requestId || null, data: data
            }), '*');
          };
        }
        try {
          window.webkit.messageHandlers.bridge.postMessage(json);
        } catch (e) { log('webkit IPC error:', e); }
        break;
      case 'iframe':
        /* Emulator: postMessage to parent compositor */
        window.parent.postMessage(json, '*');
        break;
      case 'standalone':
        /* Standalone: try direct ZylSystemServices if available */
        try {
          if (payload && payload.type === 'service.request' && typeof ZylSystemServices !== 'undefined') {
            var result = ZylSystemServices.handleRequest(payload.service, payload.method, payload.params || {});
            if (result && typeof result.then === 'function') {
              result.then(function (data) {
                window.postMessage(JSON.stringify({
                  type: 'service.response', service: payload.service,
                  method: payload.method, data: data
                }), '*');
              }).catch(function (err) {
                window.postMessage(JSON.stringify({
                  type: 'service.response', service: payload.service,
                  method: payload.method, data: null, error: String(err)
                }), '*');
              });
            }
          }
        } catch (e) { log('standalone IPC error:', e); }
        break;
    }
  }

  /* ─── Request-Response Matching ─── */
  var _nextRequestId = 1;
  /* Above the WAM-injected navigator.system's own _zylCb_ counter, which starts at 1 */
  var _nextCallbackId = 1000000;
  var _pendingRequests = {}; /* requestId → { resolve, reject, timer } */
  var REQUEST_TIMEOUT_MS = 15000;

  /**
   * Request a system service via IPC.
   * Returns a Promise that resolves with the service response data.
   * Automatically generates requestId for response matching.
   *
   * @param {string} service - Service name (e.g. 'fs', 'network', 'contacts')
   * @param {string} method - Method name (e.g. 'fetch', 'getAll')
   * @param {Object} [params] - Parameters
   * @returns {Promise} resolves with response data
   */
  function requestService(service, method, params) {
    var reqId = _nextRequestId++;
    var promise = new Promise(function (resolve, reject) {
      _pendingRequests[reqId] = {
        resolve: resolve,
        reject: reject,
        service: service,
        method: method,
        timer: setTimeout(function () {
          delete _pendingRequests[reqId];
          reject({ error: 'Request timeout', service: service, method: method, requestId: reqId });
        }, REQUEST_TIMEOUT_MS)
      };
    });

    sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {},
      requestId: reqId
    });

    return promise;
  }

  /**
   * Fire-and-forget service request (no response expected).
   * Backward compatibility for code that doesn't need responses.
   */
  function requestServiceFire(service, method, params) {
    sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  /**
   * Get current IPC mode for diagnostics.
   * @returns {string} 'webkit' | 'iframe' | 'standalone'
   */
  function getIpcMode() { return IPC_MODE; }

  /**
   * Check whether the native bridge is available.
   * @returns {boolean}
   */
  function isAvailable() {
    return !!(window.navigator && window.navigator.system);
  }

  /**
   * Check whether the WebKit message handler bridge is available.
   * @returns {boolean}
   */
  function isWebKitAvailable() {
    return !!(window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.bridge);
  }

  /* ─── App Lifecycle ─── */

  /**
   * Launch an app by its bundle identifier.
   * @param {string} appId - e.g. 'com.zylos.browser'
   * @returns {Promise<boolean>}
   */
  function launch(appId) {
    log('launch', appId);
    if (isAvailable() && typeof navigator.system.launch === 'function') {
      try {
        var result = navigator.system.launch(appId);
        return Promise.resolve(result !== false);
      } catch (e) {
        return Promise.reject(e);
      }
    }
    /* IPC 추상화: 런타임 환경에 따라 자동 라우팅 */
    sendToSystem({ type: 'app.launch', appId: appId });
    return Promise.resolve(true);
  }

  /**
   * Close the current app.
   * @returns {Promise<boolean>}
   */
  function closeApp() {
    log('closeApp');
    if (isAvailable() && navigator.system.app && typeof navigator.system.app.close === 'function') {
      try {
        navigator.system.app.close();
        return Promise.resolve(true);
      } catch (e) {
        return Promise.reject(e);
      }
    }
    sendToSystem({ type: 'app.close' });
    return Promise.resolve(true);
  }

  /* ─── System Settings ─── */

  /**
   * Set the system locale.
   * @param {string} locale - e.g. 'ko', 'en'
   * @returns {Promise<boolean>}
   */
  function setLocale(locale) {
    log('setLocale', locale);
    if (isWebKitAvailable()) {
      try {
        window.webkit.messageHandlers.bridge.postMessage(
          JSON.stringify({ type: 'system.setLocale', locale: locale })
        );
        return Promise.resolve(true);
      } catch (e) {
        return Promise.reject(e);
      }
    }
    log('setLocale (no bridge):', locale);
    return Promise.resolve(false);
  }

  /**
   * Send a generic message through the WebKit bridge.
   * @param {string} type - Message type identifier
   * @param {Object} [payload] - Additional data
   * @returns {Promise<boolean>}
   */
  function sendMessage(type, payload) {
    log('sendMessage', type, payload);
    if (isWebKitAvailable()) {
      try {
        var msg = { type: type };
        if (payload) {
          Object.keys(payload).forEach(function (k) {
            msg[k] = payload[k];
          });
        }
        window.webkit.messageHandlers.bridge.postMessage(JSON.stringify(msg));
        return Promise.resolve(true);
      } catch (e) {
        return Promise.reject(e);
      }
    }
    log('sendMessage (no bridge):', type, payload);
    return Promise.resolve(false);
  }

  /* ─── Hardware ─── */

  /**
   * Get battery information.
   * @deprecated Use power service via postMessage instead of direct navigator.getBattery.
   * @returns {Promise<{ level: number, charging: boolean }>}
   */
  function getBattery() {
    /* Delegate to OS power service via postMessage IPC.
       Direct navigator.getBattery() is a Clean Architecture violation —
       battery state must come from the power service. */
    requestService('power', 'getState', {});
    return Promise.resolve({ level: -1, charging: false });
  }

  /* ─── Notifications ─── */

  /**
   * Post a notification through the system notification channel.
   * @param {string} title
   * @param {string} body
   * @param {Object} [options] - { channel, icon, priority, appId, actions }
   * @returns {Promise<number>} notification ID
   */
  function notify(title, body, options) {
    var opts = options || {};
    var msg = {
      type: 'notification.create',
      title: title,
      body: body,
      channel: opts.channel || 'default',
      icon: opts.icon || '',
      priority: opts.priority || 1,
      appId: opts.appId || '',
      actions: opts.actions || [],
    };
    log('notify', title);
    sendToSystem(msg);
    return Promise.resolve(Date.now());
  }

  /**
   * Dismiss a notification by ID.
   * @param {number} id
   */
  function clearNotification(id) {
    sendToSystem({ type: 'notification.dismiss', id: id });
    return Promise.resolve(true);
  }

  /**
   * Clear all notifications.
   */
  function clearAllNotifications() {
    sendToSystem({ type: 'notification.clearAll' });
    return Promise.resolve(true);
  }

  /* ─── Incoming Message Handler (emulator → app iframe) ─── */

  /* ─── Instance state (lifecycle.save / lifecycle.restore) ───
     The compositor asks for UI state before it leaves an app and hands it back
     on the next launch — Android's onSaveInstanceState. Apps register a saver
     that returns a JSON-serialisable object (drafts, open view, scroll) and a
     restorer that applies it. Keep it to UI state: the compositor drops
     anything over 64 KB, and real data belongs in the fs or a service. */
  var _stateSaver = null;
  var _stateRestorer = null;
  var _pendingRestore;          /* undefined = nothing waiting */

  function onSaveState(fn) {
    _stateSaver = (typeof fn === 'function') ? fn : null;
  }

  function onRestoreState(fn) {
    _stateRestorer = (typeof fn === 'function') ? fn : null;
    /* The restore can arrive before the app finished initialising */
    if (_stateRestorer && _pendingRestore !== undefined) {
      var st = _pendingRestore;
      _pendingRestore = undefined;
      try { _stateRestorer(st); } catch (e) { log('restore failed:', e); }
    }
  }

  /* Set after the keyboard edits a field; the next touch on the app commits
     the composing syllable (moving the caret, tapping "send"). Gated so a
     plain tap with no typing in between sends nothing. */
  var _keyboardEditActive = false;

  function onAppTouch() {
    if (!_keyboardEditActive) return;
    _keyboardEditActive = false;
    sendToSystem({ type: 'input.commitComposition' });
  }

  if (typeof document !== 'undefined' && document.addEventListener) {
    document.addEventListener('mousedown', onAppTouch, true);
    document.addEventListener('touchstart', onAppTouch, true);
  }

  window.addEventListener('message', function (event) {
    var msg;
    if (typeof event.data === 'string') {
      try { msg = JSON.parse(event.data); } catch (e) { return; }
    } else if (typeof event.data === 'object') {
      msg = event.data;
    } else {
      return;
    }

    /* ─── Service response matching (requestId → pending Promise) ─── */
    if (msg.type === 'service.response' && msg.requestId) {
      var pending = _pendingRequests[msg.requestId];
      if (pending) {
        clearTimeout(pending.timer);
        delete _pendingRequests[msg.requestId];
        if (msg.data && msg.data.error) {
          pending.reject(msg.data);
        } else {
          pending.resolve(msg.data);
        }
      }
    }

    /* Virtual keyboard input relay: emulator forwards key events */
    if (msg.type === 'input.key' && msg.data) {
      var focused = document.activeElement;
      if (focused && (focused.tagName === 'INPUT' || focused.tagName === 'TEXTAREA')) {
        var key = msg.data.key;
        var start = focused.selectionStart;
        var end = focused.selectionEnd;

        if (key === 'Backspace') {
          if (start !== end) {
            focused.value = focused.value.slice(0, start) + focused.value.slice(end);
            focused.selectionStart = focused.selectionEnd = start;
          } else if (start > 0) {
            focused.value = focused.value.slice(0, start - 1) + focused.value.slice(start);
            focused.selectionStart = focused.selectionEnd = start - 1;
          }
        } else if (key === 'Enter') {
          if (focused.tagName === 'TEXTAREA') {
            focused.value = focused.value.slice(0, start) + '\n' + focused.value.slice(end);
            focused.selectionStart = focused.selectionEnd = start + 1;
          } else {
            focused.blur();
          }
        } else if (key.length === 1) {
          focused.value = focused.value.slice(0, start) + key + focused.value.slice(end);
          focused.selectionStart = focused.selectionEnd = start + 1;
        }

        focused.dispatchEvent(new Event('input', { bubbles: true }));
      }
    }

    /* Atomic text edit from the keyboard, applied in one step with one input
       event (input-method composition, candidate selection).
         expect  — text the keyboard believes is just before the caret
         text    — replaces expect
         fallbackDelete / fallback — used instead when expect is NOT there:
                   the field changed behind the keyboard (cleared after send,
                   rewritten by the app), so replacing would delete the wrong
                   characters or resurrect a syllable that was already gone.
       After a fallback we tell the keyboard so it can match its state. */
    if (msg.type === 'input.text' && msg.data) {
      var field = document.activeElement;
      if (field && (field.tagName === 'INPUT' || field.tagName === 'TEXTAREA')) {
        var d = msg.data;
        var expect = String(d.expect || '');
        var selStart = field.selectionStart;
        var selEnd = field.selectionEnd;
        /* email/number inputs expose no selection — treat as caret at the end */
        if (selStart === null || selStart === undefined) {
          selStart = selEnd = field.value.length;
        }
        var from, insert, usedFallback = false;
        if (selStart === selEnd && selStart >= expect.length &&
            field.value.slice(selStart - expect.length, selStart) === expect) {
          from = selStart - expect.length;
          insert = String(d.text || '');
        } else {
          /* A selection or a mismatch both mean the context moved on */
          var del = parseInt(d.fallbackDelete, 10);
          if (!(del > 0)) del = 0;
          from = (selStart !== selEnd) ? selStart : Math.max(0, selStart - del);
          insert = String(d.fallback || '');
          usedFallback = true;
        }
        field.value = field.value.slice(0, from) + insert + field.value.slice(selEnd);
        try {
          field.selectionStart = field.selectionEnd = from + insert.length;
        } catch (e) { /* input type without selection support — value is set */ }
        _keyboardEditActive = true;
        field.dispatchEvent(new Event('input', { bubbles: true }));
        if (usedFallback) sendToSystem({ type: 'input.compositionFallback' });
      }
    }

    /* Always answer a save request — an app with nothing to save replies null
       at once, so the compositor does not wait out its timeout */
    if (msg.type === 'lifecycle.save') {
      var saved = null;
      if (_stateSaver) {
        try { saved = _stateSaver(); } catch (e) { saved = null; log('save failed:', e); }
      }
      sendToSystem({ type: 'lifecycle.state', state: saved === undefined ? null : saved });
    }
    if (msg.type === 'lifecycle.restore' && msg.data) {
      if (_stateRestorer) {
        try { _stateRestorer(msg.data.state); } catch (e) { log('restore failed:', e); }
      } else {
        _pendingRestore = msg.data.state;
      }
    }

    /* Keyboard inset management: OS adjusts layout when keyboard shows/hides */
    if (msg.type === 'keyboard.show' && msg.data) {
      var kbHeight = parseInt(msg.data.height, 10) || 0;
      document.documentElement.style.setProperty('--keyboard-height', kbHeight + 'px');
      document.body.classList.add('keyboard-visible');
    }
    if (msg.type === 'keyboard.hide') {
      document.documentElement.style.setProperty('--keyboard-height', '0px');
      document.body.classList.remove('keyboard-visible');
    }
  });

  /* ─── Public API ─── */
  return {
    isAvailable: isAvailable,
    isWebKitAvailable: isWebKitAvailable,
    getIpcMode: getIpcMode,
    sendToSystem: sendToSystem,
    requestService: requestService,
    onSaveState: onSaveState,
    onRestoreState: onRestoreState,
    requestServiceFire: requestServiceFire,
    launch: launch,
    closeApp: closeApp,
    setLocale: setLocale,
    sendMessage: sendMessage,
    getBattery: getBattery,
    notify: notify,
    clearNotification: clearNotification,
    clearAllNotifications: clearAllNotifications,
  };
})();
