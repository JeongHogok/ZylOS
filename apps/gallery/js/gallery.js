// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 갤러리 앱 — Pictures/ 디렉토리의 이미지/비디오 표시 및 재생
// 수행범위: 미디어 그리드 렌더링 (base64), 이미지 뷰어, 비디오 플레이어
// 의존방향: fs 서비스 (postMessage IPC)
// SOLID: SRP — 미디어 브라우징 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var grid = document.getElementById('photo-grid');
  var viewer = document.getElementById('viewer');
  var viewerImg = document.getElementById('viewer-img');
  var mediaFiles = [];
  var dataCache = {};

  var IMAGE_EXT = ['.jpg', '.jpeg', '.png', '.gif', '.webp', '.bmp'];
  var VIDEO_EXT = ['.mp4', '.mov', '.webm', '.mkv', '.avi'];

  function isImage(name) {
    var lower = (name || '').toLowerCase();
    return IMAGE_EXT.some(function (ext) { return lower.indexOf(ext) !== -1; });
  }

  function isVideo(name) {
    var lower = (name || '').toLowerCase();
    return VIDEO_EXT.some(function (ext) { return lower.indexOf(ext) !== -1; });
  }

  function isMedia(name) { return isImage(name) || isVideo(name); }

  function mimeType(name) {
    var lower = (name || '').toLowerCase();
    if (lower.indexOf('.png') !== -1) return 'image/png';
    if (lower.indexOf('.gif') !== -1) return 'image/gif';
    if (lower.indexOf('.webp') !== -1) return 'image/webp';
    if (lower.indexOf('.mp4') !== -1) return 'video/mp4';
    if (lower.indexOf('.mov') !== -1) return 'video/mp4';
    if (lower.indexOf('.webm') !== -1) return 'video/webm';
    return 'image/jpeg';
  }

  function requestService(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request', service: service, method: method, params: params || {}
    });
  }

  function requestMedia() {
    requestService('fs', 'getDirectory', { path: 'Pictures' });
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* ── Navigation back handling ── */
      if (msg.type === 'navigation.back') {
        if (viewer && !viewer.classList.contains('hidden')) {
          closeViewer();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        renderMediaList(msg.data);
      }

      if (msg.service === 'fs' && msg.method === 'readBinary' && msg.data && msg.params) {
        handleMediaData(msg.params, msg.data);
      }
    } catch (err) {}
  });

  function renderMediaList(entries) {
    if (!grid) return;
    grid.innerHTML = '';
    mediaFiles = (entries || []).filter(function (e) {
      return isMedia(e.name || e);
    });

    if (mediaFiles.length === 0) {
      grid.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px;grid-column:1/-1">No media in Pictures/</div>';
      return;
    }

    mediaFiles.forEach(function (file) {
      var name = file.name || file;
      var el = document.createElement('div');
      el.className = 'photo-thumb';
      el.dataset.name = name;
      el.dataset.type = isVideo(name) ? 'video' : 'image';

      /* 비디오 오버레이 아이콘 */
      if (isVideo(name)) {
        el.innerHTML = '<div class="video-badge">&#9654;</div><span class="thumb-label"></span>';
        el.querySelector('.thumb-label').textContent = name;
      } else {
        el.textContent = name;
        /* 이미지 썸네일 로드 */
        requestService('fs', 'readBinary', { path: 'Pictures/' + name });
      }

      el.style.cssText = 'display:flex;align-items:center;justify-content:center;font-size:10px;color:#666;overflow:hidden;position:relative';
      el.addEventListener('click', function () { openViewer(name); });
      grid.appendChild(el);
    });
  }

  function handleMediaData(params, base64) {
    if (!params || !params.path) return;
    var name = params.path.replace('Pictures/', '');
    var mime = mimeType(name);
    var dataUrl = 'data:' + mime + ';base64,' + base64;

    dataCache[name] = dataUrl;

    /* 그리드 썸네일 업데이트 (이미지만) */
    if (isImage(name)) {
      var thumb = grid ? grid.querySelector('[data-name="' + name + '"]') : null;
      if (thumb) {
        thumb.textContent = '';
        thumb.style.backgroundImage = 'url(' + dataUrl + ')';
        thumb.style.backgroundSize = 'cover';
        thumb.style.backgroundPosition = 'center';
      }
    }
  }

  function openViewer(name) {
    if (!viewer) return;

    /* 기존 비디오 요소 정리 */
    var oldVideo = viewer.querySelector('video');
    if (oldVideo) { oldVideo.pause(); oldVideo.remove(); }
    if (viewerImg) viewerImg.style.display = 'none';

    if (isVideo(name)) {
      /* 비디오: base64 로드 후 재생 */
      if (!dataCache[name]) {
        requestService('fs', 'readBinary', { path: 'Pictures/' + name });
        /* 로드 완료 후 재시도 — 간단한 폴링 */
        var pollCount = 0;
        var poll = setInterval(function () {
          pollCount++;
          if (dataCache[name] || pollCount > 50) {
            clearInterval(poll);
            if (dataCache[name]) showVideo(name);
          }
        }, 200);
      } else {
        showVideo(name);
      }
    } else {
      /* 이미지 */
      if (dataCache[name] && viewerImg) {
        viewerImg.src = dataCache[name];
        viewerImg.style.display = 'block';
      } else {
        /* 캐시 없으면 로드 요청 */
        requestService('fs', 'readBinary', { path: 'Pictures/' + name });
        if (viewerImg) {
          viewerImg.style.display = 'block';
          viewerImg.src = '';
        }
      }
    }

    currentViewingFile = name;
    viewer.classList.remove('hidden');
  }

  var activeVideo = null;

  function showVideo(name) {
    currentViewingFile = name;
    if (!viewer || !dataCache[name]) return;
    var video = document.createElement('video');
    video.src = dataCache[name];
    video.autoplay = true;
    video.playsInline = true;
    video.style.cssText = 'max-width:90%;max-height:75%;border-radius:8px';
    viewer.insertBefore(video, viewer.firstChild);
    activeVideo = video;

    var controls = document.getElementById('video-controls');
    var vcPlay = document.getElementById('vc-play');
    var vcSeek = document.getElementById('vc-seek');
    var vcFill = document.getElementById('vc-progress-fill');
    var vcTime = document.getElementById('vc-time');
    var vcSpeed = document.getElementById('vc-speed');
    var vcSkipBack = document.getElementById('vc-skip-back');
    var vcSkipFwd = document.getElementById('vc-skip-fwd');

    if (controls) controls.classList.remove('hidden');

    function formatTime(sec) {
      var m = Math.floor(sec / 60);
      var s = Math.floor(sec % 60);
      return m + ':' + (s < 10 ? '0' : '') + s;
    }

    video.addEventListener('timeupdate', function () {
      if (!video.duration) return;
      var pct = (video.currentTime / video.duration) * 100;
      if (vcFill) vcFill.style.width = pct + '%';
      if (vcSeek) vcSeek.value = pct;
      if (vcTime) vcTime.textContent = formatTime(video.currentTime) + ' / ' + formatTime(video.duration);
    });

    video.addEventListener('play', function () { if (vcPlay) vcPlay.textContent = '⏸'; });
    video.addEventListener('pause', function () { if (vcPlay) vcPlay.textContent = '▶'; });
    video.addEventListener('ended', function () { if (vcPlay) vcPlay.textContent = '▶'; });

    if (vcPlay) vcPlay.onclick = function () {
      if (video.paused) video.play(); else video.pause();
    };

    if (vcSeek) vcSeek.oninput = function () {
      if (video.duration) video.currentTime = (vcSeek.value / 100) * video.duration;
    };

    if (vcSkipBack) vcSkipBack.onclick = function () { video.currentTime = Math.max(0, video.currentTime - 10); };
    if (vcSkipFwd) vcSkipFwd.onclick = function () { video.currentTime = Math.min(video.duration || 0, video.currentTime + 10); };

    if (vcSpeed) {
      vcSpeed.value = '1';
      vcSpeed.onchange = function () { video.playbackRate = parseFloat(vcSpeed.value); };
    }
  }

  /* ─── Viewer Close ─── */
  if (document.getElementById('viewer-close')) {
    document.getElementById('viewer-close').addEventListener('click', closeViewer);
  }

  function closeViewer() {
    if (viewer) {
      viewer.classList.add('hidden');
      var vid = viewer.querySelector('video');
      if (vid) { vid.pause(); vid.remove(); }
      activeVideo = null;
    }
    if (viewerImg) { viewerImg.src = ''; viewerImg.style.display = 'none'; }
    var vc = document.getElementById('video-controls');
    if (vc) vc.classList.add('hidden');
    currentViewingFile = null;
  }

  /* ─── Viewer Delete ─── */
  var currentViewingFile = null;
  var viewerDeleteBtn = document.getElementById('viewer-delete');
  if (viewerDeleteBtn) {
    viewerDeleteBtn.addEventListener('click', function () {
      if (!currentViewingFile) return;
      if (confirm('Delete this file?')) {
        requestService('fs', 'remove', { path: 'Pictures/' + currentViewingFile });
        /* Remove from local list */
        mediaFiles = mediaFiles.filter(function (f) { return f.name !== currentViewingFile; });
        delete dataCache[currentViewingFile];
        closeViewer();
        renderMediaList(mediaFiles);
      }
    });
  }

  requestMedia();
})();
