// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Compatibility Shim
//
// 역할: 레거시 i18n 호환 (shared/i18n.js로 위임)
// 수행범위: window.zylI18n이 없으면 스텁 반환
// 의존방향: shared/i18n.js (선행 로드 필요)
// SOLID: LSP — zylI18n과 동일 인터페이스
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

var i18n = (function () {
  'use strict';

  if (window.zylI18n) {
    return window.zylI18n;
  }

  return {
    t: function (key) { return key; },
    formatDate: function () { return ''; },
    setLocale: function () {},
    getLocale: function () { return 'en'; },
    getSupportedLocales: function () { return []; }
  };
})();
