// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 카메라 앱 메인 로직
// 수행범위: 촬영, 모드 전환, 타이머, 줌, 포커스 인터랙션
// 의존방향: ZylBridge (shared/bridge.js)
// SOLID: SRP — 카메라 UI 인터랙션만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  var state = {
    mode: 'photo', flash: 'off', timer: 0,
    hdr: false, ratio: '4:3', zoom: 1,
    frontCamera: false, recording: false
  };

  var viewfinder   = document.getElementById('viewfinder');
  var focusRing    = document.getElementById('focus-ring');
  var btnCapture   = document.getElementById('btn-capture');
  var btnFlash     = document.getElementById('btn-flash');
  var btnTimer     = document.getElementById('btn-timer');
  var btnHdr       = document.getElementById('btn-hdr');
  var btnRatio     = document.getElementById('btn-ratio');
  var timerOverlay = document.getElementById('timer-overlay');
  var timerCount   = document.getElementById('timer-count');
  var captureFlash = document.getElementById('capture-flash');
  var photoPreview = document.getElementById('photo-preview');
  var cameraVideo  = document.getElementById('camera-video');
  var captureCanvas = document.getElementById('capture-canvas');
  var _stream      = null;

  /* ─── Camera Stream ─── */
  function startCamera() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) return;
    var constraints = {
      video: { facingMode: state.frontCamera ? 'user' : 'environment', width: { ideal: 1920 }, height: { ideal: 1080 } },
      audio: false
    };
    navigator.mediaDevices.getUserMedia(constraints).then(function (stream) {
      _stream = stream;
      if (cameraVideo) {
        cameraVideo.srcObject = stream;
        var gradient = document.getElementById('viewfinder-gradient');
        if (gradient) gradient.style.display = 'none';
      }
    }).catch(function (err) {
      var msg = err.name === 'NotAllowedError' ? 'Camera permission denied' : 'Camera unavailable';
      if (viewfinder) {
        var overlay = document.createElement('div');
        overlay.style.cssText = 'position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#aaa;font-size:14px;text-align:center;padding:20px;z-index:5';
        overlay.textContent = msg;
        viewfinder.appendChild(overlay);
      }
    });
  }

  function stopCamera() {
    if (_stream) {
      _stream.getTracks().forEach(function (t) { t.stop(); });
      _stream = null;
    }
  }

  startCamera();

  /* ─── Focus on tap ─── */
  viewfinder.addEventListener('click', function (e) {
    var rect = viewfinder.getBoundingClientRect();
    focusRing.style.left = (e.clientX - rect.left - 35) + 'px';
    focusRing.style.top = (e.clientY - rect.top - 35) + 'px';
    focusRing.classList.remove('hidden', 'focusing');
    void focusRing.offsetWidth;
    focusRing.classList.add('focusing');
    setTimeout(function () { focusRing.classList.add('hidden'); }, 1500);
  });

  /* ─── Video Recording (MediaRecorder) ─── */
  var mediaRecorder = null;
  var recordedChunks = [];

  function startVideoRecording() {
    if (!currentStream) return;
    recordedChunks = [];
    try {
      mediaRecorder = new MediaRecorder(currentStream, { mimeType: 'video/webm' });
    } catch (e) {
      try {
        mediaRecorder = new MediaRecorder(currentStream);
      } catch (e2) { return; }
    }
    mediaRecorder.ondataavailable = function (e) {
      if (e.data && e.data.size > 0) recordedChunks.push(e.data);
    };
    mediaRecorder.onstop = function () {
      var blob = new Blob(recordedChunks, { type: 'video/webm' });
      var reader = new FileReader();
      reader.onloadend = function () {
        var base64 = reader.result.split(',')[1];
        var filename = 'VID_' + new Date().toISOString().replace(/[:.]/g, '-') + '.webm';
        window.parent.postMessage(JSON.stringify({
          type: 'service.request',
          service: 'fs',
          method: 'writeFile',
          params: { path: 'Pictures/' + filename, content: base64 }
        }), '*');
      };
      reader.readAsDataURL(blob);
    };
    mediaRecorder.start(100);
    state.recording = true;
    document.body.classList.add('recording');
  }

  function stopVideoRecording() {
    if (mediaRecorder && mediaRecorder.state !== 'inactive') {
      mediaRecorder.stop();
    }
    state.recording = false;
    document.body.classList.remove('recording');
  }

  /* ─── Capture ─── */
  btnCapture.addEventListener('click', function () {
    if (state.mode === 'video') {
      if (state.recording) {
        stopVideoRecording();
      } else {
        startVideoRecording();
      }
    } else if (state.timer > 0) {
      startTimer(state.timer, doCapture);
    } else {
      doCapture();
    }
  });

  function doCapture() {
    captureFlash.classList.remove('hidden');
    setTimeout(function () { captureFlash.classList.add('hidden'); }, 300);

    /* 실제 프레임 캡처 */
    if (cameraVideo && captureCanvas && cameraVideo.videoWidth > 0) {
      captureCanvas.width = cameraVideo.videoWidth;
      captureCanvas.height = cameraVideo.videoHeight;
      var ctx = captureCanvas.getContext('2d');
      ctx.drawImage(cameraVideo, 0, 0);
      var previewImg = document.getElementById('preview-image');
      if (previewImg) {
        previewImg.style.backgroundImage = 'url(' + captureCanvas.toDataURL('image/jpeg', 0.9) + ')';
        previewImg.style.backgroundSize = 'cover';
      }
    }

    setTimeout(function () { photoPreview.classList.remove('hidden'); }, 400);
  }

  function startTimer(sec, cb) {
    timerOverlay.classList.remove('hidden');
    var r = sec;
    (function tick() {
      timerCount.textContent = r;
      timerCount.style.animation = 'none';
      void timerCount.offsetWidth;
      timerCount.style.animation = 'timer-pop 1s ease-out';
      if (r <= 0) { timerOverlay.classList.add('hidden'); cb(); return; }
      r--; setTimeout(tick, 1000);
    })();
  }

  /* ─── Flash ─── */
  var flashModes = ['off', 'on', 'auto'];
  btnFlash.addEventListener('click', function () {
    var i = flashModes.indexOf(state.flash);
    state.flash = flashModes[(i + 1) % 3];
    btnFlash.querySelector('.btn-label').textContent = state.flash === 'off' ? zylI18n.t('camera.flash_off') : state.flash === 'on' ? zylI18n.t('camera.flash_on') : zylI18n.t('camera.flash_auto');
    btnFlash.classList.toggle('active', state.flash !== 'off');
  });

  /* ─── Timer toggle ─── */
  var timerModes = [0, 3, 10];
  btnTimer.addEventListener('click', function () {
    var i = timerModes.indexOf(state.timer);
    state.timer = timerModes[(i + 1) % 3];
    btnTimer.querySelector('.btn-label').textContent = state.timer === 0 ? zylI18n.t('camera.timer_off') : state.timer + 's';
    btnTimer.classList.toggle('active', state.timer > 0);
  });

  /* ─── HDR / Ratio ─── */
  btnHdr.addEventListener('click', function () {
    state.hdr = !state.hdr;
    btnHdr.classList.toggle('active', state.hdr);
  });

  var ratios = ['4:3', '16:9', '1:1'];
  btnRatio.addEventListener('click', function () {
    var i = ratios.indexOf(state.ratio);
    state.ratio = ratios[(i + 1) % 3];
    btnRatio.querySelector('.ratio-text').textContent = state.ratio;
  });

  /* ─── Mode selector ─── */
  document.querySelectorAll('.mode-btn').forEach(function (btn) {
    btn.addEventListener('click', function () {
      document.querySelectorAll('.mode-btn').forEach(function (b) { b.classList.remove('active'); });
      btn.classList.add('active');
      state.mode = btn.dataset.mode;
      document.body.classList.toggle('video-mode', state.mode === 'video');
      document.body.classList.remove('recording');
      state.recording = false;
    });
  });

  /* ─── Zoom — apply CSS transform to video element ─── */
  document.querySelectorAll('.zoom-btn').forEach(function (btn) {
    btn.addEventListener('click', function () {
      document.querySelectorAll('.zoom-btn').forEach(function (b) { b.classList.remove('active'); });
      btn.classList.add('active');
      state.zoom = parseFloat(btn.dataset.zoom) || 1;
      if (cameraVideo) {
        cameraVideo.style.transform = 'scale(' + state.zoom + ')';
        cameraVideo.style.transformOrigin = 'center center';
      }
    });
  });

  /* ─── Switch / Gallery ─── */
  document.getElementById('btn-switch').addEventListener('click', function () {
    state.frontCamera = !state.frontCamera;
    stopCamera();
    startCamera();
  });
  document.getElementById('btn-gallery').addEventListener('click', function () {
    if (typeof ZylBridge !== 'undefined') ZylBridge.launch('com.zylos.gallery');
  });

  /* ─── Preview actions ─── */
  document.getElementById('btn-retake').addEventListener('click', function () {
    photoPreview.classList.add('hidden');
  });
  document.getElementById('btn-save').addEventListener('click', function () {
    /* 캡처된 이미지를 OS 파일시스템의 Pictures/에 저장 (postMessage IPC) */
    if (captureCanvas && captureCanvas.width > 0) {
      var dataUrl = captureCanvas.toDataURL('image/jpeg', 0.9);
      var base64 = dataUrl.split(',')[1] || '';
      var filename = 'IMG_' + new Date().toISOString().replace(/[:\-T]/g, '').slice(0, 14) + '.jpg';
      var path = 'Pictures/' + filename;

      /* 에뮬레이터 서비스를 통해 OS 파일시스템에 저장 */
      window.parent.postMessage(JSON.stringify({
        type: 'service.request',
        service: 'fs',
        method: 'writeFile',
        params: { path: path, content: base64 }
      }), '*');
      showNotice('Saved: ' + filename);
    }
    photoPreview.classList.add('hidden');
  });

  function showNotice(msg) {
    var el = document.createElement('div');
    el.style.cssText = 'position:fixed;bottom:200px;left:50%;transform:translateX(-50%);background:rgba(0,0,0,0.8);color:#fff;padding:10px 20px;border-radius:8px;font-size:13px;z-index:999';
    el.textContent = msg;
    document.body.appendChild(el);
    setTimeout(function () { el.remove(); }, 2000);
  }

  /* ─── Message Handler ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        if (photoPreview && !photoPreview.classList.contains('hidden')) {
          photoPreview.classList.add('hidden');
          window.parent.postMessage(JSON.stringify({ type: 'navigation.handled' }), '*');
        } else {
          window.parent.postMessage(JSON.stringify({ type: 'navigation.exit' }), '*');
        }
        return;
      }
    } catch (err) { /* ignore */ }
  });
})();
