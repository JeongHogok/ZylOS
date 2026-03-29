// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 음악 앱 — Music/ 디렉토리의 오디오 파일 재생
// 수행범위: 트랙 목록 로드, 바이너리 재생, 셔플/반복/볼륨/시크 제어
// 의존방향: fs 서비스 (postMessage IPC)
// SOLID: SRP — 음악 재생 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── Audio Extensions ─── */
  var AUDIO_EXT = ['.mp3', '.ogg', '.wav', '.flac', '.aac', '.m4a', '.opus', '.wma'];

  function isAudio(name) {
    var lower = (name || '').toLowerCase();
    return AUDIO_EXT.some(function (ext) { return lower.indexOf(ext) !== -1; });
  }

  function audioMime(name) {
    var lower = (name || '').toLowerCase();
    if (lower.indexOf('.mp3') !== -1) return 'audio/mpeg';
    if (lower.indexOf('.ogg') !== -1 || lower.indexOf('.opus') !== -1) return 'audio/ogg';
    if (lower.indexOf('.wav') !== -1) return 'audio/wav';
    if (lower.indexOf('.flac') !== -1) return 'audio/flac';
    if (lower.indexOf('.aac') !== -1) return 'audio/aac';
    if (lower.indexOf('.m4a') !== -1) return 'audio/mp4';
    if (lower.indexOf('.wma') !== -1) return 'audio/x-ms-wma';
    return 'audio/mpeg';
  }

  /* ─── DOM Elements ─── */
  var trackListEl      = document.getElementById('track-list');
  var btnPlay          = document.getElementById('btn-play');
  var btnPrev          = document.getElementById('btn-prev');
  var btnNext          = document.getElementById('btn-next');
  var btnShuffle       = document.getElementById('btn-shuffle');
  var btnRepeat        = document.getElementById('btn-repeat');
  var iconPlay         = document.getElementById('icon-play');
  var iconPause        = document.getElementById('icon-pause');
  var progressSeek     = document.getElementById('progress-seek');
  var progressFill     = document.getElementById('progress-fill');
  var timeCurrent      = document.getElementById('time-current');
  var timeDuration     = document.getElementById('time-duration');
  var volumeSlider     = document.getElementById('volume-slider');
  var nowPlayingTitle  = document.getElementById('now-playing-title');
  var nowPlayingArtist = document.getElementById('now-playing-artist');

  /* ─── State ─── */
  var tracks       = [];      // array of {name, path}
  var currentIndex = -1;
  var audio        = null;    // HTMLAudioElement
  var isPlaying    = false;
  var shuffleMode  = false;
  var repeatMode   = 0;       // 0=off, 1=all, 2=one
  var isSeeking    = false;
  var pendingLoad  = null;    // track path waiting for binary data

  /* ─── IPC ─── */
  function requestService(service, method, params) {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    }), '*');
  }

  function requestTrackList() {
    requestService('fs', 'getDirectory', { path: 'Music' });
  }

  function requestTrackBinary(trackPath) {
    pendingLoad = trackPath;
    requestService('fs', 'readBinary', { path: trackPath });
  }

  /* ─── Message Handler ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;

      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        handleDirectoryResponse(msg.data);
      }

      if (msg.service === 'fs' && msg.method === 'readBinary' && msg.data && msg.params) {
        handleBinaryResponse(msg.params, msg.data);
      }
    } catch (err) {
      console.error('[Music] message parse error:', err);
    }
  });

  /* ─── Directory Response ─── */
  function handleDirectoryResponse(entries) {
    tracks = [];
    var list = entries || [];
    list.forEach(function (entry) {
      var name = entry.name || entry;
      if (isAudio(name)) {
        tracks.push({ name: name, path: 'Music/' + name });
      }
    });
    renderTrackList();
  }

  /* ─── Binary Response ─── */
  function handleBinaryResponse(params, base64) {
    if (!params || !params.path) return;
    // Only handle if this is the track we requested
    if (pendingLoad && params.path === pendingLoad) {
      pendingLoad = null;
      createAudioAndPlay(params.path, base64);
    }
  }

  /* ─── Create Audio from Base64 ─── */
  function createAudioAndPlay(trackPath, base64) {
    // Stop current audio
    if (audio) {
      audio.pause();
      audio.removeAttribute('src');
      audio = null;
    }

    var name = trackPath.replace('Music/', '');
    var mime = audioMime(name);
    var dataUrl = 'data:' + mime + ';base64,' + base64;

    audio = new Audio();
    audio.volume = (volumeSlider ? parseInt(volumeSlider.value, 10) : 80) / 100;
    audio.src = dataUrl;

    // Events
    audio.addEventListener('timeupdate', onTimeUpdate);
    audio.addEventListener('loadedmetadata', onMetadataLoaded);
    audio.addEventListener('ended', onTrackEnded);
    audio.addEventListener('play', function () {
      isPlaying = true;
      updatePlayButton();
    });
    audio.addEventListener('pause', function () {
      isPlaying = false;
      updatePlayButton();
    });
    audio.addEventListener('error', function (err) {
      console.error('[Music] Audio error:', err);
      isPlaying = false;
      updatePlayButton();
    });

    audio.play();
    isPlaying = true;
    updatePlayButton();
    updateNowPlaying(name);
    highlightCurrentTrack();
  }

  /* ─── Time Formatting ─── */
  function formatTime(seconds) {
    if (!seconds || isNaN(seconds) || !isFinite(seconds)) return '0:00';
    var m = Math.floor(seconds / 60);
    var s = Math.floor(seconds % 60);
    return m + ':' + (s < 10 ? '0' : '') + s;
  }

  /* ─── Audio Events ─── */
  function onTimeUpdate() {
    if (!audio || isSeeking) return;
    var current = audio.currentTime || 0;
    var duration = audio.duration || 0;
    if (timeCurrent) timeCurrent.textContent = formatTime(current);
    if (duration > 0) {
      var pct = (current / duration) * 1000;
      if (progressSeek) progressSeek.value = Math.floor(pct);
      if (progressFill) progressFill.style.width = (pct / 10) + '%';
    }
  }

  function onMetadataLoaded() {
    if (!audio) return;
    if (timeDuration) timeDuration.textContent = formatTime(audio.duration);
    if (timeCurrent) timeCurrent.textContent = '0:00';
  }

  function onTrackEnded() {
    if (repeatMode === 2) {
      // Repeat one
      if (audio) {
        audio.currentTime = 0;
        audio.play();
      }
      return;
    }
    // Auto-advance
    playNextTrack(false);
  }

  /* ─── Now Playing Display ─── */
  function updateNowPlaying(name) {
    // Strip extension for display
    var displayName = name.replace(/\.[^.]+$/, '');
    if (nowPlayingTitle) nowPlayingTitle.textContent = displayName;
    if (nowPlayingArtist) nowPlayingArtist.textContent = '';
  }

  function updatePlayButton() {
    if (iconPlay) iconPlay.classList.toggle('hidden', isPlaying);
    if (iconPause) iconPause.classList.toggle('hidden', !isPlaying);
  }

  /* ─── Track List Rendering ─── */
  function renderTrackList() {
    if (!trackListEl) return;
    trackListEl.innerHTML = '';

    if (tracks.length === 0) {
      var empty = document.createElement('div');
      empty.className = 'empty-state';
      empty.setAttribute('data-i18n', 'music.noTracks');
      empty.textContent = 'No music files found in Music/';
      trackListEl.appendChild(empty);
      return;
    }

    tracks.forEach(function (track, index) {
      var el = document.createElement('div');
      el.className = 'track-item';
      el.dataset.index = index;

      var nameSpan = document.createElement('span');
      nameSpan.className = 'track-name';
      nameSpan.textContent = track.name.replace(/\.[^.]+$/, '');

      var extSpan = document.createElement('span');
      extSpan.className = 'track-ext';
      var extMatch = track.name.match(/(\.[^.]+)$/);
      extSpan.textContent = extMatch ? extMatch[1].toUpperCase() : '';

      el.appendChild(nameSpan);
      el.appendChild(extSpan);

      el.addEventListener('click', function () {
        loadAndPlayTrack(index);
      });

      trackListEl.appendChild(el);
    });

    highlightCurrentTrack();
  }

  function highlightCurrentTrack() {
    if (!trackListEl) return;
    var items = trackListEl.querySelectorAll('.track-item');
    for (var i = 0; i < items.length; i++) {
      items[i].classList.toggle('active', parseInt(items[i].dataset.index, 10) === currentIndex);
    }
  }

  /* ─── Playback Control ─── */
  function loadAndPlayTrack(index) {
    if (index < 0 || index >= tracks.length) return;
    currentIndex = index;
    var track = tracks[index];
    // Reset progress
    if (progressSeek) progressSeek.value = 0;
    if (progressFill) progressFill.style.width = '0%';
    if (timeCurrent) timeCurrent.textContent = '0:00';
    if (timeDuration) timeDuration.textContent = '0:00';
    updateNowPlaying(track.name);
    highlightCurrentTrack();
    requestTrackBinary(track.path);
  }

  function playNextTrack(userAction) {
    if (tracks.length === 0) return;

    if (shuffleMode) {
      var nextIdx = Math.floor(Math.random() * tracks.length);
      // Avoid same track if possible
      if (tracks.length > 1) {
        while (nextIdx === currentIndex) {
          nextIdx = Math.floor(Math.random() * tracks.length);
        }
      }
      loadAndPlayTrack(nextIdx);
      return;
    }

    var next = currentIndex + 1;
    if (next >= tracks.length) {
      if (repeatMode === 1) {
        next = 0; // Repeat all: wrap around
      } else if (!userAction) {
        // End of playlist, stop
        isPlaying = false;
        updatePlayButton();
        return;
      } else {
        next = 0;
      }
    }
    loadAndPlayTrack(next);
  }

  function playPrevTrack() {
    if (tracks.length === 0) return;

    // If more than 3 seconds in, restart current track
    if (audio && audio.currentTime > 3) {
      audio.currentTime = 0;
      return;
    }

    var prev = currentIndex - 1;
    if (prev < 0) {
      prev = repeatMode === 1 ? tracks.length - 1 : 0;
    }
    loadAndPlayTrack(prev);
  }

  /* ─── Button Handlers ─── */
  if (btnPlay) {
    btnPlay.addEventListener('click', function () {
      if (!audio && tracks.length > 0) {
        // Nothing loaded yet, play first track
        loadAndPlayTrack(0);
        return;
      }
      if (!audio) return;
      if (isPlaying) {
        audio.pause();
      } else {
        audio.play();
      }
    });
  }

  if (btnNext) {
    btnNext.addEventListener('click', function () {
      playNextTrack(true);
    });
  }

  if (btnPrev) {
    btnPrev.addEventListener('click', function () {
      playPrevTrack();
    });
  }

  if (btnShuffle) {
    btnShuffle.addEventListener('click', function () {
      shuffleMode = !shuffleMode;
      btnShuffle.classList.toggle('ctrl-active', shuffleMode);
    });
  }

  if (btnRepeat) {
    btnRepeat.addEventListener('click', function () {
      repeatMode = (repeatMode + 1) % 3;
      btnRepeat.classList.toggle('ctrl-active', repeatMode > 0);
      // Visual indicator for repeat-one
      if (repeatMode === 2) {
        btnRepeat.classList.add('ctrl-repeat-one');
      } else {
        btnRepeat.classList.remove('ctrl-repeat-one');
      }
    });
  }

  /* ─── Progress Bar Seek ─── */
  if (progressSeek) {
    progressSeek.addEventListener('input', function () {
      isSeeking = true;
      var val = parseInt(progressSeek.value, 10) / 1000;
      if (audio && audio.duration) {
        var seekTime = val * audio.duration;
        if (timeCurrent) timeCurrent.textContent = formatTime(seekTime);
        if (progressFill) progressFill.style.width = (val * 100) + '%';
      }
    });

    progressSeek.addEventListener('change', function () {
      var val = parseInt(progressSeek.value, 10) / 1000;
      if (audio && audio.duration) {
        audio.currentTime = val * audio.duration;
      }
      isSeeking = false;
    });
  }

  /* ─── Volume Control ─── */
  if (volumeSlider) {
    volumeSlider.addEventListener('input', function () {
      var vol = parseInt(volumeSlider.value, 10) / 100;
      if (audio) audio.volume = vol;
    });
  }

  /* ─── Init ─── */
  requestTrackList();
})();
