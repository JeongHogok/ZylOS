// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Compositor
//
// 역할: 앱 인스턴스 상태 저장소 — 앱을 떠나기 전 상태를 묻고, 다음 실행 때 돌려준다
// 수행범위: 저장 요청·응답 대기(타임아웃), 크기 제한, 닫힌 앱 상태 폐기, 1회성 반환
// 의존방향: 없음 (전송·실행 여부·로그는 주입받는다)
// SOLID: SRP — 상태 보관만. 언제 묻고 언제 이동할지는 컴포지터(emulator.js)가 정한다
//
// 컴포지터의 역할(앱 생명주기)이며 OS 정책이 아니다: 앱이 무엇을 저장할지는
// 앱이 정하고(ZylBridge.onSaveState), 여기서는 운반만 한다.
// ──────────────────────────────────────────────────────────

var ZylAppStateStore = (function () {
  'use strict';

  /**
   * @param {Object} opts
   *   ask(appId)        send lifecycle.save to the app currently in the frame
   *   isRunning(appId)  false once the app was closed — its state is discarded
   *   log(msg)          optional
   *   timeoutMs         how long a slow or hung app may delay a switch
   *   maxBytes          UI state, not data
   */
  function create(opts) {
    var timeoutMs = opts.timeoutMs || 150;
    var maxBytes = opts.maxBytes || 64 * 1024;
    var saved = {};
    var waiter = null;

    /* Ask the app being left for its state, then call done() — on reply or on
       timeout, whichever comes first. Exactly once. */
    function saveThen(appId, done) {
      if (!appId) { done(); return; }
      var finished = false;
      function finish() {
        if (finished) return;
        finished = true;
        if (waiter === accept) waiter = null;
        done();
      }
      function accept(st) {
        if (st !== null && st !== undefined && opts.isRunning(appId)) {
          var size;
          try { size = JSON.stringify(st).length; } catch (e) { size = Infinity; }
          if (size <= maxBytes) saved[appId] = st;
          else if (opts.log) opts.log('[STATE] ' + appId + ' state too large (' + size + ' bytes), dropped');
        }
        finish();
      }
      waiter = accept;
      opts.ask(appId);
      setTimeout(finish, timeoutMs);
    }

    /* A lifecycle.state reply from the frame */
    function onState(st) {
      if (waiter) waiter(st);
    }

    /* One-shot: leaving the app again saves it again */
    function take(appId) {
      if (!Object.prototype.hasOwnProperty.call(saved, appId)) return { found: false };
      var st = saved[appId];
      delete saved[appId];
      return { found: true, state: st };
    }

    function discard(appId) {
      delete saved[appId];
    }

    return { saveThen: saveThen, onState: onState, take: take, discard: discard };
  }

  return { create: create };
})();

if (typeof window !== 'undefined') { window.ZylAppStateStore = ZylAppStateStore; }
if (typeof module !== 'undefined' && module.exports) { module.exports = ZylAppStateStore; }
