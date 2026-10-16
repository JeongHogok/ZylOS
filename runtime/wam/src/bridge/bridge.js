/*
 * Zyl OS WAM: JS Bridge Script
 *
 * Injected into every web app at document start.  Gives the app its identity
 * and the two window actions the WAM answers.  Everything else — service
 * calls, intents, navigation — goes through apps/shared/bridge.js
 * (service.request), where bridge.c checks the caller's permissions.
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
 * 역할: 웹앱에 navigator.system(앱 정체·닫기·실행 요청)을 주입
 * 수행범위: 창 동작 메시지 송신만. 서비스 호출은 shared/bridge.js 의 service.request
 * 의존방향: WAM 데몬(네이티브), WebKit IPC
 * SOLID: SRP — JS↔네이티브 브릿지 역할만 담당
 */

(function () {
  "use strict";

  function postBridge(msg) {
    window.webkit.messageHandlers.bridge.postMessage(JSON.stringify(msg));
  }

  window.navigator.system = {
    app: {
      id:      "{{APP_ID}}",
      name:    "{{APP_NAME}}",
      version: "{{APP_VERSION}}",

      close: function () {
        postBridge({ type: "app.close", appId: "{{APP_ID}}" });
      }
    },

    launch: function (appId) {
      postBridge({ type: "app.launch", appId: appId });
    }
  };

})();
