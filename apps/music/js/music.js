// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 음악 앱 — Music/ 디렉토리의 오디오 파일 재생 + 재생목록 관리
// 수행범위: 트랙 목록 로드, 바이너리 재생, 셔플/반복/볼륨/시크 제어, 재생목록 CRUD
// 의존방향: fs 서비스, settings 서비스 (postMessage IPC)
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
    for (var i = 0; i < AUDIO_EXT.length; i++) {
      if (lower.indexOf(AUDIO_EXT[i]) !== -1) return true;
    }
    return false;
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
  var albumArt         = document.getElementById('album-art');

  /* Playlist DOM */
  var playlistPanel     = document.getElementById('playlist-panel');
  var playlistList      = document.getElementById('playlist-list');
  var playlistEmpty     = document.getElementById('playlist-empty');
  var btnCreatePlaylist = document.getElementById('btn-create-playlist');
  var playlistDetail    = document.getElementById('playlist-detail');
  var playlistDetailName = document.getElementById('playlist-detail-name');
  var playlistDetailTracks = document.getElementById('playlist-detail-tracks');
  var btnPlaylistBack   = document.getElementById('btn-playlist-back');
  var btnPlaylistDelete = document.getElementById('btn-playlist-delete');
  var btnAddToPlaylist  = document.getElementById('btn-add-to-playlist');
  var trackPicker       = document.getElementById('track-picker');
  var trackPickerList   = document.getElementById('track-picker-list');
  var btnPickerBack     = document.getElementById('btn-picker-back');
  var btnPickerDone     = document.getElementById('btn-picker-done');
  var createPlDialog    = document.getElementById('create-pl-dialog');
  var createPlName      = document.getElementById('create-pl-name');
  var btnCreatePlCancel = document.getElementById('btn-create-pl-cancel');
  var btnCreatePlSave   = document.getElementById('btn-create-pl-save');

  /* Tab DOM */
  var musicTabs = document.querySelectorAll('.music-tab');

  /* ─── State ─── */
  var tracks       = [];      // array of {name, path}
  var currentIndex = -1;
  var audio        = null;    // HTMLAudioElement
  var isPlaying    = false;
  var shuffleMode  = false;
  var repeatMode   = 0;       // 0=off, 1=all, 2=one
  var isSeeking    = false;
  var pendingLoad  = null;    // track path waiting for binary data

  /* Playlist state */
  var playlists = []; /* Array of { id, name, trackPaths[] } */
  var currentPlaylistIdx = -1;
  var pickerSelected = []; /* track paths selected in picker */
  var activePanel = 'tracks'; /* tracks | playlists | detail | picker */

  /* ─── IPC ─── */
  function requestService(service, method, params) {
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
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
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        if (activePanel === 'picker') {
          showPanel('detail');
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else if (activePanel === 'detail') {
          showPanel('playlists');
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      if (msg.service === 'fs' && msg.method === 'getDirectory' && msg.data) {
        handleDirectoryResponse(msg.data);
      }

      if (msg.service === 'fs' && msg.method === 'readBinary' && msg.data && msg.params) {
        handleBinaryResponse(msg.params, msg.data);
      }

      if (msg.service === 'audio' && msg.method === 'getVolume' && msg.data != null) {
        var vol = typeof msg.data === 'object' ? msg.data.value : msg.data;
        if (volumeSlider) volumeSlider.value = vol;
        if (audio) audio.volume = vol / 100;
      }

      if (msg.type === 'audio.volumeChanged' && msg.data) {
        var vol2 = msg.data.value;
        if (volumeSlider) volumeSlider.value = vol2;
        if (audio) audio.volume = vol2 / 100;
      }

      /* Settings response for playlists */
      if (msg.service === 'settings' && msg.method === 'get' && msg.params && msg.params.category === 'music') {
        if (msg.data && msg.data.playlists) {
          try {
            var parsed = JSON.parse(msg.data.playlists);
            if (Array.isArray(parsed)) {
              playlists = parsed;
              renderPlaylists();
            }
          } catch (err) { /* ignore */ }
        }
      }
    } catch (err) {
      if (typeof console !== 'undefined') console.error('[Music] message parse error:', err);
    }
  });

  /* ─── Tab switching ─── */
  for (var ti = 0; ti < musicTabs.length; ti++) {
    (function (tab) {
      tab.addEventListener('click', function () {
        var target = tab.dataset.mtab;
        for (var j = 0; j < musicTabs.length; j++) {
          musicTabs[j].classList.toggle('active', musicTabs[j].dataset.mtab === target);
        }
        if (target === 'tracks') showPanel('tracks');
        else if (target === 'playlists') showPanel('playlists');
      });
    })(musicTabs[ti]);
  }

  function showPanel(name) {
    activePanel = name;
    trackListEl.classList.toggle('hidden', name !== 'tracks');
    trackListEl.classList.toggle('active', name === 'tracks');
    playlistPanel.classList.toggle('hidden', name !== 'playlists');
    playlistPanel.classList.toggle('active', name === 'playlists');
    playlistDetail.classList.toggle('hidden', name !== 'detail');
    playlistDetail.classList.toggle('active', name === 'detail');
    trackPicker.classList.toggle('hidden', name !== 'picker');
    trackPicker.classList.toggle('active', name === 'picker');
  }

  /* ─── Directory Response ─── */
  function handleDirectoryResponse(entries) {
    tracks = [];
    var list = entries || [];
    for (var i = 0; i < list.length; i++) {
      var name = list[i].name || list[i];
      if (isAudio(name)) {
        tracks.push({ name: name, path: 'Music/' + name });
      }
    }
    renderTrackList();
  }

  /* ─── Binary Response ─── */
  function handleBinaryResponse(params, base64) {
    if (!params || !params.path) return;
    if (pendingLoad && params.path === pendingLoad) {
      pendingLoad = null;
      createAudioAndPlay(params.path, base64);
    }
  }

  /* ─── Create Audio from Base64 ─── */
  function createAudioAndPlay(trackPath, base64) {
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
      if (typeof console !== 'undefined') console.error('[Music] Audio error:', err);
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
      if (audio) {
        audio.currentTime = 0;
        audio.play();
      }
      return;
    }
    playNextTrack(false);
  }

  /* ─── Now Playing Display ─── */
  function updateNowPlaying(name) {
    var displayName = name.replace(/\.[^.]+$/, '');
    if (nowPlayingTitle) nowPlayingTitle.textContent = displayName;
    if (nowPlayingArtist) nowPlayingArtist.textContent = '';
    /* Update album art background color based on track name hash */
    if (albumArt) {
      var hash = 0;
      for (var c = 0; c < displayName.length; c++) {
        hash = displayName.charCodeAt(c) + ((hash << 5) - hash);
      }
      var hue = Math.abs(hash) % 360;
      albumArt.style.background = 'linear-gradient(135deg, hsl(' + hue + ',40%,25%), hsl(' + ((hue + 60) % 360) + ',40%,20%))';
    }
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
      empty.textContent = (typeof zylI18n !== 'undefined') ? zylI18n.t('music.no_tracks') : 'No music files';
      trackListEl.appendChild(empty);
      return;
    }

    for (var i = 0; i < tracks.length; i++) {
      (function (track, index) {
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
      })(tracks[i], i);
    }

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
        next = 0;
      } else if (!userAction) {
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
      requestService('audio', 'setVolume', { stream: 'media', value: parseInt(volumeSlider.value, 10) });
    });
  }

  /* ═══════════════════════════════════════════════════════════
     Playlist Management
     ═══════════════════════════════════════════════════════════ */

  function savePlaylists() {
    requestService('settings', 'update', {
      category: 'music',
      key: 'playlists',
      value: JSON.stringify(playlists)
    });
  }

  function loadPlaylists() {
    requestService('settings', 'get', { category: 'music' });
  }

  function generatePlId() {
    return Date.now().toString(36) + Math.random().toString(36).substr(2, 5);
  }

  function renderPlaylists() {
    if (!playlistList) return;
    playlistList.innerHTML = '';

    if (playlists.length === 0) {
      if (playlistEmpty) playlistEmpty.classList.remove('hidden');
      return;
    }
    if (playlistEmpty) playlistEmpty.classList.add('hidden');

    for (var i = 0; i < playlists.length; i++) {
      (function (pl, idx) {
        var el = document.createElement('div');
        el.className = 'playlist-item';

        var nameEl = document.createElement('span');
        nameEl.className = 'playlist-name';
        nameEl.textContent = pl.name;

        var countEl = document.createElement('span');
        countEl.className = 'playlist-count';
        countEl.textContent = (pl.trackPaths ? pl.trackPaths.length : 0) + ' ' + ((typeof zylI18n !== 'undefined') ? zylI18n.t('music.tracks_count') : 'tracks');

        el.appendChild(nameEl);
        el.appendChild(countEl);

        el.addEventListener('click', function () {
          openPlaylistDetail(idx);
        });

        playlistList.appendChild(el);
      })(playlists[i], i);
    }
  }

  /* ── Create playlist dialog ── */
  if (btnCreatePlaylist) {
    btnCreatePlaylist.addEventListener('click', function () {
      if (createPlDialog) createPlDialog.classList.remove('hidden');
      if (createPlName) { createPlName.value = ''; createPlName.focus(); }
    });
  }

  if (btnCreatePlCancel) {
    btnCreatePlCancel.addEventListener('click', function () {
      if (createPlDialog) createPlDialog.classList.add('hidden');
    });
  }

  if (btnCreatePlSave) {
    btnCreatePlSave.addEventListener('click', function () {
      var name = (createPlName ? createPlName.value.trim() : '');
      if (!name) return;
      playlists.push({ id: generatePlId(), name: name, trackPaths: [] });
      savePlaylists();
      renderPlaylists();
      if (createPlDialog) createPlDialog.classList.add('hidden');
    });
  }

  /* ── Playlist detail ── */
  function openPlaylistDetail(idx) {
    currentPlaylistIdx = idx;
    var pl = playlists[idx];
    if (!pl) return;
    if (playlistDetailName) playlistDetailName.textContent = pl.name;
    renderPlaylistTracks(pl);
    showPanel('detail');
  }

  function renderPlaylistTracks(pl) {
    if (!playlistDetailTracks) return;
    playlistDetailTracks.innerHTML = '';
    if (!pl.trackPaths || pl.trackPaths.length === 0) {
      var empty = document.createElement('div');
      empty.className = 'empty-state';
      empty.textContent = (typeof zylI18n !== 'undefined') ? zylI18n.t('music.no_tracks') : 'No music files';
      playlistDetailTracks.appendChild(empty);
      return;
    }

    for (var i = 0; i < pl.trackPaths.length; i++) {
      (function (trackPath, tidx) {
        var el = document.createElement('div');
        el.className = 'track-item';

        var name = trackPath.replace('Music/', '').replace(/\.[^.]+$/, '');
        var nameSpan = document.createElement('span');
        nameSpan.className = 'track-name';
        nameSpan.textContent = name;

        var delBtn = document.createElement('button');
        delBtn.className = 'pl-track-remove';
        delBtn.textContent = '\u00D7';
        delBtn.addEventListener('click', function (e) {
          e.stopPropagation();
          pl.trackPaths.splice(tidx, 1);
          savePlaylists();
          renderPlaylistTracks(pl);
        });

        el.appendChild(nameSpan);
        el.appendChild(delBtn);

        el.addEventListener('click', function () {
          /* Play this track from the main track list */
          for (var t = 0; t < tracks.length; t++) {
            if (tracks[t].path === trackPath) {
              loadAndPlayTrack(t);
              break;
            }
          }
        });

        playlistDetailTracks.appendChild(el);
      })(pl.trackPaths[i], i);
    }
  }

  if (btnPlaylistBack) {
    btnPlaylistBack.addEventListener('click', function () {
      showPanel('playlists');
    });
  }

  if (btnPlaylistDelete) {
    btnPlaylistDelete.addEventListener('click', function () {
      if (currentPlaylistIdx >= 0) {
        playlists.splice(currentPlaylistIdx, 1);
        savePlaylists();
        renderPlaylists();
        showPanel('playlists');
      }
    });
  }

  /* ── Add tracks to playlist (picker) ── */
  if (btnAddToPlaylist) {
    btnAddToPlaylist.addEventListener('click', function () {
      pickerSelected = [];
      renderTrackPicker();
      showPanel('picker');
    });
  }

  function renderTrackPicker() {
    if (!trackPickerList) return;
    trackPickerList.innerHTML = '';
    var pl = playlists[currentPlaylistIdx];
    var existing = {};
    if (pl && pl.trackPaths) {
      for (var e = 0; e < pl.trackPaths.length; e++) {
        existing[pl.trackPaths[e]] = true;
      }
    }

    for (var i = 0; i < tracks.length; i++) {
      (function (track) {
        if (existing[track.path]) return; /* Already in playlist */
        var el = document.createElement('div');
        el.className = 'picker-item';

        var cb = document.createElement('div');
        cb.className = 'picker-check';

        var nameSpan = document.createElement('span');
        nameSpan.className = 'track-name';
        nameSpan.textContent = track.name.replace(/\.[^.]+$/, '');

        el.appendChild(cb);
        el.appendChild(nameSpan);

        el.addEventListener('click', function () {
          var idx = pickerSelected.indexOf(track.path);
          if (idx === -1) {
            pickerSelected.push(track.path);
            cb.classList.add('checked');
          } else {
            pickerSelected.splice(idx, 1);
            cb.classList.remove('checked');
          }
        });

        trackPickerList.appendChild(el);
      })(tracks[i]);
    }
  }

  if (btnPickerBack) {
    btnPickerBack.addEventListener('click', function () {
      showPanel('detail');
    });
  }

  if (btnPickerDone) {
    btnPickerDone.addEventListener('click', function () {
      var pl = playlists[currentPlaylistIdx];
      if (pl && pickerSelected.length > 0) {
        for (var i = 0; i < pickerSelected.length; i++) {
          pl.trackPaths.push(pickerSelected[i]);
        }
        savePlaylists();
        renderPlaylistTracks(pl);
      }
      showPanel('detail');
    });
  }

  /* ─── Init ─── */
  requestTrackList();
  requestService('audio', 'getVolume', { stream: 'media' });
  loadPlaylists();
})();
