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

  function postBridge(msg) {
    window.webkit.messageHandlers.bridge.postMessage(JSON.stringify(msg));
  }

  function asyncBridge(msg) {
    return new Promise(function (resolve) {
      _cbId += 1;
      msg._cbId = _cbId;
      window["_zylCb_" + _cbId] = resolve;
      postBridge(msg);
    });
  }

  /* ────────────────────────────────────────────────────────
   * App lifecycle events (dispatched by WAM runtime)
   *
   * zyl:pause   — App is being suspended.  Save state,
   *               stop animations, release heavy resources.
   * zyl:resume  — App is being brought back.  Restore state,
   *               resume animations.
   * zyl:destroy — App is about to be closed.  Perform final
   *               cleanup (flush storage, close connections).
   * ──────────────────────────────────────────────────────── */
  document.addEventListener("zyl:pause", function () {
    console.log("[ZylOS] App paused");
  });

  document.addEventListener("zyl:resume", function () {
    console.log("[ZylOS] App resumed");
  });

  document.addEventListener("zyl:destroy", function () {
    console.log("[ZylOS] App destroying");
  });

  window.navigator.system = {
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

    notification: {
      create: function (title, body, opts) {
        postBridge({
          type:    "notification.create",
          title:   title,
          body:    body,
          options: opts || {}
        });
      }
    },

    battery: {
      getLevel: function () {
        return asyncBridge({ type: "battery.getLevel" });
      }
    },

    wifi: {
      scan: function () {
        return asyncBridge({ type: "wifi.scan" });
      }
    }
  };
})();
