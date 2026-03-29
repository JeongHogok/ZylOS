// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 메모 앱 — 텍스트 노트 생성/편집/삭제 (FS 저장)
// 수행범위: 노트 목록, 편집기, fs 서비스 연동
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
  var notes = [];
  var currentNote = null;

  function requestNotes() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'fs', method: 'getDirectory', params: { path: 'Documents/Notes' }
    }), '*');
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;
      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) { renderNotes(msg.data); }
    } catch (err) {}
  });

  function renderNotes(entries) {
    if (!list) return;
    list.innerHTML = '';
    notes = (entries || []).filter(function (e) { return (e.name || '').indexOf('.txt') !== -1; });
    if (notes.length === 0) { list.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px">No notes yet</div>'; return; }
    notes.forEach(function (n) {
      var el = document.createElement('div'); el.className = 'note-item';
      el.textContent = n.name.replace('.txt', '');
      el.addEventListener('click', function () { openNote(n.name); });
      list.appendChild(el);
    });
  }

  function openNote(filename) {
    currentNote = filename;
    if (titleEl) titleEl.value = filename.replace('.txt', '');
    if (bodyEl) bodyEl.value = '';
    if (list) list.classList.add('hidden');
    if (editor) editor.classList.remove('hidden');
    if (document.getElementById('header')) document.getElementById('header').classList.add('hidden');
    /* 파일 내용 요청 */
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'fs', method: 'getFileContent', params: { path: 'Documents/Notes/' + filename }
    }), '*');
  }

  function closeEditor() {
    if (list) list.classList.remove('hidden');
    if (editor) editor.classList.add('hidden');
    if (document.getElementById('header')) document.getElementById('header').classList.remove('hidden');
    currentNote = null;
    requestNotes();
  }

  if (document.getElementById('btn-back')) document.getElementById('btn-back').addEventListener('click', closeEditor);

  if (document.getElementById('btn-save')) document.getElementById('btn-save').addEventListener('click', function () {
    var title = titleEl ? titleEl.value.trim() : '';
    var body = bodyEl ? bodyEl.value : '';
    if (!title) return;
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'fs', method: 'writeFile',
      params: { path: 'Documents/Notes/' + title + '.txt', content: body }
    }), '*');
    closeEditor();
  });

  if (document.getElementById('btn-delete')) document.getElementById('btn-delete').addEventListener('click', function () {
    if (!currentNote) return;
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'fs', method: 'remove',
      params: { path: 'Documents/Notes/' + currentNote }
    }), '*');
    closeEditor();
  });

  if (document.getElementById('btn-new')) document.getElementById('btn-new').addEventListener('click', function () {
    currentNote = null;
    if (titleEl) titleEl.value = '';
    if (bodyEl) bodyEl.value = '';
    if (list) list.classList.add('hidden');
    if (editor) editor.classList.remove('hidden');
    if (document.getElementById('header')) document.getElementById('header').classList.add('hidden');
  });

  /* 파일 내용 수신 */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (msg && msg.type === 'service.response' && msg.service === 'fs' && msg.method === 'getFileContent' && msg.data) {
        if (bodyEl) bodyEl.value = msg.data;
      }
    } catch (err) {}
  });

  /* Notes 디렉토리 생성 보장 */
  window.parent.postMessage(JSON.stringify({
    type: 'service.request', service: 'fs', method: 'mkdir', params: { path: 'Documents/Notes' }
  }), '*');
  requestNotes();
})();
