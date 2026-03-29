// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 갤러리 앱 — Pictures/ 디렉토리의 이미지 표시
// 수행범위: 사진 그리드 렌더링, 사진 뷰어
// 의존방향: fs 서비스 (postMessage IPC)
// SOLID: SRP — 이미지 브라우징 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var grid = document.getElementById('photo-grid');
  var viewer = document.getElementById('viewer');
  var viewerImg = document.getElementById('viewer-img');

  function requestPhotos() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'fs', method: 'getDirectory', params: { path: 'Pictures' }
    }), '*');
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;
      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        renderPhotos(msg.data);
      }
    } catch (err) {}
  });

  function renderPhotos(entries) {
    if (!grid) return;
    grid.innerHTML = '';
    var photos = (entries || []).filter(function (e) {
      var ext = (e.name || '').toLowerCase();
      return ext.indexOf('.jpg') !== -1 || ext.indexOf('.jpeg') !== -1 || ext.indexOf('.png') !== -1;
    });
    if (photos.length === 0) {
      grid.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px;grid-column:1/-1">No photos</div>';
      return;
    }
    photos.forEach(function (photo) {
      var el = document.createElement('div');
      el.className = 'photo-thumb';
      el.textContent = photo.name;
      el.addEventListener('click', function () { openViewer(photo.name); });
      grid.appendChild(el);
    });
  }

  function openViewer(name) {
    if (viewer) viewer.classList.remove('hidden');
  }

  if (document.getElementById('viewer-close')) {
    document.getElementById('viewer-close').addEventListener('click', function () {
      if (viewer) viewer.classList.add('hidden');
    });
  }

  requestPhotos();
})();
