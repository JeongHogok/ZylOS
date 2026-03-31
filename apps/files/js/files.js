// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 파일 관리자 UI — 파일 탐색, 정렬, 컨텍스트 메뉴
// 수행범위: 브레드크럼 탐색, 파일 목록 렌더링, 정렬 옵션, 파일 작업 메뉴
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 파일 관리자 UI와 탐색 로직만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── File System (loaded from service) ─── */
  var fileSystem = {};
  var serviceReady = false;

  /* Request filesystem data from central service */
  function requestService(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
  }

  function requestFileSystem() {
    requestService('fs', 'getAllData');
    requestService('storage', 'getFormatted');
  }

  /* Listen for messages from emulator */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* ── Navigation back handling ── */
      if (msg.type === 'navigation.back') {
        if (currentPath !== '/') {
          btnBack.click();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      if (msg.service === 'fs' && msg.data) {
        if (msg.method === 'getAllData') {
          /* Rust fs_read_dir 응답을 앱 형식으로 변환 */
          var rootEntries = msg.data.tree && msg.data.tree['/'] ? msg.data.tree['/'] : msg.data;
          fileSystem['/'] = normalizeEntries(Array.isArray(rootEntries) ? rootEntries : []);
          serviceReady = true;
          renderFiles();
        } else if (msg.method === 'getDirectory' && msg.params && msg.params.path) {
          fileSystem[msg.params.path] = normalizeEntries(msg.data || []);
          if (currentPath === msg.params.path) renderFiles();
        }
      }

      if (msg.service === 'storage' && msg.method === 'getFormatted' && msg.data) {
        updateStorageBar(msg.data);
      }
    } catch (err) { /* ignore */ }
  });

  /* Rust FileEntry → Files 앱 형식 변환
     Rust: {name, is_dir, size, modified, file_type}
     App:  {name, type, date, size} */
  function normalizeEntries(entries) {
    if (!Array.isArray(entries)) return [];
    return entries.map(function (e) {
      /* 이미 앱 형식이면 그대로 */
      if (e.type && e.date) return e;
      /* Rust FileEntry → 앱 형식 */
      return {
        name: e.name,
        type: e.is_dir ? 'folder' : (e.file_type || 'unknown'),
        date: e.modified || '',
        size: e.is_dir ? null : (e.size || 0)
      };
    });
  }

  function updateStorageBar(data) {
    var valueEl = document.getElementById('storage-value');
    var fillEl = document.getElementById('storage-fill');
    if (valueEl && data) {
      valueEl.textContent = (data.used || '0 B') + ' / ' + (data.total || '0 B');
    }
    if (fillEl && data) {
      fillEl.style.width = Math.min(data.percent || 0, 100) + '%';
    }
  }

  requestFileSystem();

  /* ─── State ─── */
  var currentPath = '/';
  var currentSort = 'name';
  var contextTarget = null;

  function escapeHtml(str) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(str || ''));
    return d.innerHTML;
  }

  /* ─── DOM ─── */
  var headerTitle = document.getElementById('header-title');
  var btnBack = document.getElementById('btn-back');
  var btnSort = document.getElementById('btn-sort');
  var breadcrumb = document.getElementById('breadcrumb');
  var fileList = document.getElementById('file-list');
  var sortMenu = document.getElementById('sort-menu');
  var contextMenu = document.getElementById('context-menu');
  var overlay = document.getElementById('overlay');

  /* ─── SVG Icons ─── */
  var icons = {
    folder: '<svg viewBox="0 0 24 24"><path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg>',
    image: '<svg viewBox="0 0 24 24"><path d="M21 19V5c0-1.1-.9-2-2-2H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2zM8.5 13.5l2.5 3.01L14.5 12l4.5 6H5l3.5-4.5z"/></svg>',
    document: '<svg viewBox="0 0 24 24"><path d="M14 2H6c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z"/></svg>',
    video: '<svg viewBox="0 0 24 24"><path d="M17 10.5V7c0-.55-.45-1-1-1H4c-.55 0-1 .45-1 1v10c0 .55.45 1 1 1h12c.55 0 1-.45 1-1v-3.5l4 4v-11l-4 4z"/></svg>',
    audio: '<svg viewBox="0 0 24 24"><path d="M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z"/></svg>',
    code: '<svg viewBox="0 0 24 24"><path d="M9.4 16.6L4.8 12l4.6-4.6L8 6l-6 6 6 6 1.4-1.4zm5.2 0l4.6-4.6-4.6-4.6L16 6l6 6-6 6-1.4-1.4z"/></svg>',
    archive: '<svg viewBox="0 0 24 24"><path d="M20.54 5.23l-1.39-1.68C18.88 3.21 18.47 3 18 3H6c-.47 0-.88.21-1.16.55L3.46 5.23C3.17 5.57 3 6.02 3 6.5V19c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2V6.5c0-.48-.17-.93-.46-1.27zM12 17.5L6.5 12H10v-2h4v2h3.5L12 17.5zM5.12 5l.81-1h12l.94 1H5.12z"/></svg>',
    unknown: '<svg viewBox="0 0 24 24"><path d="M6 2c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V8l-6-6H6zm7 7V3.5L18.5 9H13z"/></svg>',
    more: '<svg viewBox="0 0 24 24"><path d="M12 8c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm0 2c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm0 6c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z"/></svg>'
  };

  /* ─── Utility ─── */
  function formatSize(bytes) {
    if (bytes === null || bytes === undefined) return '';
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + ' MB';
    return (bytes / 1073741824).toFixed(1) + ' GB';
  }

  function formatDate(dateStr) {
    if (!dateStr) return '';
    /* Unix timestamp(초) 또는 ISO 문자열 모두 처리 */
    var ts = parseInt(dateStr, 10);
    var d = (ts > 1000000000) ? new Date(ts * 1000) : new Date(dateStr);
    if (isNaN(d.getTime())) return '';
    var m = d.getMonth() + 1;
    var day = d.getDate();
    return d.getFullYear() + '.' + (m < 10 ? '0' : '') + m + '.' + (day < 10 ? '0' : '') + day;
  }

  function getIconClass(type) {
    return icons[type] ? 'icon-' + type : 'icon-unknown';
  }

  function getIconSvg(type) {
    return icons[type] || icons.unknown;
  }

  /* ─── Sort Files ─── */
  function sortFiles(files) {
    var sorted = files.slice();
    /* Folders first */
    sorted.sort(function (a, b) {
      if (a.type === 'folder' && b.type !== 'folder') return -1;
      if (a.type !== 'folder' && b.type === 'folder') return 1;

      if (currentSort === 'name') {
        return a.name.localeCompare(b.name);
      } else if (currentSort === 'date') {
        return new Date(b.date) - new Date(a.date);
      } else if (currentSort === 'size') {
        return (b.size || 0) - (a.size || 0);
      }
      return 0;
    });
    return sorted;
  }

  /* ─── Render Breadcrumb ─── */
  function renderBreadcrumb() {
    breadcrumb.innerHTML = '';

    /* Home */
    var homeEl = document.createElement('span');
    homeEl.className = 'crumb' + (currentPath === '/' ? ' active' : '');
    homeEl.dataset.path = '/';
    homeEl.innerHTML = '<svg viewBox="0 0 24 24" width="16" height="16"><path fill="currentColor" d="M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z"/></svg>';
    homeEl.addEventListener('click', function () { navigateTo('/'); });
    breadcrumb.appendChild(homeEl);

    if (currentPath !== '/') {
      var parts = currentPath.split('/').filter(Boolean);
      var path = '';
      parts.forEach(function (part, i) {
        path += '/' + part;
        var sep = document.createElement('span');
        sep.className = 'crumb-sep';
        sep.textContent = '/';
        breadcrumb.appendChild(sep);

        var el = document.createElement('span');
        el.className = 'crumb' + (i === parts.length - 1 ? ' active' : '');
        el.dataset.path = path;
        el.textContent = part;
        var p = path;
        el.addEventListener('click', function () { navigateTo(p); });
        breadcrumb.appendChild(el);
      });
    }
  }

  /* ─── Render File List ─── */
  function renderFiles() {
    fileList.innerHTML = '';

    if (!serviceReady) {
      fileList.innerHTML = '<div class="empty-state"><span>Loading...</span></div>';
      return;
    }

    var files = fileSystem[currentPath];

    if (!files || files.length === 0) {
      fileList.innerHTML =
        '<div class="empty-state">' +
        '<svg viewBox="0 0 24 24"><path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg>' +
        '<span>Empty folder</span></div>';
      return;
    }

    var sorted = sortFiles(files);
    sorted.forEach(function (file) {
      var el = document.createElement('div');
      el.className = 'file-item';
      el.dataset.name = file.name;
      el.dataset.type = file.type;

      var meta = formatDate(file.date);
      if (file.size !== null) meta += ' \u00B7 ' + formatSize(file.size);

      el.innerHTML =
        '<div class="file-icon ' + getIconClass(file.type) + '">' + getIconSvg(file.type) + '</div>' +
        '<div class="file-info">' +
          '<span class="file-name">' + escapeHtml(file.name) + '</span>' +
          '<span class="file-meta">' + meta + '</span>' +
        '</div>' +
        '<button class="file-more" aria-label="More">' + icons.more + '</button>';

      /* Tap file */
      el.addEventListener('click', function (e) {
        if (e.target.closest('.file-more')) return;
        if (file.type === 'folder') {
          var newPath = currentPath === '/' ? '/' + file.name : currentPath + '/' + file.name;
          navigateTo(newPath);
        }
      });

      /* More button */
      el.querySelector('.file-more').addEventListener('click', function (e) {
        e.stopPropagation();
        showContextMenu(file, e);
      });

      /* Long press for context menu */
      var pressTimer;
      el.addEventListener('touchstart', function (e) {
        pressTimer = setTimeout(function () {
          showContextMenu(file, e);
        }, 500);
      });
      el.addEventListener('touchend', function () { clearTimeout(pressTimer); });
      el.addEventListener('touchmove', function () { clearTimeout(pressTimer); });

      fileList.appendChild(el);
    });
  }

  /* ─── Navigate ─── */
  function navigateTo(path) {
    currentPath = path;
    var folderName = path === '/' ? 'Files' : path.split('/').pop();
    headerTitle.textContent = folderName;
    btnBack.classList.toggle('hidden', path === '/');
    renderBreadcrumb();

    /* 해당 경로의 데이터가 없으면 서비스에 요청 */
    if (!fileSystem[path]) {
      ZylBridge.sendToSystem({
        type: 'service.request',
        service: 'fs',
        method: 'getDirectory',
        params: { path: path }
      });
      fileList.innerHTML = '<div style="text-align:center;opacity:0.5;padding:32px">Loading...</div>';
    } else {
      renderFiles();
    }
  }

  /* ─── Context Menu ─── */
  function showContextMenu(file, e) {
    contextTarget = file;
    contextMenu.classList.remove('hidden');
    overlay.classList.remove('hidden');

    /* Position near the tap/click */
    var x = e.clientX || (e.touches && e.touches[0] ? e.touches[0].clientX : 200);
    var y = e.clientY || (e.touches && e.touches[0] ? e.touches[0].clientY : 300);

    var menuW = 180;
    var menuH = 220;
    if (x + menuW > window.innerWidth) x = window.innerWidth - menuW - 12;
    if (y + menuH > window.innerHeight) y = window.innerHeight - menuH - 12;

    contextMenu.style.left = x + 'px';
    contextMenu.style.top = y + 'px';
  }

  function hideContextMenu() {
    contextMenu.classList.add('hidden');
    overlay.classList.add('hidden');
    contextTarget = null;
  }

  overlay.addEventListener('click', function () {
    hideContextMenu();
    sortMenu.classList.add('hidden');
  });

  /* Context menu actions */
  document.querySelectorAll('.ctx-item').forEach(function (item) {
    item.addEventListener('click', function () {
      var action = item.dataset.action;
      if (!contextTarget) { hideContextMenu(); return; }

      if (action === 'open') {
        if (contextTarget.type === 'folder') {
          var newPath = currentPath === '/' ? '/' + contextTarget.name : currentPath + '/' + contextTarget.name;
          navigateTo(newPath);
        } else {
          openFileWithApp(contextTarget);
        }
      } else if (action === 'delete') {
        var filePath = currentPath === '/' ? '/' + contextTarget.name : currentPath + '/' + contextTarget.name;
        if (confirm('Delete "' + contextTarget.name + '"?')) {
          requestService('fs', 'remove', { path: filePath });
          var files = fileSystem[currentPath];
          if (files) {
            var idx = -1;
            for (var di = 0; di < files.length; di++) {
              if (files[di].name === contextTarget.name) { idx = di; break; }
            }
            if (idx !== -1) {
              files.splice(idx, 1);
              renderFiles();
            }
          }
        }
      } else if (action === 'rename') {
        var oldName = contextTarget.name;
        var newName = prompt('Rename:', oldName);
        if (newName && newName !== oldName) {
          var oldPath = currentPath === '/' ? '/' + oldName : currentPath + '/' + oldName;
          var newPath = currentPath === '/' ? '/' + newName : currentPath + '/' + newName;
          requestService('fs', 'rename', { oldPath: oldPath, newPath: newPath });
          contextTarget.name = newName;
          renderFiles();
        }
      } else if (action === 'share') {
        /* In emulator, show a toast-like message */
        alert('Share: ' + contextTarget.name);
      }

      hideContextMenu();
    });
  });

  /* ─── Back Button ─── */
  btnBack.addEventListener('click', function () {
    if (currentPath === '/') return;
    var parts = currentPath.split('/').filter(Boolean);
    parts.pop();
    navigateTo('/' + parts.join('/') || '/');
  });

  /* ─── Sort ─── */
  btnSort.addEventListener('click', function (e) {
    e.stopPropagation();
    sortMenu.classList.toggle('hidden');
  });

  document.querySelectorAll('.sort-option').forEach(function (opt) {
    opt.addEventListener('click', function () {
      currentSort = opt.dataset.sort;
      document.querySelectorAll('.sort-option').forEach(function (o) {
        o.classList.toggle('active', o.dataset.sort === currentSort);
      });
      sortMenu.classList.add('hidden');
      renderFiles();
    });
  });

  /* Close menus on outside tap */
  document.addEventListener('click', function (e) {
    if (!sortMenu.classList.contains('hidden') && !e.target.closest('#sort-menu') && !e.target.closest('#btn-sort')) {
      sortMenu.classList.add('hidden');
    }
  });

  /* ─── FAB: New Folder ─── */
  var fabBtn = document.getElementById('fab');
  if (fabBtn) {
    fabBtn.addEventListener('click', function () {
      var folderName = prompt('New folder name:');
      if (folderName && folderName.trim()) {
        var newPath = currentPath === '/' ? '/' + folderName.trim() : currentPath + '/' + folderName.trim();
        requestService('fs', 'mkdir', { path: newPath });
        /* Optimistic update */
        var files = fileSystem[currentPath] || [];
        files.push({ name: folderName.trim(), type: 'folder', size: 0 });
        fileSystem[currentPath] = files;
        renderFiles();
      }
    });
  }

  /* ─── View Toggle ─── */
  var viewToggle = document.getElementById('btn-view');
  var isGridView = false;
  if (viewToggle) {
    viewToggle.addEventListener('click', function () {
      isGridView = !isGridView;
      var list = document.getElementById('file-list');
      if (list) {
        list.classList.toggle('grid-view', isGridView);
      }
    });
  }

  /* ─── Open file with corresponding app ─── */
  function openFileWithApp(file) {
    if (!file || !file.name) return;
    var ext = file.name.split('.').pop().toLowerCase();
    var imageExts = ['jpg', 'jpeg', 'png', 'gif', 'webp', 'bmp'];
    var audioExts = ['mp3', 'ogg', 'wav', 'flac', 'aac'];
    var videoExts = ['mp4', 'mov', 'webm', 'mkv', 'avi'];
    var textExts  = ['txt', 'md', 'log', 'json', 'xml', 'csv'];

    var filePath = currentPath === '/' ? '/' + file.name : currentPath + '/' + file.name;
    var detectedMime = 'application/octet-stream';
    var appId = null;

    if (imageExts.indexOf(ext) !== -1) {
      detectedMime = 'image/*';
      appId = 'com.zylos.gallery';
    } else if (videoExts.indexOf(ext) !== -1) {
      detectedMime = 'video/*';
      appId = 'com.zylos.gallery';
    } else if (audioExts.indexOf(ext) !== -1) {
      detectedMime = 'audio/*';
      appId = 'com.zylos.music';
    } else if (textExts.indexOf(ext) !== -1) {
      detectedMime = 'text/plain';
      appId = 'com.zylos.notes';
    }

    /* Intent 연동: 파일 유형에 맞는 앱으로 열기 */
    if (appId && typeof ZylIntent !== 'undefined') {
      ZylIntent.startIntent({
        action: ZylIntent.ACTION.VIEW,
        data: filePath,
        mimeType: detectedMime
      });
      return;
    }

    /* ZylIntent 미로드 시 폴백: 직접 앱 실행 */
    if (appId) {
      ZylBridge.sendToSystem({
        type: 'app.launch',
        appId: appId
      });
    }
  }

  /* ─── Init ─── */
  navigateTo('/');

})();
