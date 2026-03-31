// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Applications Domain Module
//
// 역할: 설정 앱 애플리케이션 도메인 — 앱 목록, 앱 상세, 권한 관리
// 수행범위: 설치된 앱 목록 표시, 앱 상세 페이지, 권한 토글
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — 애플리케이션 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  var installedApps = [];
  var appPermissions = {};

  var PERM_LABELS = {
    camera:       'settings.perm_camera',
    microphone:   'settings.perm_microphone',
    location:     'settings.perm_location',
    storage:      'settings.perm_storage',
    contacts:     'settings.perm_contacts',
    notifications:'settings.perm_notifications',
    phone:        'settings.perm_phone',
    bluetooth:    'settings.perm_bluetooth',
    network:      'settings.perm_network',
    sensors:      'settings.perm_sensors'
  };

  /* ─── Render App List ─── */
  function renderAppList(apps) {
    var list = document.getElementById('app-list');
    if (!list) return;
    list.innerHTML = '';
    if (!apps || apps.length === 0) {
      list.innerHTML = '<div class="setting-item no-tap"><span class="setting-label" style="opacity:0.5">' + (core.t('settings.no_apps') || 'No apps') + '</span></div>';
      return;
    }
    apps.forEach(function (app) {
      var el = document.createElement('div');
      el.className = 'setting-item';
      el.innerHTML =
        '<div class="setting-text">' +
          '<span class="setting-label">' + (app.name || app.id) + '</span>' +
          '<span class="setting-value">' + (app.version || '') + '</span>' +
        '</div>' +
        '<svg class="chevron" viewBox="0 0 24 24"><path d="M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z"/></svg>';
      el.setAttribute('tabindex', '0');
      el.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); el.click(); }
      });
      el.addEventListener('click', function () {
        openAppDetail(app);
      });
      list.appendChild(el);
    });
  }

  /* ─── Open App Detail ─── */
  function openAppDetail(app) {
    var headerTitle = core.getHeaderTitle();

    /* Push current page onto navStack */
    var cur = core.getCurrentPage();
    if (cur) core.navStack.push(cur);

    var curPageEl = document.getElementById('page-' + cur);
    if (curPageEl) curPageEl.classList.add('hidden');

    core.setCurrentPage('app-detail');
    var detailPage = document.getElementById('page-app-detail');
    if (detailPage) detailPage.classList.remove('hidden');

    headerTitle.textContent = app.name || app.id;

    /* Render header info */
    var header = document.getElementById('app-detail-header');
    if (header) {
      header.innerHTML =
        '<div class="setting-item no-tap">' +
          '<span class="setting-label">' + (core.t('settings.app_version') || 'Version') + '</span>' +
          '<span class="setting-value">' + (app.version || '1.0.0') + '</span>' +
        '</div>' +
        '<div class="setting-item no-tap">' +
          '<span class="setting-label">' + (core.t('settings.app_package') || 'Package') + '</span>' +
          '<span class="setting-value" style="font-size:12px;opacity:0.7">' + (app.id || '') + '</span>' +
        '</div>';
    }

    /* Render permissions */
    var permList = document.getElementById('app-detail-permissions');
    if (!permList) return;
    var perms = app.permissions || [];
    if (perms.length === 0) {
      permList.innerHTML =
        '<div class="setting-item no-tap">' +
          '<span class="setting-label" style="opacity:0.5">' + (core.t('settings.no_permissions') || 'No permissions') + '</span>' +
        '</div>';
      return;
    }

    /* Check if this is a system app -- system apps have locked permissions */
    /* System app check -- use OS policy from app.json type field, not hardcoded list */
    var isSystem = app.system || (app.type === 'system');

    if (isSystem) {
      permList.innerHTML =
        '<div class="setting-item no-tap">' +
          '<span class="setting-label" style="font-weight:600">' + (core.t('settings.permissions') || 'Permissions') + '</span>' +
        '</div>' +
        '<div class="setting-item no-tap">' +
          '<span class="setting-label" style="opacity:0.5;font-size:13px">' + (core.t('settings.system_app_notice') || 'System app \u2014 all permissions granted') + '</span>' +
        '</div>';
      /* Show all permissions as locked ON */
      perms.forEach(function (perm) {
        var row = document.createElement('div');
        row.className = 'setting-item no-tap';
        var labelKey = PERM_LABELS[perm] || '';
        var label = labelKey ? core.t(labelKey) : perm;
        row.innerHTML =
          '<span class="setting-label">' + label + '</span>' +
          '<label class="toggle">' +
            '<input type="checkbox" checked disabled>' +
            '<span class="toggle-slider"></span>' +
          '</label>';
        permList.appendChild(row);
      });
      return;
    }

    permList.innerHTML = '<div class="setting-item no-tap"><span class="setting-label" style="font-weight:600">' + (core.t('settings.permissions') || 'Permissions') + '</span></div>';
    var appPerms = (appPermissions[app.id]) || {};

    perms.forEach(function (perm) {
      var granted = appPerms[perm] !== false;
      var row = document.createElement('div');
      row.className = 'setting-item no-tap';
      var labelKey = PERM_LABELS[perm] || '';
      var label = labelKey ? core.t(labelKey) : perm;
      row.innerHTML =
        '<span class="setting-label">' + label + '</span>' +
        '<label class="toggle">' +
          '<input type="checkbox" class="app-perm-toggle" data-app="' + app.id + '" data-perm="' + perm + '"' + (granted ? ' checked' : '') + '>' +
          '<span class="toggle-slider"></span>' +
        '</label>';
      row.querySelector('.app-perm-toggle').addEventListener('change', function () {
        var checked = this.checked;
        if (!appPermissions[app.id]) appPermissions[app.id] = {};
        appPermissions[app.id][perm] = checked;
        var revoked = [];
        var declared = app.permissions || [];
        declared.forEach(function (p) {
          if (appPermissions[app.id] && appPermissions[app.id][p] === false) {
            revoked.push(p);
          }
        });
        core.updateSetting('app_permissions', app.id, revoked.join(','));
      });
      permList.appendChild(row);
    });
  }

  /* ─── Register with core ─── */
  core.handlers.apps = {
    onServiceResponse: function (method, data) {
      if (method === 'getInstalled' && data) {
        installedApps = data;
        renderAppList(data);
      }
    }
  };

  core.handlers.app_permissions = {
    onSettingsGet: function (data) {
      /* data = { 'com.zylos.camera': 'microphone,gallery', ... } */
      Object.keys(data).forEach(function (appId) {
        var revokedList = String(data[appId]).split(',').filter(Boolean);
        appPermissions[appId] = {};
        revokedList.forEach(function (p) { appPermissions[appId][p] = false; });
      });
    },
    onSettingsUpdated: null
  };

})();
