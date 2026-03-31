// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Permission Dialog
//
// Role: Runtime permission request UI — Android-style grant/deny dialog
//       with "Don't ask again" permanent denial support
// Scope: Dangerous permission dialog, permanent denial tracking,
//        user approval/denial → permission state update
// Dependency Direction: Presentation -> ZylPermissions (Domain)
// SOLID: SRP — permission dialog only
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylPermissionDialog = (function () {
  'use strict';

  /* ─── Dangerous permissions (runtime request needed) ─── */
  var DANGEROUS_PERMISSIONS = [
    'camera', 'location', 'contacts', 'messaging',
    'telephony', 'storage', 'microphone', 'bluetooth'
  ];

  /* ─── Permission → display label (i18n key) ─── */
  var PERMISSION_LABELS = {
    camera:      'permission.camera',
    location:    'permission.location',
    contacts:    'permission.contacts',
    messaging:   'permission.messaging',
    telephony:   'permission.telephony',
    storage:     'permission.storage',
    microphone:  'permission.microphone',
    bluetooth:   'permission.bluetooth'
  };

  var _pendingGrant = null; /* { appId, permission, resolve } */

  /* ─── Permanent denial tracking ─── */
  var _permanentDenials = {}; /* "appId:permission" → true */
  var _denialCounts = {};     /* "appId:permission" → number of denials */

  /**
   * Check if a permission has been permanently denied for an app.
   */
  function isPermanentlyDenied(appId, permission) {
    return !!_permanentDenials[appId + ':' + permission];
  }

  /**
   * Clear permanent denial for an app + permission.
   * Called from Settings when user manually re-enables a permission.
   */
  function clearPermanentDenial(appId, permission) {
    delete _permanentDenials[appId + ':' + permission];
    delete _denialCounts[appId + ':' + permission];
  }

  /**
   * Get all permanent denials for an app.
   * Returns array of permission strings.
   */
  function getPermanentDenials(appId) {
    var result = [];
    var keys = Object.keys(_permanentDenials);
    for (var i = 0; i < keys.length; i++) {
      if (keys[i].indexOf(appId + ':') === 0) {
        result.push(keys[i].substring(appId.length + 1));
      }
    }
    return result;
  }

  /**
   * Request a dangerous permission from the user.
   * Returns Promise<true> (granted), Promise<false> (denied),
   * or Promise<false> immediately if permanently denied.
   */
  function requestPermission(appId, permission) {
    /* Already granted → resolve immediately */
    if (typeof ZylPermissions !== 'undefined') {
      if (ZylPermissions.hasPermission(appId, permission)) {
        return Promise.resolve(true);
      }
    }

    /* Not a dangerous permission → auto-grant */
    if (DANGEROUS_PERMISSIONS.indexOf(permission) === -1) {
      return Promise.resolve(true);
    }

    /* Permanently denied → reject immediately */
    if (isPermanentlyDenied(appId, permission)) {
      return Promise.resolve(false);
    }

    /* Show dialog */
    return new Promise(function (resolve) {
      _pendingGrant = { appId: appId, permission: permission, resolve: resolve };
      showDialog(appId, permission);
    });
  }

  /**
   * OS-level permission dialog with "Don't ask again" checkbox.
   */
  function showDialog(appId, permission) {
    var appName = appId;
    if (typeof ZylAppRegistry !== 'undefined') {
      var app = ZylAppRegistry.getApp(appId);
      if (app) appName = app.name;
    }

    var labelKey = PERMISSION_LABELS[permission] || permission;
    var permLabel = (typeof zylI18n !== 'undefined')
      ? zylI18n.t(labelKey) || permission
      : permission;

    var key = appId + ':' + permission;
    var denied = _denialCounts[key] || 0;
    var showDontAsk = denied >= 1; /* Show "don't ask again" after first denial */

    /* Create dialog overlay */
    var overlay = document.createElement('div');
    overlay.id = 'zyl-permission-dialog';
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;' +
      'background:rgba(0,0,0,0.6);z-index:99999;display:flex;' +
      'align-items:center;justify-content:center;';

    var dialog = document.createElement('div');
    dialog.style.cssText = 'background:#1a1a2e;color:#e0e0e0;border-radius:16px;' +
      'padding:24px;max-width:320px;width:90%;text-align:center;';

    var title = document.createElement('div');
    title.style.cssText = 'font-size:16px;font-weight:600;margin-bottom:12px;';
    title.textContent = appName;

    var body = document.createElement('div');
    body.style.cssText = 'font-size:14px;margin-bottom:20px;opacity:0.8;';
    body.textContent = (typeof zylI18n !== 'undefined')
      ? zylI18n.t('permission.request_message', { app: appName, perm: permLabel })
        || appName + ' wants to access ' + permLabel
      : appName + ' wants to access ' + permLabel;

    /* "Don't ask again" checkbox — shown after first denial */
    var checkRow = null;
    var checkbox = null;
    if (showDontAsk) {
      checkRow = document.createElement('div');
      checkRow.style.cssText = 'display:flex;align-items:center;justify-content:center;' +
        'gap:8px;margin-bottom:16px;';

      checkbox = document.createElement('input');
      checkbox.type = 'checkbox';
      checkbox.id = 'zyl-perm-dontask';
      checkbox.style.cssText = 'width:16px;height:16px;accent-color:#4a90d9;';

      var checkLabel = document.createElement('label');
      checkLabel.htmlFor = 'zyl-perm-dontask';
      checkLabel.style.cssText = 'font-size:12px;opacity:0.7;cursor:pointer;';
      checkLabel.textContent = (typeof zylI18n !== 'undefined')
        ? zylI18n.t('permission.dont_ask_again') || "Don't ask again"
        : "Don't ask again";

      checkRow.appendChild(checkbox);
      checkRow.appendChild(checkLabel);
    }

    var btnRow = document.createElement('div');
    btnRow.style.cssText = 'display:flex;gap:12px;justify-content:center;';

    var denyBtn = document.createElement('button');
    denyBtn.style.cssText = 'flex:1;padding:10px;border:none;border-radius:8px;' +
      'background:#333;color:#e0e0e0;font-size:14px;cursor:pointer;';
    denyBtn.textContent = (typeof zylI18n !== 'undefined')
      ? zylI18n.t('permission.deny') || 'Deny' : 'Deny';
    denyBtn.setAttribute('aria-label', 'Deny permission');

    var allowBtn = document.createElement('button');
    allowBtn.style.cssText = 'flex:1;padding:10px;border:none;border-radius:8px;' +
      'background:#4a90d9;color:#fff;font-size:14px;cursor:pointer;';
    allowBtn.textContent = (typeof zylI18n !== 'undefined')
      ? zylI18n.t('permission.allow') || 'Allow' : 'Allow';
    allowBtn.setAttribute('aria-label', 'Allow permission');

    denyBtn.onclick = function () {
      var dontAsk = checkbox ? checkbox.checked : false;
      handleResponse(false, dontAsk);
    };
    allowBtn.onclick = function () { handleResponse(true, false); };

    btnRow.appendChild(denyBtn);
    btnRow.appendChild(allowBtn);
    dialog.appendChild(title);
    dialog.appendChild(body);
    if (checkRow) dialog.appendChild(checkRow);
    dialog.appendChild(btnRow);
    overlay.appendChild(dialog);

    document.body.appendChild(overlay);
  }

  function handleResponse(granted, dontAskAgain) {
    var el = document.getElementById('zyl-permission-dialog');
    if (el) el.parentNode.removeChild(el);

    if (_pendingGrant) {
      var key = _pendingGrant.appId + ':' + _pendingGrant.permission;

      if (granted) {
        /* Reset denial count on grant */
        _denialCounts[key] = 0;
        delete _permanentDenials[key];
      } else {
        /* Track denial count */
        _denialCounts[key] = (_denialCounts[key] || 0) + 1;

        /* Permanent denial if checkbox checked */
        if (dontAskAgain) {
          _permanentDenials[key] = true;
          /* Also update user overrides so the permission is revoked */
          if (typeof ZylPermissions !== 'undefined') {
            var existing = ZylPermissions.getEffectivePermissions(_pendingGrant.appId);
            var revoked = [];
            var declared = ZylPermissions.getDeclaredPermissions(_pendingGrant.appId);
            for (var i = 0; i < declared.length; i++) {
              if (existing.indexOf(declared[i]) === -1) revoked.push(declared[i]);
            }
            if (revoked.indexOf(_pendingGrant.permission) === -1) {
              revoked.push(_pendingGrant.permission);
            }
            ZylPermissions.setAppOverride(_pendingGrant.appId, revoked);
          }
        }
      }

      _pendingGrant.resolve(granted);
      _pendingGrant = null;
    }
  }

  /**
   * Middleware for service calls — check and request permission before proceeding.
   */
  function checkAndRequest(appId, permission) {
    return requestPermission(appId, permission);
  }

  return {
    requestPermission: requestPermission,
    checkAndRequest: checkAndRequest,
    isPermanentlyDenied: isPermanentlyDenied,
    clearPermanentDenial: clearPermanentDenial,
    getPermanentDenials: getPermanentDenials,
    DANGEROUS_PERMISSIONS: DANGEROUS_PERMISSIONS
  };
})();
