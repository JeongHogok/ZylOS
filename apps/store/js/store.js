// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 앱스토어 — 설치 가능한 앱 목록 및 관리
// 수행범위: appstore 서비스 조회, 앱 설치/제거
// 의존방향: appstore 서비스 (postMessage IPC)
// SOLID: SRP — 앱스토어 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var appList = document.getElementById('app-list');

  function requestApps() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'appstore', method: 'getAvailable'
    }), '*');
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'apps', method: 'getInstalled'
    }), '*');
  }

  var installedApps = [];

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;
      if (msg.service === 'apps' && msg.method === 'getInstalled' && msg.data) {
        installedApps = msg.data || [];
        renderInstalled();
      }
    } catch (err) {}
  });

  function renderInstalled() {
    if (!appList) return;
    appList.innerHTML = '';
    if (installedApps.length === 0) {
      appList.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px">No apps available</div>';
      return;
    }
    installedApps.forEach(function (app) {
      var el = document.createElement('div');
      el.className = 'store-item';
      el.innerHTML = '<span class="store-name">' + escapeHtml(app.id || app.nameKey || '') + '</span>' +
        '<span class="store-status">Installed</span>';
      appList.appendChild(el);
    });
  }

  function escapeHtml(str) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(str || ''));
    return d.innerHTML;
  }

  requestApps();
})();
