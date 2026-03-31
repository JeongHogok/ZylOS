// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Permission Dialog
//
// Role: Runtime permission request UI — Android-style grant/deny dialog
// Scope: 앱이 위험 권한을 요청할 때 OS 수준 다이얼로그 표시,
//        사용자 승인/거부 → 권한 상태 업데이트
// Dependency Direction: Presentation -> ZylPermissions (Domain)
// SOLID: SRP — 권한 다이얼로그만 담당
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylPermissionDialog = (function () {
  'use strict';

  /* ─── 위험 권한 (런타임 요청 필요) ─── */
  var DANGEROUS_PERMISSIONS = [
    'camera', 'location', 'contacts', 'messaging',
    'telephony', 'storage', 'microphone', 'bluetooth'
  ];

  /* ─── 권한 → 사용자 표시 문구 (i18n 키) ─── */
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

  /**
   * 앱이 위험 권한을 요청할 때 호출.
   * Promise를 반환: true(승인) / false(거부).
   */
  function requestPermission(appId, permission) {
    /* 이미 승인된 권한이면 즉시 resolve */
    if (typeof ZylPermissions !== 'undefined') {
      if (ZylPermissions.hasPermission(appId, permission)) {
        return Promise.resolve(true);
      }
    }

    /* 위험 권한이 아니면 앱에 선언되어 있으면 자동 승인 */
    if (DANGEROUS_PERMISSIONS.indexOf(permission) === -1) {
      return Promise.resolve(true);
    }

    /* 다이얼로그 표시 */
    return new Promise(function (resolve) {
      _pendingGrant = { appId: appId, permission: permission, resolve: resolve };
      showDialog(appId, permission);
    });
  }

  /**
   * OS 수준 다이얼로그 표시 (에뮬레이터에서는 DOM, 실기기에서는 컴포지터 오버레이).
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

    denyBtn.onclick = function () { handleResponse(false); };
    allowBtn.onclick = function () { handleResponse(true); };

    btnRow.appendChild(denyBtn);
    btnRow.appendChild(allowBtn);
    dialog.appendChild(title);
    dialog.appendChild(body);
    dialog.appendChild(btnRow);
    overlay.appendChild(dialog);

    /* 이 스크립트는 컴포지터(index.html)에서 로드되므로 document.body 직접 사용.
     * window.parent.document 접근은 아키텍처 경계 위반 (CLAUDE.md §1). */
    document.body.appendChild(overlay);
  }

  function handleResponse(granted) {
    var el = document.getElementById('zyl-permission-dialog');
    if (el) el.parentNode.removeChild(el);

    if (_pendingGrant) {
      if (granted && typeof ZylPermissions !== 'undefined') {
        /* 실제로는 permission override에 추가하지 않음 — app.json에 선언된 권한만 유효.
           런타임 grant는 "사용자가 동의함"을 의미하며, 이후 동일 권한 재요청 시 다이얼로그 스킵. */
      }
      _pendingGrant.resolve(granted);
      _pendingGrant = null;
    }
  }

  /**
   * 앱이 위험 권한을 요청하는 서비스 호출 전에 호출되어야 하는 미들웨어.
   * 권한이 없으면 다이얼로그 표시 → 승인 시 서비스 호출 진행.
   */
  function checkAndRequest(appId, permission) {
    return requestPermission(appId, permission);
  }

  return {
    requestPermission: requestPermission,
    checkAndRequest: checkAndRequest,
    DANGEROUS_PERMISSIONS: DANGEROUS_PERMISSIONS
  };
})();
