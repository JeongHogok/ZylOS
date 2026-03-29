// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Adapter
//
// 역할: 클라이언트 측 브릿지 API — navigator.system 래퍼
// 수행범위: Promise 기반 네이티브 API 호출, 브릿지 미사용 시 폴백 제공
// 의존방향: 없음 (네이티브 셸 API를 추상화)
// SOLID: DIP — 네이티브 API 구현이 아닌 추상 인터페이스에 의존
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// ──────────────────────────────────────────────────────────

var ZylBridge = (function () {
  'use strict';

  var DEBUG = false;

  function log() {
    if (DEBUG) {
      console.log.apply(console, ['[ZylBridge]'].concat(Array.prototype.slice.call(arguments)));
    }
  }

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
    /* 에뮬레이터 환경: 부모 프레임에 postMessage */
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({ type: 'app.launch', appId: appId }), '*');
      return Promise.resolve(true);
    }
    log('launch (no bridge):', appId);
    return Promise.resolve(false);
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
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({ type: 'app.close' }), '*');
      return Promise.resolve(true);
    }
    log('closeApp (no bridge)');
    return Promise.resolve(false);
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
   * Get battery information if available.
   * @returns {Promise<{ level: number, charging: boolean }>}
   */
  function getBattery() {
    if (navigator.getBattery) {
      return navigator.getBattery().then(function (battery) {
        return {
          level: Math.round(battery.level * 100),
          charging: battery.charging,
        };
      });
    }
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
    if (isWebKitAvailable()) {
      window.webkit.messageHandlers.bridge.postMessage(JSON.stringify(msg));
      return Promise.resolve(Date.now());
    }
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify(msg), '*');
      return Promise.resolve(Date.now());
    }
    return Promise.resolve(0);
  }

  /**
   * Dismiss a notification by ID.
   * @param {number} id
   */
  function clearNotification(id) {
    var msg = { type: 'notification.dismiss', id: id };
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify(msg), '*');
    }
    return Promise.resolve(true);
  }

  /**
   * Clear all notifications.
   */
  function clearAllNotifications() {
    var msg = { type: 'notification.clearAll' };
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify(msg), '*');
    }
    return Promise.resolve(true);
  }

  /* ─── Public API ─── */
  return {
    isAvailable: isAvailable,
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
