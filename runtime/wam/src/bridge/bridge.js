/*
 * Zyl OS WAM: JS Bridge Script
 *
 * Injected into every web app at document start.  Provides the
 * navigator.system API that apps use to communicate with the
 * native WAM daemon.
 *
 * Tokens replaced at injection time:
 *   {{APP_ID}}      - unique app identifier
 *   {{APP_NAME}}    - display name
 *   {{APP_VERSION}} - version string
 *
 * Copyright (c) 2026 Zyl OS Project
 * SPDX-License-Identifier: MIT
 *
 * [Clean Architecture] Platform Layer - Bridge
 * 역할: 웹앱에 navigator.system API를 주입하여 네이티브 WAM과 통신
 * 수행범위: IPC 메시지 송수신, 앱 메타데이터 노출. 비즈니스 로직 금지
 * 의존방향: WAM 데몬(네이티브), WebKit IPC
 * SOLID: SRP — JS↔네이티브 브릿지 역할만 담당
 */

(function () {
  "use strict";

  var _cbId = 0;

  /**
   * Fire-and-forget: no response expected.
   */
  function postBridge(msg) {
    window.webkit.messageHandlers.bridge.postMessage(JSON.stringify(msg));
  }

  /**
   * Async bridge call.  Returns a Promise that resolves when the native side
   * calls back via window._zylCb_N().
   * callbackId is used by bridge.c to route the response.
   */
  function asyncBridge(msg) {
    return new Promise(function (resolve, reject) {
      _cbId += 1;
      var id = _cbId;
      msg.callbackId = id;
      window["_zylCb_" + id] = function (data) {
        delete window["_zylCb_" + id];
        if (data && data.error) {
          reject(new Error(data.message || "bridge error"));
        } else {
          resolve(data);
        }
      };
      try {
        postBridge(msg);
      } catch (err) {
        delete window["_zylCb_" + id];
        reject(err);
      }
    });
  }

  /* ────────────────────────────────────────────────────────
   * App lifecycle events (dispatched by WAM runtime)
   *
   * zyl:pause      — App is being suspended.
   * zyl:resume     — App is being brought back.
   * zyl:destroy    — App is about to be closed.
   * zyl:lowmemory  — System is memory-constrained; save state now.
   * ──────────────────────────────────────────────────────── */
  document.addEventListener("zyl:pause",     function () { console.log("[ZylOS] App paused"); });
  document.addEventListener("zyl:resume",    function () { console.log("[ZylOS] App resumed"); });
  document.addEventListener("zyl:destroy",   function () { console.log("[ZylOS] App destroying"); });
  document.addEventListener("zyl:lowmemory", function (e) {
    console.warn("[ZylOS] Low memory: " + (e.detail && e.detail.reason));
  });

  window.navigator.system = {

    /* ── App identity ───────────────────────────────────── */
    app: {
      id:      "{{APP_ID}}",
      name:    "{{APP_NAME}}",
      version: "{{APP_VERSION}}",

      close: function () {
        postBridge({ type: "app.close", appId: "{{APP_ID}}" });
      },

      minimize: function () {
        postBridge({ type: "app.minimize", appId: "{{APP_ID}}" });
      }
    },

    launch: function (appId) {
      postBridge({ type: "app.launch", appId: appId });
    },

    /* ── Notifications ─────────────────────────────────── */
    notification: {
      create: function (title, body, opts) {
        return asyncBridge({
          type:    "notification.create",
          title:   title,
          body:    body,
          options: opts || {}
        });
      }
    },

    /* ── Battery ─────────────────────────────────────────
     * Returns Promise<{level: number, charging: boolean}>
     */
    battery: {
      getLevel: function () {
        return asyncBridge({ type: "battery.getLevel" });
      }
    },

    /* ── Wi-Fi ────────────────────────────────────────────
     * Returns Promise<string[]>  (list of SSIDs)
     */
    wifi: {
      scan: function () {
        return asyncBridge({ type: "wifi.scan" });
      }
    },

    /* ── Settings ─────────────────────────────────────────
     * get(key?)   → Promise<value>
     * set(key, v) → Promise<{ok:true}>
     */
    settings: {
      get: function (key) {
        return asyncBridge({ type: "settings.get", key: key || null });
      },
      set: function (key, value) {
        return asyncBridge({ type: "settings.update", key: key, value: value });
      }
    },

    /* ── Camera ───────────────────────────────────────────
     * capture(opts?) → Promise<{path: string}>
     */
    camera: {
      capture: function (opts) {
        return asyncBridge({
          type: "service.request",
          service: "camera",
          method: "capture",
          params: opts || {}
        });
      },
      startPreview: function () {
        return asyncBridge({
          type: "service.request",
          service: "camera",
          method: "startPreview",
          params: {}
        });
      },
      stopPreview: function () {
        postBridge({
          type: "service.request",
          service: "camera",
          method: "stopPreview",
          params: {}
        });
      }
    },

    /* ── Audio ────────────────────────────────────────────
     * getVolume()        → Promise<{level: number}>
     * setVolume(level)   → Promise<{ok:true}>
     */
    audio: {
      getVolume: function () {
        return asyncBridge({
          type: "service.request",
          service: "audio",
          method: "getVolume",
          params: {}
        });
      },
      setVolume: function (level) {
        return asyncBridge({
          type: "service.request",
          service: "audio",
          method: "setVolume",
          params: { level: level }
        });
      }
    },

    /* ── Location ─────────────────────────────────────────
     * getLastKnown() → Promise<{latitude, longitude, accuracy}>
     */
    location: {
      getLastKnown: function () {
        return asyncBridge({
          type: "service.request",
          service: "location",
          method: "getLastKnown",
          params: {}
        });
      }
    },

    /* ── Generic service bridge ──────────────────────────
     * For services not covered by the typed APIs above.
     * request(service, method, params?) → Promise<any>
     */
    service: {
      request: function (serviceName, method, params) {
        return asyncBridge({
          type:    "service.request",
          service: serviceName,
          method:  method,
          params:  params || {}
        });
      }
    }
  };

})();
