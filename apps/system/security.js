// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Policy
//
// Role: Zyl OS security guard — protects system-critical file paths
// Scope: Protected path validation for filesystem operations
// Dependency Direction: Domain -> none (pure policy logic)
// SOLID: SRP — path protection only, OCP — new paths via array extension
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylSecurity = (function () {
  'use strict';

  var PROTECTED_PATHS = [
    'settings.json',
    '.credentials',
    '.system'
  ];

  function isProtectedPath(path) {
    var normalized = (path || '').replace(/^\/+/, '');
    for (var i = 0; i < PROTECTED_PATHS.length; i++) {
      if (normalized === PROTECTED_PATHS[i] || normalized.indexOf(PROTECTED_PATHS[i] + '/') === 0) {
        return true;
      }
    }
    return false;
  }

  /**
   * Check if a filename should be hidden from directory listings.
   * Used by fs.getDirectory to filter protected system files from user view.
   */
  function isHiddenFromListing(filename) {
    var name = (filename || '').replace(/^\/+/, '');
    for (var i = 0; i < PROTECTED_PATHS.length; i++) {
      if (name === PROTECTED_PATHS[i] || name.indexOf(PROTECTED_PATHS[i]) === 0) {
        return true;
      }
    }
    return false;
  }

  return {
    isProtectedPath: isProtectedPath,
    isHiddenFromListing: isHiddenFromListing
  };
})();
