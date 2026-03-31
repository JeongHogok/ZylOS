// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 갤러리 앱 — Pictures/ 디렉토리의 이미지/비디오 표시 및 재생
// 수행범위: 미디어 그리드 렌더링 (base64), 이미지 뷰어, 비디오 플레이어,
//          앨범 분류, 슬라이드쇼, 회전
// 의존방향: fs 서비스 (postMessage IPC)
// SOLID: SRP — 미디어 브라우징 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var grid = document.getElementById('photo-grid');
  var albumGrid = document.getElementById('album-grid');
  var viewer = document.getElementById('viewer');
  var viewerImg = document.getElementById('viewer-img');
  var mediaFiles = [];
  var dataCache = {};

  /* ─── View mode: 'all' or 'albums' ─── */
  var currentView = 'all';
  var albumData = {}; // { folderName: [file entries] }
  var currentAlbumName = null; // when viewing inside an album

  /* ─── Rotation state ─── */
  var rotationDeg = 0;

  /* ─── Slideshow state ─── */
  var slideshowActive = false;
  var slideshowPaused = false;
  var slideshowTimer = null;
  var slideshowProgressTimer = null;
  var slideshowIndex = 0;
  var slideshowFiles = [];
  var SLIDESHOW_INTERVAL = 3000;

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

  function t(key, params) {
    if (typeof zylI18n !== 'undefined') {
      var str = zylI18n.t(key);
      if (params) {
        Object.keys(params).forEach(function (k) {
          str = str.replace('{' + k + '}', params[k]);
        });
      }
      return str;
    }
    return key;
  }

  /* ─── a11y: keyboard handler for button-like elements ─── */
  function addButtonKeyHandler(el) {
    el.setAttribute('tabindex', '0');
    el.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        el.click();
      }
    });
  }

  /* ─── Custom Confirm Modal (replaces native confirm) ─── */
  var galleryModalOverlay = null;

  function showGalleryConfirm(title, message, onConfirm) {
    if (!galleryModalOverlay) {
      galleryModalOverlay = document.createElement('div');
      galleryModalOverlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.5);z-index:9000;display:none;align-items:center;justify-content:center';
      document.body.appendChild(galleryModalOverlay);
    }
    var cancelText = t('common.cancel');
    var okText = t('common.ok');
    galleryModalOverlay.innerHTML =
      '<div role="dialog" aria-modal="true" aria-label="' + title + '" style="background:#1e1e2e;border-radius:16px;padding:24px;width:280px;max-width:90%;color:#fff">' +
        '<div style="font-size:16px;font-weight:600;margin-bottom:12px">' + title + '</div>' +
        '<div style="font-size:14px;opacity:0.8;margin-bottom:20px">' + message + '</div>' +
        '<div style="display:flex;gap:8px;justify-content:flex-end">' +
          '<button class="gm-cancel" style="padding:8px 16px;border:none;border-radius:8px;background:#333;color:#fff;cursor:pointer">' + cancelText + '</button>' +
          '<button class="gm-ok" style="padding:8px 16px;border:none;border-radius:8px;background:#ef4444;color:#fff;cursor:pointer">' + okText + '</button>' +
        '</div>' +
      '</div>';
    galleryModalOverlay.style.display = 'flex';
    var cancelBtn = galleryModalOverlay.querySelector('.gm-cancel');
    var okBtn = galleryModalOverlay.querySelector('.gm-ok');
    cancelBtn.addEventListener('click', function () { galleryModalOverlay.style.display = 'none'; });
    okBtn.addEventListener('click', function () { galleryModalOverlay.style.display = 'none'; onConfirm(); });
    okBtn.focus();
  }

  function requestService(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request', service: service, method: method, params: params || {}
    });
  }

  function requestMedia() {
    requestService('fs', 'getDirectory', { path: 'Pictures' });
  }

  function requestAlbumDirectories() {
    requestService('fs', 'getDirectory', { path: 'Pictures', subdirectories: true });
  }

  /* ─── Header Tabs ─── */
  var tabAll = document.getElementById('tab-all');
  var tabAlbums = document.getElementById('tab-albums');

  function updateTabLabels() {
    if (tabAll) tabAll.textContent = t('gallery.all');
    if (tabAlbums) tabAlbums.textContent = t('gallery.albums');
  }

  function switchView(view) {
    currentView = view;
    currentAlbumName = null;
    if (tabAll) tabAll.className = 'header-tab' + (view === 'all' ? ' active' : '');
    if (tabAlbums) tabAlbums.className = 'header-tab' + (view === 'albums' ? ' active' : '');

    if (view === 'all') {
      if (grid) grid.classList.remove('hidden');
      if (albumGrid) { albumGrid.classList.add('hidden'); albumGrid.innerHTML = ''; }
      renderMediaList(mediaFiles);
    } else {
      if (grid) grid.classList.add('hidden');
      if (albumGrid) albumGrid.classList.remove('hidden');
      requestAlbumDirectories();
    }
  }

  if (tabAll) {
    tabAll.addEventListener('click', function () { switchView('all'); });
  }
  if (tabAlbums) {
    tabAlbums.addEventListener('click', function () { switchView('albums'); });
  }

  /* ─── Message handler ─── */
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
        } else if (currentAlbumName) {
          /* Return from album contents to album list */
          currentAlbumName = null;
          switchView('albums');
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        if (msg.params && msg.params.subdirectories) {
          handleAlbumData(msg.data);
        } else if (msg.params && msg.params._albumPath) {
          handleAlbumContents(msg.params._albumName, msg.data);
        } else {
          renderMediaList(msg.data);
        }
      }

      if (msg.service === 'fs' && msg.method === 'readBinary' && msg.data && msg.params) {
        handleMediaData(msg.params, msg.data);
      }
    } catch (err) { if (typeof console !== 'undefined') console.error('[Gallery] message parse error:', err); }
  });

  /* ─── Album handling ─── */
  function handleAlbumData(entries) {
    albumData = {};
    var dirs = [];
    var rootFiles = [];

    (entries || []).forEach(function (e) {
      var name = e.name || e;
      if (e.type === 'directory' || e.isDirectory) {
        dirs.push(name);
      } else if (isMedia(name)) {
        rootFiles.push(e);
      }
    });

    if (rootFiles.length > 0) {
      albumData['Pictures'] = rootFiles;
    }

    dirs.forEach(function (dirName) {
      requestService('fs', 'getDirectory', {
        path: 'Pictures/' + dirName,
        _albumPath: true,
        _albumName: dirName
      });
    });

    /* If no subdirectories, just show root album */
    if (dirs.length === 0) {
      renderAlbumGrid();
    }
  }

  function handleAlbumContents(albumName, entries) {
    var files = (entries || []).filter(function (e) {
      return isMedia(e.name || e);
    });
    if (files.length > 0) {
      albumData[albumName] = files;
    }
    renderAlbumGrid();
  }

  function renderAlbumGrid() {
    if (!albumGrid || currentAlbumName) return;
    albumGrid.innerHTML = '';

    var albumNames = Object.keys(albumData);
    if (albumNames.length === 0) {
      albumGrid.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px;grid-column:1/-1">' + t('gallery.no_media') + '</div>';
      return;
    }

    albumNames.forEach(function (name) {
      var files = albumData[name];
      var count = files.length;

      var card = document.createElement('div');
      card.className = 'album-card';
      addButtonKeyHandler(card);

      var cover = document.createElement('div');
      cover.className = 'album-card-cover';
      cover.textContent = '\uD83D\uDCC1'; /* folder emoji as fallback */

      /* Try to use first image as cover */
      var firstImage = null;
      for (var i = 0; i < files.length; i++) {
        var fname = files[i].name || files[i];
        if (isImage(fname)) {
          firstImage = fname;
          break;
        }
      }
      if (firstImage) {
        var prefix = name === 'Pictures' ? 'Pictures/' : 'Pictures/' + name + '/';
        var cacheKey = prefix === 'Pictures/' ? firstImage : name + '/' + firstImage;
        if (dataCache[cacheKey] || dataCache[firstImage]) {
          cover.textContent = '';
          cover.style.backgroundImage = 'url(' + (dataCache[cacheKey] || dataCache[firstImage]) + ')';
        }
      }

      var info = document.createElement('div');
      info.className = 'album-card-info';

      var nameEl = document.createElement('div');
      nameEl.className = 'album-card-name';
      nameEl.textContent = name;

      var countEl = document.createElement('div');
      countEl.className = 'album-card-count';
      countEl.textContent = t('gallery.photos_count', { count: count });

      info.appendChild(nameEl);
      info.appendChild(countEl);
      card.appendChild(cover);
      card.appendChild(info);

      card.addEventListener('click', (function (albumName) {
        return function () { openAlbum(albumName); };
      })(name));

      albumGrid.appendChild(card);
    });
  }

  function openAlbum(albumName) {
    currentAlbumName = albumName;
    if (!albumGrid) return;
    albumGrid.innerHTML = '';

    /* Back bar */
    var backBar = document.createElement('div');
    backBar.id = 'album-back-bar';
    backBar.style.cssText = 'display:flex;align-items:center;gap:8px;padding:8px 12px;grid-column:1/-1';
    var backBtn = document.createElement('button');
    backBtn.textContent = '\u2190';
    backBtn.style.cssText = 'background:none;border:none;color:#6366f1;font-size:20px;cursor:pointer;padding:4px 8px';
    backBtn.addEventListener('click', function () {
      currentAlbumName = null;
      renderAlbumGrid();
    });
    var titleSpan = document.createElement('span');
    titleSpan.textContent = albumName;
    titleSpan.style.cssText = 'font-size:16px;font-weight:600;color:#e0e0e0';
    backBar.appendChild(backBtn);
    backBar.appendChild(titleSpan);
    albumGrid.appendChild(backBar);

    /* Render album contents as grid */
    var files = albumData[albumName] || [];
    albumGrid.style.gridTemplateColumns = 'repeat(3,1fr)';
    albumGrid.style.gap = '2px';
    albumGrid.style.padding = '2px';

    files.forEach(function (file) {
      var name = file.name || file;
      var el = document.createElement('div');
      el.className = 'photo-thumb';
      el.dataset.name = name;
      el.dataset.type = isVideo(name) ? 'video' : 'image';
      el.setAttribute('role', 'img');
      el.setAttribute('aria-label', name);
      addButtonKeyHandler(el);

      if (isVideo(name)) {
        el.innerHTML = '<div class="video-badge">&#9654;</div><span class="thumb-label"></span>';
        el.querySelector('.thumb-label').textContent = name;
      } else {
        el.textContent = name;
        var prefix = albumName === 'Pictures' ? 'Pictures/' : 'Pictures/' + albumName + '/';
        requestService('fs', 'readBinary', { path: prefix + name });
      }

      el.style.cssText = 'display:flex;align-items:center;justify-content:center;font-size:10px;color:#666;overflow:hidden;position:relative;aspect-ratio:1;background:#1a1a2e;cursor:pointer';
      el.addEventListener('click', (function (fileName, prefix) {
        return function () {
          openViewer(fileName, prefix);
        };
      })(name, albumName === 'Pictures' ? '' : albumName + '/'));
      albumGrid.appendChild(el);
    });
  }

  /* ─── Media List Rendering ─── */
  function renderMediaList(entries) {
    if (!grid) return;
    grid.innerHTML = '';
    mediaFiles = (entries || []).filter(function (e) {
      return isMedia(e.name || e);
    });

    if (mediaFiles.length === 0) {
      grid.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px;grid-column:1/-1">' + t('gallery.no_media') + '</div>';
      return;
    }

    mediaFiles.forEach(function (file) {
      var name = file.name || file;
      var el = document.createElement('div');
      el.className = 'photo-thumb';
      el.dataset.name = name;
      el.dataset.type = isVideo(name) ? 'video' : 'image';
      el.setAttribute('role', 'img');
      el.setAttribute('aria-label', name);
      addButtonKeyHandler(el);

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
      el.addEventListener('click', function () { openViewer(name, ''); });
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
      /* Update in main grid */
      var baseName = name.indexOf('/') !== -1 ? name.split('/').pop() : name;
      var thumbs = document.querySelectorAll('[data-name="' + baseName + '"], [data-name="' + name + '"]');
      for (var i = 0; i < thumbs.length; i++) {
        var thumb = thumbs[i];
        if (thumb) {
          thumb.textContent = '';
          thumb.style.backgroundImage = 'url(' + dataUrl + ')';
          thumb.style.backgroundSize = 'cover';
          thumb.style.backgroundPosition = 'center';
        }
      }
    }
  }

  /* ─── Viewer ─── */
  var currentViewingFile = null;
  var currentViewingPrefix = '';

  function openViewer(name, prefix) {
    if (!viewer) return;
    prefix = prefix || '';

    /* Stop slideshow if active */
    stopSlideshow();

    /* Reset rotation */
    rotationDeg = 0;
    if (viewerImg) viewerImg.style.transform = 'rotate(0deg)';

    /* 기존 비디오 요소 정리 */
    var oldVideo = viewer.querySelector('video');
    if (oldVideo) { oldVideo.pause(); oldVideo.remove(); }
    if (viewerImg) viewerImg.style.display = 'none';

    var fullPath = prefix ? prefix + name : name;
    var fsPath = 'Pictures/' + fullPath;

    if (isVideo(name)) {
      /* Show slideshow button as disabled for video */
      var ssBtn = document.getElementById('viewer-slideshow');
      if (ssBtn) ssBtn.style.display = 'none';
      var rotBtn = document.getElementById('viewer-rotate');
      if (rotBtn) rotBtn.style.display = 'none';

      if (!dataCache[fullPath] && !dataCache[name]) {
        requestService('fs', 'readBinary', { path: fsPath });
        var pollCount = 0;
        var poll = setInterval(function () {
          pollCount++;
          if (dataCache[fullPath] || dataCache[name] || pollCount > 50) {
            clearInterval(poll);
            if (dataCache[fullPath] || dataCache[name]) showVideo(fullPath, name);
          }
        }, 200);
      } else {
        showVideo(fullPath, name);
      }
    } else {
      /* 이미지 */
      var ssBtn2 = document.getElementById('viewer-slideshow');
      if (ssBtn2) ssBtn2.style.display = '';
      var rotBtn2 = document.getElementById('viewer-rotate');
      if (rotBtn2) rotBtn2.style.display = '';

      var cachedUrl = dataCache[fullPath] || dataCache[name];
      if (cachedUrl && viewerImg) {
        viewerImg.src = cachedUrl;
        viewerImg.style.display = 'block';
      } else {
        requestService('fs', 'readBinary', { path: fsPath });
        if (viewerImg) {
          viewerImg.style.display = 'block';
          viewerImg.src = '';
        }
      }
    }

    currentViewingFile = name;
    currentViewingPrefix = prefix;
    viewer.classList.remove('hidden');
  }

  var activeVideo = null;

  function showVideo(fullPath, name) {
    currentViewingFile = name;
    var url = dataCache[fullPath] || dataCache[name];
    if (!viewer || !url) return;
    var video = document.createElement('video');
    video.src = url;
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

    video.addEventListener('play', function () { if (vcPlay) vcPlay.textContent = '\u23F8'; });
    video.addEventListener('pause', function () { if (vcPlay) vcPlay.textContent = '\u25B6'; });
    video.addEventListener('ended', function () { if (vcPlay) vcPlay.textContent = '\u25B6'; });

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

  /* ─── Share Button (동적 추가) ─── */
  (function () {
    var viewerEl = document.getElementById('viewer');
    if (!viewerEl) return;
    if (!document.getElementById('viewer-share')) {
      var shareBtn = document.createElement('button');
      shareBtn.id = 'viewer-share';
      shareBtn.setAttribute('aria-label', t('gallery.share'));
      shareBtn.textContent = '\u2B06';
      shareBtn.style.cssText =
        'position:absolute;bottom:20px;right:228px;background:rgba(74,158,255,0.8);' +
        'border:none;color:white;width:44px;height:44px;border-radius:22px;' +
        'font-size:20px;cursor:pointer;z-index:10;';
      shareBtn.addEventListener('click', function () {
        if (!currentViewingFile) return;
        var imagePath = 'Pictures/' + (currentViewingPrefix ? currentViewingPrefix : '') + currentViewingFile;
        if (typeof ZylIntent !== 'undefined') {
          ZylIntent.startIntent({
            action: ZylIntent.ACTION.SEND,
            data: imagePath,
            mimeType: 'image/*'
          });
        } else {
          ZylBridge.sendToSystem({
            type: 'service.request',
            service: 'share',
            method: 'share',
            params: { path: imagePath, mimeType: 'image/*' }
          });
        }
      });
      viewerEl.appendChild(shareBtn);
    }
  })();

  /* ─── Rotate Button ─── */
  var rotateBtn = document.getElementById('viewer-rotate');
  if (rotateBtn) {
    rotateBtn.addEventListener('click', function () {
      if (!viewerImg || viewerImg.style.display === 'none') return;
      rotationDeg = (rotationDeg + 90) % 360;
      viewerImg.style.transform = 'rotate(' + rotationDeg + 'deg)';
    });
  }

  /* ─── Slideshow ─── */
  var slideshowBtn = document.getElementById('viewer-slideshow');
  var slideshowControlsEl = document.getElementById('slideshow-controls');
  var slideshowProgressFill = document.getElementById('slideshow-progress-fill');
  var slideshowStatusEl = document.getElementById('slideshow-status');

  function getSlideshowFiles() {
    var files = [];
    /* Use current context: album or all */
    var source = currentAlbumName ? (albumData[currentAlbumName] || []) : mediaFiles;
    source.forEach(function (f) {
      var name = f.name || f;
      if (isImage(name)) {
        files.push(name);
      }
    });
    return files;
  }

  function startSlideshow() {
    slideshowFiles = getSlideshowFiles();
    if (slideshowFiles.length < 2) return;

    /* Find current image index */
    var curName = currentViewingFile;
    slideshowIndex = 0;
    for (var i = 0; i < slideshowFiles.length; i++) {
      if (slideshowFiles[i] === curName) {
        slideshowIndex = i;
        break;
      }
    }

    slideshowActive = true;
    slideshowPaused = false;
    if (slideshowControlsEl) slideshowControlsEl.classList.remove('hidden');
    updateSlideshowStatus();
    scheduleSlideshowAdvance();
  }

  function stopSlideshow() {
    slideshowActive = false;
    slideshowPaused = false;
    if (slideshowTimer) { clearTimeout(slideshowTimer); slideshowTimer = null; }
    if (slideshowProgressTimer) { clearInterval(slideshowProgressTimer); slideshowProgressTimer = null; }
    if (slideshowControlsEl) slideshowControlsEl.classList.add('hidden');
    if (slideshowProgressFill) slideshowProgressFill.style.width = '0%';
  }

  function toggleSlideshowPause() {
    if (!slideshowActive) return;
    if (slideshowPaused) {
      slideshowPaused = false;
      updateSlideshowStatus();
      scheduleSlideshowAdvance();
    } else {
      slideshowPaused = true;
      if (slideshowTimer) { clearTimeout(slideshowTimer); slideshowTimer = null; }
      if (slideshowProgressTimer) { clearInterval(slideshowProgressTimer); slideshowProgressTimer = null; }
      updateSlideshowStatus();
    }
  }

  function updateSlideshowStatus() {
    if (!slideshowStatusEl) return;
    if (slideshowPaused) {
      slideshowStatusEl.textContent = t('gallery.slideshow_paused');
    } else {
      slideshowStatusEl.textContent = t('gallery.slideshow_playing');
    }
  }

  function scheduleSlideshowAdvance() {
    if (!slideshowActive || slideshowPaused) return;

    var startTime = Date.now();
    if (slideshowProgressFill) slideshowProgressFill.style.width = '0%';

    slideshowProgressTimer = setInterval(function () {
      var elapsed = Date.now() - startTime;
      var pct = Math.min((elapsed / SLIDESHOW_INTERVAL) * 100, 100);
      if (slideshowProgressFill) slideshowProgressFill.style.width = pct + '%';
    }, 50);

    slideshowTimer = setTimeout(function () {
      if (slideshowProgressTimer) { clearInterval(slideshowProgressTimer); slideshowProgressTimer = null; }
      if (slideshowProgressFill) slideshowProgressFill.style.width = '100%';

      slideshowIndex++;
      if (slideshowIndex >= slideshowFiles.length) {
        /* End of slideshow */
        stopSlideshow();
        return;
      }

      var nextName = slideshowFiles[slideshowIndex];
      /* Reset rotation for new image */
      rotationDeg = 0;
      if (viewerImg) viewerImg.style.transform = 'rotate(0deg)';

      var fullPath = currentViewingPrefix ? currentViewingPrefix + nextName : nextName;
      var cachedUrl = dataCache[fullPath] || dataCache[nextName];
      if (cachedUrl && viewerImg) {
        viewerImg.src = cachedUrl;
        viewerImg.style.display = 'block';
      } else {
        requestService('fs', 'readBinary', { path: 'Pictures/' + fullPath });
        if (viewerImg) { viewerImg.style.display = 'block'; viewerImg.src = ''; }
      }
      currentViewingFile = nextName;
      scheduleSlideshowAdvance();
    }, SLIDESHOW_INTERVAL);
  }

  if (slideshowBtn) {
    slideshowBtn.addEventListener('click', function () {
      if (slideshowActive) {
        stopSlideshow();
      } else {
        startSlideshow();
      }
    });
  }

  /* Tap on viewer image to toggle slideshow pause */
  if (viewerImg) {
    viewerImg.addEventListener('click', function () {
      if (slideshowActive) {
        toggleSlideshowPause();
      }
    });
  }

  /* ─── Viewer Close ─── */
  function closeViewer() {
    stopSlideshow();
    rotationDeg = 0;
    if (viewer) {
      viewer.classList.add('hidden');
      var vid = viewer.querySelector('video');
      if (vid) { vid.pause(); vid.remove(); }
      activeVideo = null;
    }
    if (viewerImg) { viewerImg.src = ''; viewerImg.style.display = 'none'; viewerImg.style.transform = 'rotate(0deg)'; }
    var vc = document.getElementById('video-controls');
    if (vc) vc.classList.add('hidden');
    currentViewingFile = null;
    currentViewingPrefix = '';
  }

  /* ─── Viewer Delete ─── */
  var viewerDeleteBtn = document.getElementById('viewer-delete');
  if (viewerDeleteBtn) {
    viewerDeleteBtn.addEventListener('click', function () {
      if (!currentViewingFile) return;
      var deleteTitle = t('gallery.delete_title');
      var deleteMsg = t('gallery.confirm_delete');
      var fileToDelete = currentViewingFile;
      var prefixToDelete = currentViewingPrefix;
      showGalleryConfirm(deleteTitle, deleteMsg, function () {
        var fullPath = prefixToDelete ? prefixToDelete + fileToDelete : fileToDelete;
        requestService('fs', 'remove', { path: 'Pictures/' + fullPath });
        mediaFiles = mediaFiles.filter(function (f) { return (f.name || f) !== fileToDelete; });
        delete dataCache[fileToDelete];
        delete dataCache[fullPath];
        closeViewer();
        if (currentView === 'all') {
          renderMediaList(mediaFiles);
        }
      });
    });
  }

  /* ─── Init ─── */
  updateTabLabels();
  requestMedia();
})();
