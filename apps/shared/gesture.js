// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Adapter
//
// 역할: 터치 제스처 감지기 — 스와이프 인터랙션 처리
// 수행범위: touchstart/move/end 이벤트 처리, 방향/임계값/축 제약 설정
// 의존방향: 없음 (독립 모듈)
// SOLID: OCP — 콜백 기반으로 제스처 액션 확장 가능
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// ──────────────────────────────────────────────────────────

var ZylGesture = (function () {
  'use strict';

  /**
   * Listen for swipe gestures on an element.
   *
   * @param {HTMLElement} element - The element to listen on
   * @param {Function} callback - Called with { direction, deltaX, deltaY, velocity }
   * @param {Object} [options]
   * @param {'up'|'down'|'left'|'right'|null} [options.direction=null]
   *        Only fire for this direction (null = any)
   * @param {number} [options.threshold=60] - Minimum distance in px to trigger
   * @param {'x'|'y'|'both'} [options.axis='both'] - Constrain detection axis
   * @returns {{ destroy: Function }}
   */
  function onSwipe(element, callback, options) {
    var opts = options || {};
    var threshold = opts.threshold || 60;
    var dirFilter = opts.direction || null;
    var axis = opts.axis || 'both';

    var startX = 0;
    var startY = 0;
    var startTime = 0;

    function handleTouchStart(e) {
      var touch = e.touches[0];
      startX = touch.clientX;
      startY = touch.clientY;
      startTime = Date.now();
    }

    function handleTouchEnd(e) {
      var touch = e.changedTouches[0];
      var dx = touch.clientX - startX;
      var dy = touch.clientY - startY;
      var elapsed = Date.now() - startTime;
      var absDx = Math.abs(dx);
      var absDy = Math.abs(dy);

      /* Determine dominant direction */
      var direction;
      if (axis === 'x' || (axis === 'both' && absDx >= absDy)) {
        if (absDx < threshold) return;
        direction = dx > 0 ? 'right' : 'left';
      } else if (axis === 'y' || (axis === 'both' && absDy > absDx)) {
        if (absDy < threshold) return;
        direction = dy > 0 ? 'down' : 'up';
      } else {
        return;
      }

      /* Apply direction filter */
      if (dirFilter && direction !== dirFilter) return;

      var distance = Math.sqrt(dx * dx + dy * dy);
      var velocity = elapsed > 0 ? distance / elapsed : 0;

      callback({
        direction: direction,
        deltaX: dx,
        deltaY: dy,
        velocity: velocity,
      });
    }

    element.addEventListener('touchstart', handleTouchStart, { passive: true });
    element.addEventListener('touchend', handleTouchEnd, { passive: true });

    return {
      /** Remove all gesture listeners */
      destroy: function () {
        element.removeEventListener('touchstart', handleTouchStart);
        element.removeEventListener('touchend', handleTouchEnd);
      },
    };
  }

  return {
    onSwipe: onSwipe,
  };
})();
