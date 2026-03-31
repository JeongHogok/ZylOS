// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 잠금화면 UI — 시계, PIN 입력, 스와이프 잠금해제, 알림 상호작용
// 수행범위: 실시간 스와이프 피드백, PIN 인증, 알림 터치→앱 이동, 에뮬레이터 연동
// 의존방향: ZylClock (clock.js), ZylGesture (gesture.js), ZylBridge (bridge.js)
// SOLID: SRP — 잠금화면 UI와 인증 로직만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── DOM ─── */
  var lockMain    = document.getElementById('lock-main');
  var pinScreen   = document.getElementById('pin-screen');
  var pinDots     = document.querySelectorAll('.pin-dot');
  var pinError    = document.getElementById('pin-error');
  var unlockFlash = document.getElementById('unlock-flash');

  /* ─── State ─── */
  var enteredPin   = '';
  var correctPin   = '';  /* Empty = no PIN set, swipe-only unlock */
  var pendingAppId = null;
  var failCount    = 0;
  var lockoutUntil = 0;

  /* Fetch current PIN from settings service */
  ZylBridge.sendToSystem({
    type: 'service.request',
    service: 'settings',
    method: 'get',
    params: { category: 'security' }
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
  if (typeof ZylClock !== 'undefined') {
    ZylClock.create(lockTime, lockDate, { showDate: true, dateFormat: 'long' });
  } else {
    /* 폴백: ZylClock 미로드 시 직접 업데이트 */
    function updateClock() {
      var now = new Date();
      lockTime.textContent = String(now.getHours()).padStart(2, '0') + ':' + String(now.getMinutes()).padStart(2, '0');
      var locale = (typeof zylI18n !== 'undefined' && zylI18n.getLocale) ? zylI18n.getLocale() : 'en';
      lockDate.textContent = now.toLocaleDateString(locale, { year: 'numeric', month: 'long', day: 'numeric', weekday: 'long' });
    }
    updateClock();
    setInterval(updateClock, 1000);
  }

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
      if (!correctPin) {
        /* No PIN set → swipe-only unlock */
        document.body.classList.add('unlocking');
        unlockFlash.classList.add('flash');
        setTimeout(function () {
          if (pendingAppId) {
            ZylBridge.sendToSystem({ type: 'app.launch', appId: pendingAppId });
          } else {
            ZylBridge.sendToSystem({ type: 'unlock' });
          }
        }, 500);
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
      if (enteredPin.length >= 4) return;
      enteredPin += btn.dataset.num;
      updateDots();

      if (enteredPin.length === 4) {
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
  function verifyPin() {
    /* Lockout check: 5 failed attempts → 30 second wait */
    if (Date.now() < lockoutUntil) {
      var remaining = Math.ceil((lockoutUntil - Date.now()) / 1000);
      pinError.textContent = zylI18n.t('lock.try_again', { s: remaining }) || ('Try again in ' + remaining + 's');
      pinError.classList.add('show');
      enteredPin = '';
      updateDots();
      return;
    }

    if (enteredPin === correctPin) {
      failCount = 0;
      /* Success: zoom-out + flash effect */
      document.body.classList.add('unlocking');
      unlockFlash.classList.add('flash');

      setTimeout(function () {
        /* 에뮬레이터에 잠금해제 알림 */
        if (pendingAppId) {
          ZylBridge.sendToSystem({
            type: 'app.launch', appId: pendingAppId
          });
        } else {
          ZylBridge.sendToSystem({
            type: 'unlock'
          });
        }

        /* 자체 Bridge 호출 (WAM 환경) */
        if (typeof ZylBridge !== 'undefined') {
          ZylBridge.closeApp();
        }
      }, 500);
    } else {
      /* Failure: shake + error + lockout after 5 attempts */
      failCount++;
      if (failCount >= 5) {
        lockoutUntil = Date.now() + 30000;
        failCount = 0;
      }
      pinError.textContent = (typeof zylI18n !== 'undefined') ? zylI18n.t('lock.wrong_pin') : 'Wrong PIN';
      pinError.classList.add('show');
      var dots = document.getElementById('pin-dots');
      dots.classList.add('shake');

      setTimeout(function () {
        dots.classList.remove('shake');
        enteredPin = '';
        updateDots();
      }, 500);
    }
  }

  /* ════════════════════════════════════════════
   *  알림: 터치→PIN→앱 이동 / 스와이프 삭제
   * ════════════════════════════════════════════ */
  var notifList = document.getElementById('lock-notifications');

  /* 에뮬레이터/시스템에서 메시지 수신 (postMessage) */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;
      /* Notification */
      if (msg.type === 'notification.push') {
        addLockNotification(msg.data);
      }
      /* PIN changed from settings */
      if (msg.type === 'settings.pinChanged' && msg.data && msg.data.pin) {
        correctPin = msg.data.pin;
      }
      /* Settings response — initial PIN load */
      if (msg.type === 'service.response' && msg.service === 'settings' && msg.data) {
        if (msg.data.pin) {
          correctPin = String(msg.data.pin);
        }
      }
    } catch (err) { /* ignore */ }
  });

  function addLockNotification(n) {
    var el = document.createElement('div');
    el.className = 'lock-notif';
    el.dataset.appId = n.appId || '';
    el.innerHTML =
      '<div class="lock-notif-icon">' + (n.icon || '🔔') + '</div>' +
      '<div class="lock-notif-text">' +
        '<div class="lock-notif-app">' + (n.appName || 'System') + '</div>' +
        '<div class="lock-notif-title">' + (n.title || '') + '</div>' +
        '<div class="lock-notif-body">' + (n.body || '') + '</div>' +
      '</div>' +
      '<div class="lock-notif-time">now</div>';

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
      /* Launch phone app with emergency dial intent */
      ZylBridge.sendToSystem({
        type: 'app.launch',
        appId: 'com.zylos.phone',
        intent: {
          action: 'zyl.intent.action.DIAL',
          data: 'tel:emergency',
          extras: { emergency: true }
        }
      });
    });
  }

  if (btnCamera) {
    btnCamera.addEventListener('click', function (e) {
      e.stopPropagation();
      /* Unlock + launch camera without PIN for quick capture */
      document.body.classList.add('unlocking');
      unlockFlash.classList.add('flash');
      setTimeout(function () {
        ZylBridge.sendToSystem({
          type: 'app.launch',
          appId: 'com.zylos.camera',
          intent: {
            action: 'zyl.intent.action.CAPTURE',
            extras: { fromLockscreen: true }
          }
        });
      }, 500);
    });
  }

})();
