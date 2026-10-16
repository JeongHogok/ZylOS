// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 잠금화면 UI — 시계, PIN 입력, 스와이프 잠금해제, 알림 상호작용
// 수행범위: 실시간 스와이프 피드백, PIN 인증, 알림 터치→앱 이동, 잠금 해제는 OS 셸에 알림
// 의존방향: ZylClock (clock.js), ZylBridge (bridge.js)
// SOLID: SRP — 잠금화면 UI와 인증 로직만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── XSS-safe HTML escaping ─── */
  function escapeHtml(str) {
    if (!str) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  /* ─── DOM ─── */
  var lockMain    = document.getElementById('lock-main');
  var pinScreen   = document.getElementById('pin-screen');
  var pinDots     = document.querySelectorAll('.pin-dot');
  var pinError    = document.getElementById('pin-error');
  var unlockFlash = document.getElementById('unlock-flash');

  /* ─── State ─── */
  var enteredPin   = '';
  var hasPin       = false;  /* false = swipe-only unlock */
  var pendingAppId = null;
  var lockoutUntil = 0;

  /* The lock service holds the PIN (as a hash) and counts failures — this
     screen only asks. It never learns the PIN, so no message can change it. */
  function applyLockState(r) {
    if (!r) return;
    hasPin = r.hasPin === true;
    if (r.retryAfterMs > 0) lockoutUntil = Date.now() + r.retryAfterMs;
  }
  function refreshLockState() {
    ZylBridge.requestService('lock', 'getState').then(applyLockState, function () {
      hasPin = true;   /* unknown must not mean "no PIN" */
    });
  }
  refreshLockState();
  document.addEventListener('visibilitychange', function () {
    if (!document.hidden) refreshLockState();
  });

  /* ─── 파티클 생성 ─── */
  (function createParticles() {
    var container = document.getElementById('particles');
    for (var i = 0; i < 25; i++) {
      var p = document.createElement('div');
      p.className = 'particle';
      var size = Math.random() * 3 + 1;
      p.style.width = size + 'px';
      p.style.height = size + 'px';
      p.style.left = Math.random() * 100 + '%';
      p.style.top = (Math.random() * 100 + 100) + '%';
      p.style.animationDuration = (Math.random() * 12 + 8) + 's';
      p.style.animationDelay = (Math.random() * 10) + 's';
      container.appendChild(p);
    }
  })();

  /* ─── 시계 ─── */
  var lockTime = document.getElementById('lock-time');
  var lockDate = document.getElementById('lock-date');
  /* clock.js is loaded before this script (index.html), so ZylClock always
     exists. The former fallback drew the process time zone, not the user's. */
  ZylClock.create(lockTime, lockDate, { showDate: true, dateFormat: 'long' });

  /* ════════════════════════════════════════════
   *  스와이프: 실시간 드래그 피드백
   * ════════════════════════════════════════════ */
  var dragState = { active: false, startY: 0, currentY: 0 };
  var SWIPE_THRESHOLD = 120;

  lockMain.addEventListener('mousedown', onDragStart);
  lockMain.addEventListener('touchstart', function (e) {
    onDragStart({ clientY: e.touches[0].clientY });
  }, { passive: true });

  document.addEventListener('mousemove', onDragMove);
  document.addEventListener('touchmove', function (e) {
    onDragMove({ clientY: e.touches[0].clientY });
  }, { passive: true });

  document.addEventListener('mouseup', onDragEnd);
  document.addEventListener('touchend', onDragEnd);

  function onDragStart(e) {
    if (pinScreen.classList.contains('visible')) return;
    dragState.active = true;
    dragState.startY = e.clientY;
    dragState.currentY = e.clientY;
    lockMain.classList.add('dragging');
    lockMain.classList.remove('snapping');
  }

  function onDragMove(e) {
    if (!dragState.active) return;
    dragState.currentY = e.clientY;

    var dy = dragState.startY - dragState.currentY;
    if (dy < 0) dy = 0; /* 아래로는 안 움직임 */

    /* 실시간 피드백: 위로 따라 올라감 + 투명해짐 */
    var progress = Math.min(dy / SWIPE_THRESHOLD, 1);
    var translateY = -dy * 0.6;
    var scale = 1 - progress * 0.05;
    var opacity = 1 - progress * 0.4;

    lockMain.style.transform = 'translateY(' + translateY + 'px) scale(' + scale + ')';
    lockMain.style.opacity = opacity;
  }

  function onDragEnd() {
    if (!dragState.active) return;
    dragState.active = false;
    lockMain.classList.remove('dragging');

    var dy = dragState.startY - dragState.currentY;

    if (dy > SWIPE_THRESHOLD) {
      /* Threshold exceeded → check if PIN is set */
      lockMain.classList.add('sliding-out');
      if (!hasPin) {
        /* No PIN set → swipe-only unlock */
        document.body.classList.add('unlocking');
        unlockFlash.classList.add('flash');
        setTimeout(unlockDevice, 500);
      } else {
        /* PIN set → show PIN screen */
        setTimeout(function () {
          showPinScreen();
        }, 350);
      }
    } else {
      /* 임계값 미달 → 스냅백 (스프링 효과) */
      lockMain.classList.add('snapping');
      lockMain.style.transform = '';
      lockMain.style.opacity = '';
      setTimeout(function () {
        lockMain.classList.remove('snapping');
      }, 400);
    }
  }

  /* ════════════════════════════════════════════
   *  PIN 화면 전환 (슬라이드 업/다운)
   * ════════════════════════════════════════════ */
  function showPinScreen() {
    lockMain.style.display = 'none';
    pinScreen.classList.add('visible');
    enteredPin = '';
    updateDots();
    pinError.classList.remove('show');
  }

  function hidePinScreen() {
    pinScreen.classList.remove('visible');
    pinScreen.classList.add('sliding-down');

    setTimeout(function () {
      pinScreen.classList.remove('sliding-down');
      lockMain.style.display = '';
      lockMain.classList.remove('sliding-out');
      lockMain.style.transform = '';
      lockMain.style.opacity = '';
    }, 400);

    pendingAppId = null;
  }

  /* ════════════════════════════════════════════
   *  PIN 입력
   * ════════════════════════════════════════════ */
  document.querySelectorAll('.num-btn[data-num]').forEach(function (btn) {
    btn.addEventListener('click', function () {
      if (enteredPin.length >= 6) return;
      enteredPin += btn.dataset.num;
      updateDots();

      if (enteredPin.length === 6) {
        setTimeout(verifyPin, 250);
      }
    });
  });

  document.getElementById('btn-delete').addEventListener('click', function () {
    if (enteredPin.length > 0) {
      enteredPin = enteredPin.slice(0, -1);
      updateDots();
      pinError.classList.remove('show');
    }
  });

  document.getElementById('btn-cancel').addEventListener('click', function () {
    hidePinScreen();
  });

  function updateDots() {
    pinDots.forEach(function (dot, i) {
      dot.classList.toggle('filled', i < enteredPin.length);
    });
  }

  /* ════════════════════════════════════════════
   *  PIN 검증 + 잠금해제
   * ════════════════════════════════════════════ */
  var lockoutTimerId = null;

  function startLockoutCountdown() {
    clearLockoutTimer();
    disableNumpad(true);
    updateLockoutDisplay();
    lockoutTimerId = setInterval(function () {
      if (Date.now() >= lockoutUntil) {
        clearLockoutTimer();
        pinError.classList.remove('show');
        disableNumpad(false);
      } else {
        updateLockoutDisplay();
      }
    }, 1000);
  }

  function clearLockoutTimer() {
    if (lockoutTimerId) {
      clearInterval(lockoutTimerId);
      lockoutTimerId = null;
    }
  }

  function updateLockoutDisplay() {
    var remaining = Math.ceil((lockoutUntil - Date.now()) / 1000);
    if (remaining < 0) remaining = 0;
    pinError.textContent = zylI18n.t('lock.try_again', { s: remaining });
    pinError.classList.add('show');
  }

  function disableNumpad(disabled) {
    document.querySelectorAll('.num-btn[data-num]').forEach(function (btn) {
      btn.disabled = disabled;
      btn.style.opacity = disabled ? '0.3' : '';
    });
  }

  function verifyPin() {
    if (Date.now() < lockoutUntil) {
      startLockoutCountdown();
      enteredPin = '';
      updateDots();
      return;
    }
    disableNumpad(true);
    ZylBridge.requestService('lock', 'verifyPin', { pin: enteredPin }).then(function (r) {
      disableNumpad(false);
      if (r && r.ok === true) unlockNow();
      else pinRejected(r);
    }, function () {
      disableNumpad(false);
      pinRejected(null);   /* service unreachable: stay locked */
    });
  }

  function unlockNow() {
    clearLockoutTimer();
    /* Success: zoom-out + flash effect */
    document.body.classList.add('unlocking');
    unlockFlash.classList.add('flash');

    setTimeout(unlockDevice, 500);
  }

  /* Tell the OS the user got in (only this app may); it opens the app of the
     notification the user tapped, or home. This screen then goes away — the
     OS raises it again, fresh, at the next lock. */
  function unlockDevice() {
    ZylBridge.sendToSystem({ type: 'unlock', appId: pendingAppId || '' });
    ZylBridge.closeApp();
  }

  function pinRejected(r) {
    if (r && r.retryAfterMs > 0) {
      lockoutUntil = Date.now() + r.retryAfterMs;
      startLockoutCountdown();     /* shows "try again in N s" */
    } else {
      /* 'storage': the OS could not count the try, so it did not make one */
      pinError.textContent = zylI18n.t(r && r.reason === 'storage' ? 'lock.storage_full' : 'lock.wrong_pin');
      pinError.classList.add('show');
    }
    var dots = document.getElementById('pin-dots');
    dots.classList.add('shake');

    setTimeout(function () {
      dots.classList.remove('shake');
      enteredPin = '';
      updateDots();
    }, 500);
  }

  /* ════════════════════════════════════════════
   *  알림: 터치→PIN→앱 이동 / 스와이프 삭제
   * ════════════════════════════════════════════ */
  var notifList = document.getElementById('lock-notifications');

  /* What the device already holds, then the notification service's own
     events (the WAM on the device, the emulator the same way) */
  ZylBridge.requestService('notification', 'getActive').then(function (list) {
    (list || []).forEach(function (n) { addLockNotification(n); });
  }, function () {});
  ZylBridge.onServiceEvent('notification', 'NotificationPosted', function (d) {
    if (d && d.length >= 4) addLockNotification({ id: d[0], appId: d[1], title: d[2], body: d[3] });
  });
  ZylBridge.onServiceEvent('notification', 'NotificationDismissed', function (id) {
    var el = notifList && notifList.querySelector('.lock-notif[data-id="' + String(id).replace(/\D/g, '') + '"]');
    if (el) el.remove();
  });

  function addLockNotification(n) {
    var el = document.createElement('div');
    el.className = 'lock-notif';
    el.dataset.appId = n.appId || '';
    if (n.id !== undefined) el.dataset.id = String(n.id);
    el.innerHTML =
      '<div class="lock-notif-icon">' + escapeHtml(n.icon || '🔔') + '</div>' +
      '<div class="lock-notif-text">' +
        '<div class="lock-notif-app">' + escapeHtml(n.appName || n.appId || '') + '</div>' +
        /* The content stays off the lock screen — the app and that there is a
           notification, not what it says, until the device is unlocked */
        '<div class="lock-notif-title">' + escapeHtml(zylI18n.t('lock.notif_hidden')) + '</div>' +
      '</div>' +
      '<div class="lock-notif-time">' + escapeHtml(zylI18n.t('time.just_now')) + '</div>';

    el.addEventListener('click', function () {
      if (el.classList.contains('swiping')) return;
      pendingAppId = n.appId || null;
      lockMain.classList.add('sliding-out');
      setTimeout(showPinScreen, 350);
    });

    var sx = 0, swiping = false;
    el.addEventListener('mousedown', function (e) { sx = e.clientX; });
    el.addEventListener('touchstart', function (e) { sx = e.touches[0].clientX; }, { passive: true });
    function onMove(dx) {
      if (Math.abs(dx) > 15) {
        swiping = true; el.classList.add('swiping');
        el.style.transform = 'translateX(' + dx + 'px)';
        el.style.opacity = 1 - Math.abs(dx) / 300;
      }
    }
    el.addEventListener('mousemove', function (e) { onMove(e.clientX - sx); });
    el.addEventListener('touchmove', function (e) { onMove(e.touches[0].clientX - sx); }, { passive: true });
    function endSwipe(dx) {
      if (Math.abs(dx) > 100) {
        el.classList.add('dismissed');
        setTimeout(function () { el.remove(); }, 350);
      } else {
        el.classList.remove('swiping'); el.style.transform = ''; el.style.opacity = '';
      }
      setTimeout(function () { swiping = false; }, 50);
    }
    el.addEventListener('mouseup', function (e) { endSwipe(e.clientX - sx); });
    el.addEventListener('touchend', function (e) { endSwipe(e.changedTouches[0].clientX - sx); });

    notifList.prepend(el);
  }

  /* ════════════════════════════════════════════
   *  긴급전화 + 카메라 바로가기
   * ════════════════════════════════════════════ */
  var btnEmergency = document.getElementById('btn-emergency');
  var btnCamera = document.getElementById('btn-camera');

  if (btnEmergency) {
    btnEmergency.addEventListener('click', function (e) {
      e.stopPropagation();
      /* The keypad, over the lock screen (the OS lets calls through) */
      ZylIntent.startActivity({ targetApp: 'com.zylos.phone', action: ZylIntent.ACTION.DIAL });
    });
  }

  if (btnCamera) {
    btnCamera.addEventListener('click', function (e) {
      e.stopPropagation();
      /* The camera over the lock screen, still locked: leaving it comes
         back here, and it cannot open anything else meanwhile (the OS's) */
      ZylIntent.startActivity({ targetApp: 'com.zylos.camera' });
    });
  }

})();
