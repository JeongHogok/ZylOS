// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 카메라 앱 메인 로직
// 수행범위: 촬영, 모드 전환, 타이머, 줌, 포커스 인터랙션
// 의존방향: ZylBridge (shared/bridge.js)
// SOLID: SRP — 카메라 UI 인터랙션만 담당
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
      if (cameraVideo) { cameraVideo.srcObject = stream; }
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

  /* ─── Capture ─── */
  btnCapture.addEventListener('click', function () {
    if (state.mode === 'video') {
      state.recording = !state.recording;
      document.body.classList.toggle('recording', state.recording);
      document.body.classList.toggle('video-mode', true);
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
    btnFlash.querySelector('.btn-label').textContent = state.flash === 'off' ? 'Off' : state.flash === 'on' ? 'On' : 'Auto';
    btnFlash.classList.toggle('active', state.flash !== 'off');
  });

  /* ─── Timer toggle ─── */
  var timerModes = [0, 3, 10];
  btnTimer.addEventListener('click', function () {
    var i = timerModes.indexOf(state.timer);
    state.timer = timerModes[(i + 1) % 3];
    btnTimer.querySelector('.btn-label').textContent = state.timer === 0 ? 'Off' : state.timer + 's';
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

  /* ─── Zoom ─── */
  document.querySelectorAll('.zoom-btn').forEach(function (btn) {
    btn.addEventListener('click', function () {
      document.querySelectorAll('.zoom-btn').forEach(function (b) { b.classList.remove('active'); });
      btn.classList.add('active');
      state.zoom = parseFloat(btn.dataset.zoom);
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
    photoPreview.classList.add('hidden');
  });
})();
