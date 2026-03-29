// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Widget
//
// 역할: 부팅 스플래시 애니메이션 및 리소스 예약 진행률 표시
// 수행범위: 로고 표시 → 프로그레스 바 → 커널 부팅 로그 → 에뮬레이터 전환
// 의존방향: Tauri invoke (boot_device), config-ui.js (config 수신)
// SOLID: SRP — 부팅 시퀀스 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

/* global ZylBootSequence */
/* eslint-disable no-unused-vars */

var ZylBootSequence = (function () {
  'use strict';

  var _invoke = null;
  var _onComplete = null;

  function getInvoke() {
    if (_invoke) return _invoke;
    if (typeof window !== 'undefined' && window.__TAURI__ && window.__TAURI__.core) {
      _invoke = window.__TAURI__.core.invoke;
    }
    return _invoke;
  }

  function invoke(cmd, args) {
    var fn = getInvoke();
    if (!fn) return Promise.resolve(null);
    return fn(cmd, args || {});
  }

  var bootMessages = [
    { text: '[    0.000] Linux 6.6.0-spacemit (riscv64)', type: 'info' },
    { text: '[    0.012] CPU: SpacemiT K1 X60 x8 @ 1.6GHz', type: 'info' },
    { text: '[    0.045] Memory: {RAM} available', type: 'info' },
    { text: '[    0.089] DRM: img-gpu initialized', type: 'ok' },
    { text: '[    0.120] Mounting rootfs...', type: 'info' },
    { text: '[    0.234] systemd[1]: Starting Zyl OS...', type: 'info' },
    { text: '[    0.456] zyl-compositor: Wayland display ready', type: 'ok' },
    { text: '[    0.567] zyl-wam: WebAppManager started', type: 'ok' },
    { text: '[    0.678] zyl-notification: D-Bus registered', type: 'ok' },
    { text: '[    0.789] zyl-power: PowerManager active', type: 'ok' },
    { text: '[    0.890] zyl-sensors: 5 sensors detected', type: 'ok' },
    { text: '[    0.950] Storage: {STORAGE} disk mounted', type: 'ok' },
    { text: '[    1.000] Zyl OS v{VERSION} ready', type: 'ok' }
  ];

  function start(config, onComplete) {
    _onComplete = onComplete;

    // 화면 전환
    document.getElementById('config-screen').classList.add('hidden');
    document.getElementById('boot-screen').classList.remove('hidden');

    // 버전 표시
    var versionEl = document.getElementById('boot-version');
    if (versionEl) versionEl.textContent = 'v' + (config.os_version || '0.1.0');

    // 프로그레스 + 로그
    var progressBar = document.getElementById('boot-progress-bar');
    var logContainer = document.getElementById('boot-log');
    if (logContainer) logContainer.innerHTML = '';

    var ramLabel = config.ram_mb >= 1024 ? (config.ram_mb / 1024) + ' GB' : config.ram_mb + ' MB';
    var storageLabel = config.storage_gb + ' GB';
    var version = config.os_version || '0.1.0';

    // Tauri 리소스 예약 (병렬)
    var resourceReady = false;
    var bootInfo = null;

    invoke('boot_device', { config: config }).then(function (info) {
      bootInfo = info;
      resourceReady = true;
    }).catch(function (err) {
      // 리소스 예약 실패해도 에뮬레이터는 실행 (폴백 모드)
      resourceReady = true;
      bootInfo = {
        profile_id: config.profile_id,
        mount_point: '',
        ram_limit_mb: config.ram_mb,
        nav_mode: String(config.nav_mode).toLowerCase(),
        screen_width: config.screen_width,
        screen_height: config.screen_height
      };
      addLogLine('[WARN] Resource reservation failed: ' + (err || 'unknown'), 'warn', logContainer);
    });

    // 부팅 메시지 순차 표시
    var totalMessages = bootMessages.length;
    var currentIdx = 0;

    function showNextMessage() {
      if (currentIdx >= totalMessages) {
        // 리소스 준비 대기
        waitForResources();
        return;
      }

      var msg = bootMessages[currentIdx];
      var text = msg.text
        .replace('{RAM}', ramLabel)
        .replace('{STORAGE}', storageLabel)
        .replace('{VERSION}', version);

      addLogLine(text, msg.type, logContainer);

      var progress = Math.round(((currentIdx + 1) / totalMessages) * 90);
      if (progressBar) progressBar.style.width = progress + '%';

      currentIdx++;
      setTimeout(showNextMessage, 150 + Math.random() * 200);
    }

    function waitForResources() {
      if (resourceReady) {
        finishBoot();
        return;
      }
      addLogLine('[    1.100] Waiting for resources...', 'info', logContainer);
      var waitCount = 0;
      var maxWaits = 50; /* 10초 타임아웃 */
      var waitInterval = setInterval(function () {
        waitCount++;
        if (resourceReady || waitCount >= maxWaits) {
          clearInterval(waitInterval);
          if (!resourceReady) {
            addLogLine('[    1.150] Resource timeout — proceeding with fallback', 'warn', logContainer);
          }
          finishBoot();
        }
      }, 200);
    }

    function finishBoot() {
      if (progressBar) progressBar.style.width = '100%';
      addLogLine('[    1.200] Boot complete.', 'ok', logContainer);

      setTimeout(function () {
        document.getElementById('boot-screen').classList.add('hidden');
        if (_onComplete) {
          _onComplete(config, bootInfo);
        }
      }, 600);
    }

    // 시작
    setTimeout(showNextMessage, 500);
  }

  function addLogLine(text, type, container) {
    if (!container) return;
    var line = document.createElement('div');
    line.className = 'boot-log-line ' + (type || 'info');
    line.textContent = text;
    container.appendChild(line);
    container.scrollTop = container.scrollHeight;
  }

  return {
    start: start
  };
})();
