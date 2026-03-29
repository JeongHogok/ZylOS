// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Security Policy
//
// Role: OS app sandbox policy — defines iframe restrictions, permissions policy,
//       domain whitelist, and CSP for all apps
// Scope: Sandbox flags, Permissions Policy generation, network domain control
// Dependency Direction: Domain -> none (pure policy, no side effects)
// SOLID: SRP — sandbox policy definition only, OCP — extend via policy maps
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylSandbox = (function () {
  'use strict';

  /* iframe sandbox flags — applied to every app iframe by the compositor */
  var SANDBOX_FLAGS = 'allow-scripts allow-same-origin allow-popups allow-forms';

  /* Default Permissions Policy — all hardware access denied */
  var DEFAULT_POLICY = "camera 'none'; microphone 'none'; geolocation 'none'";

  /* Permission → Permissions Policy feature mapping */
  var FEATURE_MAP = {
    'camera':     'camera',
    'microphone': 'microphone',
    'location':   'geolocation'
  };

  /**
   * Generate Permissions Policy string for an app based on its effective permissions.
   * Granted permissions get 'self', denied get 'none'.
   */
  function getPolicy(appId, grantedPerms) {
    var policies = [];
    var features = Object.keys(FEATURE_MAP);
    for (var i = 0; i < features.length; i++) {
      var perm = features[i];
      var feature = FEATURE_MAP[perm];
      if (grantedPerms.indexOf(perm) !== -1) {
        policies.push(feature + " 'self'");
      } else {
        policies.push(feature + " 'none'");
      }
    }
    return policies.join('; ');
  }

  /* Network domain whitelist — only these domains can be fetched via network service */
  var ALLOWED_DOMAINS = [
    'api.open-meteo.com',
    'ipinfo.io'
  ];

  /**
   * Check if a URL is in the allowed domains list.
   */
  function isAllowedDomain(url) {
    try {
      var hostname = url.replace(/^https?:\/\//, '').split('/')[0].split(':')[0];
      for (var i = 0; i < ALLOWED_DOMAINS.length; i++) {
        if (hostname === ALLOWED_DOMAINS[i]) return true;
      }
    } catch (e) { /* invalid URL */ }
    return false;
  }

  return {
    SANDBOX_FLAGS: SANDBOX_FLAGS,
    DEFAULT_POLICY: DEFAULT_POLICY,
    getPolicy: getPolicy,
    isAllowedDomain: isAllowedDomain,
    ALLOWED_DOMAINS: ALLOWED_DOMAINS
  };
})();
