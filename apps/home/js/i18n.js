/*
 * DEPRECATED: This file has been replaced by the shared i18n module.
 * Please use: apps/shared/i18n.js (window.bpiI18n)
 *
 * This file is kept as a no-op redirect for backwards compatibility.
 * If loaded, it creates a thin shim that delegates to bpiI18n.
 */

var i18n = (function () {
  'use strict';

  if (window.bpiI18n) {
    return window.bpiI18n;
  }

  /* Fallback: if shared module was not loaded, warn and return stubs */
  console.warn('[i18n] Shared module not found. Load apps/shared/i18n.js before this file.');
  return {
    t: function (key) { return key; },
    formatDate: function () { return ''; },
    setLocale: function () {},
    getLocale: function () { return 'en'; },
    getSupportedLocales: function () { return []; },
  };
})();
