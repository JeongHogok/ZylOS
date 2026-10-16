/*
 * Zyl OS WAM: JS Bridge Script
 *
 * Injected into the top document of every web app at document start.  Gives
 * the app its identity, the two window actions the WAM answers, and the one
 * way to reach the WAM: window.__zylPost, which apps/shared/bridge.js sends
 * through (service.request, intents, …; bridge.c checks the permissions).
 *
 * The WAM answers only a message that starts with this view's token
 * (bridge.c zyl_bridge_untoken). Only this script knows it, and it uses it
 * only in the app's own document: a page the app frames gets no copy of this
 * script, and a page the app navigates to is not zyl-app://<id>/.
 *
 * Tokens replaced at injection time, each with a JS string literal of its
 * value (bridge.c js_literal — a manifest's strings are the app's to write):
 *   {{APP_ID}}       - unique app identifier
 *   {{APP_NAME}}     - display name
 *   {{APP_VERSION}}  - version string
 *   {{BRIDGE_TOKEN}} - this web view's secret (zyl_bridge_new_token)
 *
 * Copyright (c) 2026 Zyl OS Project
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * [Clean Architecture] Platform Layer - Bridge
 * 역할: 웹앱에 navigator.system(앱 정체·닫기·실행 요청)과 WAM 송신 통로를 주입
 * 수행범위: 앱 자기 문서에서만 토큰을 붙여 송신. 서비스 호출은 shared/bridge.js 의 service.request
 * 의존방향: WAM 데몬(네이티브), WebKit IPC
 * SOLID: SRP — JS↔네이티브 브릿지 역할만 담당
 */

(function () {
  "use strict";

  var APP_ID = {{APP_ID}}, TOKEN = {{BRIDGE_TOKEN}};
  var own = location.href.indexOf("zyl-app://" + APP_ID + "/") === 0;
  var handler = window.webkit && window.webkit.messageHandlers &&
                window.webkit.messageHandlers.bridge;

  function post(json) {
    if (own && handler) handler.postMessage(TOKEN + json);
  }
  Object.defineProperty(window, "__zylPost", { value: post });

  function postBridge(msg) {
    post(JSON.stringify(msg));
  }

  window.navigator.system = {
    app: {
      id:      APP_ID,
      name:    {{APP_NAME}},
      version: {{APP_VERSION}},

      close: function () {
        postBridge({ type: "app.close", appId: APP_ID });
      }
    },

    launch: function (appId) {
      postBridge({ type: "app.launch", appId: appId });
    }
  };

})();
