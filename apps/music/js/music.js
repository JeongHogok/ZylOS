// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 음악 앱 — Music/ 디렉토리의 오디오 파일 재생
// 수행범위: 트랙 목록, 재생 컨트롤
// 의존방향: fs 서비스 (postMessage IPC)
// SOLID: SRP — 음악 재생 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var trackList = document.getElementById('track-list');
  var nowPlaying = document.getElementById('now-playing');
  var btnPlay = document.getElementById('btn-play');
  var playing = false;

  function requestTracks() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'fs', method: 'getDirectory', params: { path: 'Music' }
    }), '*');
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;
      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        renderTracks(msg.data);
      }
    } catch (err) {}
  });

  function renderTracks(entries) {
    if (!trackList) return;
    trackList.innerHTML = '';
    var tracks = (entries || []).filter(function (e) {
      var ext = (e.name || '').toLowerCase();
      return ext.indexOf('.mp3') !== -1 || ext.indexOf('.ogg') !== -1 || ext.indexOf('.wav') !== -1;
    });
    if (tracks.length === 0) {
      trackList.innerHTML = '<div style="text-align:center;opacity:0.5;padding:40px">No music files in Music/</div>';
      return;
    }
    tracks.forEach(function (track) {
      var el = document.createElement('div');
      el.className = 'track-item';
      el.textContent = track.name;
      el.addEventListener('click', function () {
        if (nowPlaying) nowPlaying.textContent = track.name;
      });
      trackList.appendChild(el);
    });
  }

  if (btnPlay) btnPlay.addEventListener('click', function () {
    playing = !playing;
    btnPlay.textContent = playing ? '⏸' : '▶';
  });

  requestTracks();
})();
