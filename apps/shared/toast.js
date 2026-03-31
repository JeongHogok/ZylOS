// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - UI Utility
//
// 역할: 토스트 메시지 유틸리티 — 하단 알림 표시/자동 소멸
// 수행범위: 토스트 생성, 표시, 3초 후 자동 제거
// 의존방향: 없음 (독립 유틸리티)
// SOLID: SRP — 토스트 메시지 표시만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

var ZylToast = (function () {
  'use strict';

  var TOAST_DURATION = 3000;
  var container = null;

  function ensureContainer() {
    if (container) return;
    container = document.createElement('div');
    container.id = 'zyl-toast-container';
    container.style.cssText =
      'position:fixed;bottom:60px;left:50%;transform:translateX(-50%);' +
      'z-index:99999;display:flex;flex-direction:column;align-items:center;' +
      'gap:8px;pointer-events:none;';
    document.body.appendChild(container);
  }

  /**
   * Show a toast message at the bottom of the screen.
   * @param {string} message - Text to display (should be i18n translated)
   * @param {string} [type] - 'success' | 'error' | 'info' (default: 'info')
   */
  function show(message, type) {
    ensureContainer();
    var toast = document.createElement('div');
    var bgColor = 'rgba(0,0,0,0.85)';
    if (type === 'error') bgColor = 'rgba(220,38,38,0.9)';
    if (type === 'success') bgColor = 'rgba(22,163,74,0.9)';

    toast.style.cssText =
      'background:' + bgColor + ';color:#fff;padding:10px 20px;' +
      'border-radius:8px;font-size:13px;pointer-events:auto;' +
      'opacity:0;transition:opacity 0.3s ease;max-width:80vw;text-align:center;';
    toast.textContent = message;
    container.appendChild(toast);

    /* Fade in */
    setTimeout(function () { toast.style.opacity = '1'; }, 10);

    /* Fade out and remove */
    setTimeout(function () {
      toast.style.opacity = '0';
      setTimeout(function () {
        if (toast.parentNode) toast.parentNode.removeChild(toast);
      }, 300);
    }, TOAST_DURATION);
  }

  function success(message) { show(message, 'success'); }
  function error(message) { show(message, 'error'); }
  function info(message) { show(message, 'info'); }

  return {
    show: show,
    success: success,
    error: error,
    info: info
  };
})();
