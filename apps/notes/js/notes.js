// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 메모 앱 — 텍스트 노트 생성/편집/삭제 (FS 저장)
// 수행범위: 노트 목록, 편집기, 검색, 정렬, 서식 도구, fs 서비스 연동
// 의존방향: fs 서비스 (postMessage IPC)
// SOLID: SRP — 노트 관리 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var list = document.getElementById('notes-list');
  var editor = document.getElementById('editor');
  var titleEl = document.getElementById('note-title');
  var bodyEl = document.getElementById('note-body');
  var searchInput = document.getElementById('search-input');
  var btnSortDate = document.getElementById('btn-sort-date');
  var btnSortName = document.getElementById('btn-sort-name');
  var listControls = document.getElementById('list-controls');
  var notes = [];
  var currentNote = null;
  var sortMode = 'date'; /* 'date' | 'name' */
  /* FIX: In-memory content cache for content-based search.
   *      Keys are note filenames; values are loaded body text.
   *      Populated lazily when a note is opened; used by filterNotes(). */
  var noteBodyCache = {};

  function t(key) {
    return typeof zylI18n !== 'undefined' ? zylI18n.t(key) : key;
  }

  function requestNotes() {
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'fs', method: 'getDirectory', params: { path: 'Documents/Notes' }
    });
  }

  /* FIX: Consolidated single message listener — previously had two separate
   *       window.addEventListener('message', ...) registrations (one for navigation/
   *       getDirectory, one for getFileContent), causing duplicate handler registration
   *       on every openNote() call. Now all IPC handled in one place.
   * SOLID: SRP — single responsibility for message dispatch */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* ── Navigation back handling ── */
      if (msg.type === 'navigation.back') {
        if (editor && !editor.classList.contains('hidden')) {
          closeEditor();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      /* Directory listing → render note list */
      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        renderNotes(msg.data);
        return;
      }

      /* FIX: File content response handler merged here (was a separate duplicate listener) */
      if (msg.service === 'fs' && msg.method === 'getFileContent' && msg.data) {
        if (bodyEl) bodyEl.value = msg.data;
        /* Populate body cache for content-aware search */
        if (currentNote) noteBodyCache[currentNote] = msg.data;
        return;
      }
    } catch (err) { if (typeof console !== 'undefined') console.error('[Notes] message parse error:', err); }
  });

  /* ── Sort helpers ── */
  function sortNotes(arr) {
    var sorted = arr.slice();
    if (sortMode === 'name') {
      sorted.sort(function (a, b) {
        var nameA = (a.name || '').toLowerCase();
        var nameB = (b.name || '').toLowerCase();
        if (nameA < nameB) return -1;
        if (nameA > nameB) return 1;
        return 0;
      });
    } else {
      /* date: newest first — use modifiedAt or fallback to reverse order */
      sorted.sort(function (a, b) {
        var tA = a.modifiedAt || a.modified || 0;
        var tB = b.modifiedAt || b.modified || 0;
        return tB - tA;
      });
    }
    return sorted;
  }

  /* ── Search filter ── */
  /* FIX: Content-aware search — checks title first, then cached body if available.
   *      Body cache is populated when notes are opened; first query after loading
   *      a note will include body content in subsequent searches. */
  function filterNotes(arr, query) {
    if (!query) return arr;
    var q = query.toLowerCase();
    return arr.filter(function (n) {
      if ((n.name || '').toLowerCase().indexOf(q) !== -1) return true;
      var body = noteBodyCache[n.name];
      if (body && body.toLowerCase().indexOf(q) !== -1) return true;
      return false;
    });
  }

  function renderNotes(entries) {
    if (!list) return;
    list.innerHTML = '';
    notes = (entries || []).filter(function (e) { return (e.name || '').indexOf('.txt') !== -1; });

    var query = searchInput ? searchInput.value.trim() : '';
    var filtered = filterNotes(notes, query);
    var sorted = sortNotes(filtered);

    if (sorted.length === 0) {
      list.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px">' + t('notes.no_notes') + '</div>';
      return;
    }
    sorted.forEach(function (n) {
      var el = document.createElement('div'); el.className = 'note-item';
      el.textContent = n.name.replace('.txt', '');
      el.setAttribute('role', 'listitem');
      el.setAttribute('aria-label', n.name.replace('.txt', ''));
      el.setAttribute('tabindex', '0');
      el.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          el.click();
        }
      });
      el.addEventListener('click', function () { openNote(n.name); });
      list.appendChild(el);
    });
  }

  /* ── Re-render from cached notes (for search/sort changes) ── */
  function reRenderList() {
    if (!list) return;
    list.innerHTML = '';
    var query = searchInput ? searchInput.value.trim() : '';
    var filtered = filterNotes(notes, query);
    var sorted = sortNotes(filtered);

    if (sorted.length === 0) {
      list.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px">' + t('notes.no_notes') + '</div>';
      return;
    }
    sorted.forEach(function (n) {
      var el = document.createElement('div'); el.className = 'note-item';
      el.textContent = n.name.replace('.txt', '');
      el.setAttribute('role', 'listitem');
      el.setAttribute('aria-label', n.name.replace('.txt', ''));
      el.setAttribute('tabindex', '0');
      el.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          el.click();
        }
      });
      el.addEventListener('click', function () { openNote(n.name); });
      list.appendChild(el);
    });
  }

  /* ── Search input handler ── */
  if (searchInput) {
    searchInput.addEventListener('input', function () {
      reRenderList();
    });
  }

  /* ── Sort button handlers ── */
  function setSortMode(mode) {
    sortMode = mode;
    if (btnSortDate) btnSortDate.classList.toggle('active', mode === 'date');
    if (btnSortName) btnSortName.classList.toggle('active', mode === 'name');
    reRenderList();
  }

  if (btnSortDate) {
    btnSortDate.addEventListener('click', function () { setSortMode('date'); });
  }
  if (btnSortName) {
    btnSortName.addEventListener('click', function () { setSortMode('name'); });
  }

  function openNote(filename) {
    currentNote = filename;
    if (titleEl) titleEl.value = filename.replace('.txt', '');
    if (bodyEl) bodyEl.value = '';
    if (list) list.classList.add('hidden');
    if (editor) editor.classList.remove('hidden');
    if (document.getElementById('header')) document.getElementById('header').classList.add('hidden');
    if (listControls) listControls.classList.add('hidden');
    /* 파일 내용 요청 */
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'fs', method: 'getFileContent', params: { path: 'Documents/Notes/' + filename }
    });
  }

  function closeEditor() {
    if (list) list.classList.remove('hidden');
    if (editor) editor.classList.add('hidden');
    if (document.getElementById('header')) document.getElementById('header').classList.remove('hidden');
    if (listControls) listControls.classList.remove('hidden');
    currentNote = null;
    requestNotes();
  }

  if (document.getElementById('btn-back')) document.getElementById('btn-back').addEventListener('click', closeEditor);

  if (document.getElementById('btn-save')) document.getElementById('btn-save').addEventListener('click', function () {
    var title = titleEl ? titleEl.value.trim() : '';
    var body = bodyEl ? bodyEl.value : '';
    if (!title) return;

    var newFileName = title + '.txt';

    /* If title changed, rename old file first, then write */
    var renamePromise;
    if (currentNote && currentNote !== newFileName) {
      renamePromise = ZylBridge.requestService('fs', 'rename', {
        oldPath: 'Documents/Notes/' + currentNote, newPath: 'Documents/Notes/' + newFileName
      });
    } else {
      renamePromise = Promise.resolve();
    }

    renamePromise.then(function () {
      return ZylBridge.requestService('fs', 'writeFile', {
        path: 'Documents/Notes/' + newFileName, content: body
      });
    }).then(function () {
      /* Update body cache on save (rename may change key) */
      if (currentNote && currentNote !== newFileName) {
        delete noteBodyCache[currentNote];
      }
      noteBodyCache[newFileName] = body;
      if (typeof ZylToast !== 'undefined') ZylToast.success(t('notes.save_success'));
      closeEditor();
    }).catch(function () {
      if (typeof ZylToast !== 'undefined') ZylToast.error(t('notes.save_error'));
    });
  });

  if (document.getElementById('btn-delete')) document.getElementById('btn-delete').addEventListener('click', function () {
    if (!currentNote) return;
    var noteToDelete = currentNote;
    ZylBridge.requestService('fs', 'remove', {
      path: 'Documents/Notes/' + noteToDelete
    }).then(function () {
      /* Clean body cache on delete */
      delete noteBodyCache[noteToDelete];
      if (typeof ZylToast !== 'undefined') ZylToast.success(t('notes.delete_success'));
      closeEditor();
    }).catch(function () {
      if (typeof ZylToast !== 'undefined') ZylToast.error(t('notes.delete_error'));
    });
  });

  if (document.getElementById('btn-new')) document.getElementById('btn-new').addEventListener('click', function () {
    currentNote = null;
    if (titleEl) titleEl.value = '';
    if (bodyEl) bodyEl.value = '';
    if (list) list.classList.add('hidden');
    if (editor) editor.classList.remove('hidden');
    if (document.getElementById('header')) document.getElementById('header').classList.add('hidden');
    if (listControls) listControls.classList.add('hidden');
  });

  /* ── Formatting toolbar ── */
  function insertMarkdown(prefix, suffix) {
    if (!bodyEl) return;
    var start = bodyEl.selectionStart;
    var end = bodyEl.selectionEnd;
    var text = bodyEl.value;
    var selected = text.substring(start, end);

    if (selected) {
      bodyEl.value = text.substring(0, start) + prefix + selected + suffix + text.substring(end);
      bodyEl.selectionStart = start + prefix.length;
      bodyEl.selectionEnd = end + prefix.length;
    } else {
      bodyEl.value = text.substring(0, start) + prefix + suffix + text.substring(end);
      bodyEl.selectionStart = start + prefix.length;
      bodyEl.selectionEnd = start + prefix.length;
    }
    bodyEl.focus();
  }

  if (document.getElementById('btn-bold')) {
    document.getElementById('btn-bold').addEventListener('click', function () {
      insertMarkdown('**', '**');
    });
  }

  if (document.getElementById('btn-italic')) {
    document.getElementById('btn-italic').addEventListener('click', function () {
      insertMarkdown('*', '*');
    });
  }

  if (document.getElementById('btn-heading')) {
    document.getElementById('btn-heading').addEventListener('click', function () {
      if (!bodyEl) return;
      var start = bodyEl.selectionStart;
      var text = bodyEl.value;
      /* Find start of current line */
      var lineStart = text.lastIndexOf('\n', start - 1) + 1;
      var lineEnd = text.indexOf('\n', start);
      if (lineEnd === -1) lineEnd = text.length;
      var line = text.substring(lineStart, lineEnd);

      if (line.indexOf('# ') === 0) {
        /* Already has heading — remove it */
        bodyEl.value = text.substring(0, lineStart) + line.substring(2) + text.substring(lineEnd);
        bodyEl.selectionStart = start - 2;
        bodyEl.selectionEnd = start - 2;
      } else {
        /* Add heading prefix */
        bodyEl.value = text.substring(0, lineStart) + '# ' + text.substring(lineStart);
        bodyEl.selectionStart = start + 2;
        bodyEl.selectionEnd = start + 2;
      }
      bodyEl.focus();
    });
  }

  /* ── 클립보드: 텍스트 선택 후 복사 (마우스업 / 터치엔드) ── */
  (function () {
    function copySelectedText() {
      if (!bodyEl) return;
      var start = bodyEl.selectionStart;
      var end = bodyEl.selectionEnd;
      if (start === end) return; /* 선택된 텍스트 없음 */
      var selectedText = bodyEl.value.substring(start, end);
      if (!selectedText) return;
      if (typeof ZylBridge !== 'undefined') {
        ZylBridge.requestService('clipboard', 'copy', { text: selectedText }).catch(function () { /* ignore */ });
      }
    }

    if (bodyEl) {
      bodyEl.addEventListener('mouseup', function () {
        copySelectedText();
      });
      bodyEl.addEventListener('touchend', function () {
        /* 터치 환경: 약간의 딜레이 후 선택 확인 */
        setTimeout(copySelectedText, 100);
      });
    }
  })();

  /* NOTE: getFileContent handler merged into single message listener above (duplicate removed) */

  /* Notes 디렉토리 생성 보장 */
  ZylBridge.sendToSystem({
    type: 'service.request', service: 'fs', method: 'mkdir', params: { path: 'Documents/Notes' }
  });
  requestNotes();
})();
