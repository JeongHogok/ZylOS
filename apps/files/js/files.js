// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 파일 관리자 UI — 파일 탐색, 정렬, 컨텍스트 메뉴
// 수행범위: 브레드크럼 탐색, 파일 목록 렌더링, 정렬 옵션, 파일 작업 메뉴
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 파일 관리자 UI와 탐색 로직만 담당
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── Simulated File System ─── */
  var fileSystem = {
    '/': [
      { name: 'Documents', type: 'folder', date: '2026-03-27', size: null },
      { name: 'Downloads', type: 'folder', date: '2026-03-28', size: null },
      { name: 'Pictures', type: 'folder', date: '2026-03-25', size: null },
      { name: 'Music', type: 'folder', date: '2026-03-20', size: null },
      { name: 'Videos', type: 'folder', date: '2026-03-15', size: null },
      { name: 'readme.txt', type: 'document', date: '2026-03-28', size: 2400 },
      { name: 'system.log', type: 'code', date: '2026-03-28', size: 15360 }
    ],
    '/Documents': [
      { name: 'Work', type: 'folder', date: '2026-03-26', size: null },
      { name: 'report_2026.pdf', type: 'document', date: '2026-03-26', size: 1258291 },
      { name: 'notes.md', type: 'code', date: '2026-03-27', size: 4096 },
      { name: 'budget.xlsx', type: 'document', date: '2026-03-20', size: 52480 },
      { name: 'presentation.pptx', type: 'document', date: '2026-03-18', size: 3145728 }
    ],
    '/Downloads': [
      { name: 'bpios-image-v0.1.img.gz', type: 'archive', date: '2026-03-28', size: 524288000 },
      { name: 'linux-6.6.63.tar.xz', type: 'archive', date: '2026-03-25', size: 142606336 },
      { name: 'wallpaper.jpg', type: 'image', date: '2026-03-27', size: 2097152 },
      { name: 'setup.sh', type: 'code', date: '2026-03-24', size: 8192 }
    ],
    '/Pictures': [
      { name: 'Screenshots', type: 'folder', date: '2026-03-28', size: null },
      { name: 'photo_001.jpg', type: 'image', date: '2026-03-27', size: 3145728 },
      { name: 'photo_002.jpg', type: 'image', date: '2026-03-26', size: 2621440 },
      { name: 'avatar.png', type: 'image', date: '2026-03-20', size: 524288 },
      { name: 'banner.svg', type: 'image', date: '2026-03-15', size: 12288 }
    ],
    '/Music': [
      { name: 'playlist_01.mp3', type: 'audio', date: '2026-03-10', size: 5242880 },
      { name: 'podcast_ep12.m4a', type: 'audio', date: '2026-03-22', size: 31457280 },
      { name: 'ringtone.ogg', type: 'audio', date: '2026-02-15', size: 204800 }
    ],
    '/Videos': [
      { name: 'demo_riscv.mp4', type: 'video', date: '2026-03-18', size: 157286400 },
      { name: 'screen_record_01.webm', type: 'video', date: '2026-03-25', size: 52428800 }
    ]
  };

  /* ─── State ─── */
  var currentPath = '/';
  var currentSort = 'name';
  var contextTarget = null;

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
    var d = new Date(dateStr);
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
          '<span class="file-name">' + file.name + '</span>' +
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
    renderFiles();
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
        }
      } else if (action === 'delete') {
        var files = fileSystem[currentPath];
        if (files) {
          var idx = files.findIndex(function (f) { return f.name === contextTarget.name; });
          if (idx !== -1) {
            files.splice(idx, 1);
            renderFiles();
          }
        }
      } else if (action === 'rename') {
        /* In a real app, show rename dialog */
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

  /* ─── Init ─── */
  navigateTo('/');

})();
