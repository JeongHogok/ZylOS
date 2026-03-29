// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Widget
//
// 역할: 재사용 가능한 시계 위젯 — 시간 및 로케일 기반 날짜 표시
// 수행범위: HH:MM 시간 표시, 포맷된 날짜 표시, 1초 간격 자동 업데이트
// 의존방향: zylI18n (i18n.js)
// SOLID: SRP — 시계 표시 로직만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// ──────────────────────────────────────────────────────────

var ZylClock = (function () {
  'use strict';

  /**
   * Create a clock that updates every second.
   *
   * @param {HTMLElement} timeEl  - Element to display HH:MM
   * @param {HTMLElement|null} dateEl - Element to display formatted date (optional)
   * @param {Object} [options]
   * @param {boolean} [options.showDate=true] - Whether to update the date element
   * @param {'long'|'short'} [options.dateFormat='long'] - 'long' uses i18n full format,
   *        'short' uses a compact numeric format (YYYY-MM-DD)
   * @returns {{ destroy: Function }}
   */
  function create(timeEl, dateEl, options) {
    var opts = options || {};
    var showDate = opts.showDate !== false;
    var dateFormat = opts.dateFormat || 'long';
    var timer = null;

    function pad(n) {
      return n < 10 ? '0' + n : String(n);
    }

    function update() {
      var now = new Date();
      var h = pad(now.getHours());
      var m = pad(now.getMinutes());

      if (timeEl) {
        timeEl.textContent = h + ':' + m;
      }

      if (showDate && dateEl) {
        if (dateFormat === 'short') {
          dateEl.textContent =
            now.getFullYear() + '-' + pad(now.getMonth() + 1) + '-' + pad(now.getDate());
        } else {
          /* 'long' - use zylI18n if available, otherwise fallback */
          if (window.zylI18n && typeof window.zylI18n.formatDate === 'function') {
            dateEl.textContent = window.zylI18n.formatDate(now);
          } else {
            dateEl.textContent = now.toLocaleDateString();
          }
        }
      }
    }

    /* Initial update + start interval */
    update();
    timer = setInterval(update, 1000);

    /* Listen for locale changes to refresh the date immediately */
    var removeListener = null;
    if (window.zylI18n && typeof window.zylI18n.onLocaleChange === 'function') {
      removeListener = window.zylI18n.onLocaleChange(function () {
        update();
      });
    }

    return {
      /** Stop the clock and clean up resources */
      destroy: function () {
        if (timer) {
          clearInterval(timer);
          timer = null;
        }
        if (removeListener) {
          removeListener();
          removeListener = null;
        }
      },
      /** Force an immediate update */
      update: update,
    };
  }

  return {
    create: create,
  };
})();
