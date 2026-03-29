// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 갤러리 앱 — Pictures/ 디렉토리의 이미지 표시
// 수행범위: 사진 그리드 렌더링 (base64), 사진 뷰어
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
  var photoFiles = [];

  function requestService(service, method, params) {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: service, method: method, params: params || {}
    }), '*');
  }

  function requestPhotos() {
    requestService('fs', 'getDirectory', { path: 'Pictures' });
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;

      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        renderPhotoList(msg.data);
      }

      if (msg.service === 'fs' && msg.method === 'readBinary' && msg.data) {
        handleImageData(msg.params, msg.data);
      }
    } catch (err) {}
  });

  function renderPhotoList(entries) {
    if (!grid) return;
    grid.innerHTML = '';
    photoFiles = (entries || []).filter(function (e) {
      var name = (e.name || e).toLowerCase();
      return name.indexOf('.jpg') !== -1 || name.indexOf('.jpeg') !== -1 || name.indexOf('.png') !== -1;
    });

    if (photoFiles.length === 0) {
      grid.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px;grid-column:1/-1">No photos in Pictures/</div>';
      return;
    }

    photoFiles.forEach(function (photo) {
      var name = photo.name || photo;
      var el = document.createElement('div');
      el.className = 'photo-thumb';
      el.dataset.name = name;
      el.textContent = name;
      el.style.cssText = 'display:flex;align-items:center;justify-content:center;font-size:10px;color:#666;overflow:hidden';

      /* 썸네일 로드 요청 */
      requestService('fs', 'readBinary', { path: 'Pictures/' + name });

      el.addEventListener('click', function () {
        openViewer(name);
      });
      grid.appendChild(el);
    });
  }

  function handleImageData(params, base64) {
    if (!params || !params.path) return;
    var name = params.path.replace('Pictures/', '');
    var ext = name.toLowerCase().indexOf('.png') !== -1 ? 'png' : 'jpeg';
    var dataUrl = 'data:image/' + ext + ';base64,' + base64;

    /* 그리드 썸네일 업데이트 */
    var thumb = grid ? grid.querySelector('[data-name="' + name + '"]') : null;
    if (thumb) {
      thumb.textContent = '';
      thumb.style.backgroundImage = 'url(' + dataUrl + ')';
      thumb.style.backgroundSize = 'cover';
      thumb.style.backgroundPosition = 'center';
    }

    /* 뷰어용 데이터 캐시 */
    if (!handleImageData._cache) handleImageData._cache = {};
    handleImageData._cache[name] = dataUrl;
  }

  function openViewer(name) {
    if (!viewer || !viewerImg) return;
    var cache = handleImageData._cache || {};
    if (cache[name]) {
      viewerImg.src = cache[name];
    }
    viewer.classList.remove('hidden');
  }

  if (document.getElementById('viewer-close')) {
    document.getElementById('viewer-close').addEventListener('click', function () {
      if (viewer) viewer.classList.add('hidden');
      if (viewerImg) viewerImg.src = '';
    });
  }

  requestPhotos();
})();
