// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 잠금화면 UI — 시계, PIN 입력, 스와이프 잠금해제
// 수행범위: PIN 인증, 스와이프 제스처 잠금해제, 시간/날짜 표시
// 의존방향: zylI18n (i18n.js), ZylClock (clock.js), ZylGesture (gesture.js), ZylBridge (bridge.js)
// SOLID: SRP — 잠금화면 UI와 인증 로직만 담당
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  var lockMain = document.getElementById('lock-main');
  var pinScreen = document.getElementById('pin-screen');
  var pinDots = document.querySelectorAll('.pin-dot');
  var pinError = document.getElementById('pin-error');
  var enteredPin = '';
  var correctPin = '0000'; /* 기본 PIN */

  /* ─── 시계 (shared ZylClock 사용) ─── */
  var lockTime = document.getElementById('lock-time');
  var lockDate = document.getElementById('lock-date');
  var clock = ZylClock.create(lockTime, lockDate, { showDate: true, dateFormat: 'long' });

  /* ─── 스와이프로 PIN 화면 열기 (shared ZylGesture 사용) ─── */
  var swipeGesture = ZylGesture.onSwipe(lockMain, function (e) {
    if (e.direction === 'up') {
      showPinScreen();
    }
  }, { direction: 'up', threshold: 80, axis: 'y' });

  /* 클릭으로도 PIN 화면 열기 (마우스 테스트용) */
  lockMain.addEventListener('click', function () {
    showPinScreen();
  });

  function showPinScreen() {
    lockMain.classList.add('hidden');
    pinScreen.classList.remove('hidden');
    enteredPin = '';
    updateDots();
    pinError.classList.add('hidden');
  }

  function hidePinScreen() {
    pinScreen.classList.add('hidden');
    lockMain.classList.remove('hidden');
  }

  /* ─── PIN 입력 ─── */
  document.querySelectorAll('.num-btn[data-num]').forEach(function (btn) {
    btn.addEventListener('click', function () {
      if (enteredPin.length >= 4) return;

      enteredPin += btn.dataset.num;
      updateDots();

      /* 4자리 입력 완료 시 검증 */
      if (enteredPin.length === 4) {
        setTimeout(verifyPin, 200);
      }
    });
  });

  /* 삭제 버튼 */
  document.getElementById('btn-delete').addEventListener('click', function () {
    if (enteredPin.length > 0) {
      enteredPin = enteredPin.slice(0, -1);
      updateDots();
      pinError.classList.add('hidden');
    }
  });

  /* 취소 버튼 */
  document.getElementById('btn-cancel').addEventListener('click', function () {
    hidePinScreen();
  });

  /* ─── 도트 업데이트 ─── */
  function updateDots() {
    pinDots.forEach(function (dot, i) {
      dot.classList.toggle('filled', i < enteredPin.length);
    });
  }

  /* ─── PIN 검증 (shared ZylBridge 사용) ─── */
  function verifyPin() {
    if (enteredPin === correctPin) {
      /* 잠금 해제 성공 */
      document.body.style.transition = 'opacity 0.3s';
      document.body.style.opacity = '0';
      setTimeout(function () {
        /* WAM에게 잠금 해제 알림 */
        ZylBridge.closeApp();
      }, 300);
    } else {
      /* 실패 - 흔들기 애니메이션 */
      pinError.classList.remove('hidden');
      var dots = document.getElementById('pin-dots');
      dots.classList.add('shake');
      setTimeout(function () {
        dots.classList.remove('shake');
        enteredPin = '';
        updateDots();
      }, 500);
    }
  }

  /* ─── 데모 알림 ─── */
  var notifList = document.getElementById('lock-notifications');
  var demoNotifs = [
    { icon: '\uD83D\uDCAC', title: '\uBA54\uC2DC\uC9C0', body: '\uC548\uB155\uD558\uC138\uC694! Zyl OS\uC5D0 \uC624\uC2E0 \uAC83\uC744 \uD658\uC601\uD569\uB2C8\uB2E4.', time: '2\uBD84 \uC804' },
    { icon: '\uD83D\uDCE7', title: '\uC774\uBA54\uC77C', body: '\uC2DC\uC2A4\uD15C \uC5C5\uB370\uC774\uD2B8\uAC00 \uC900\uBE44\uB418\uC5C8\uC2B5\uB2C8\uB2E4.', time: '15\uBD84 \uC804' },
  ];

  demoNotifs.forEach(function (n) {
    var el = document.createElement('div');
    el.className = 'lock-notif';
    el.innerHTML =
      '<div class="lock-notif-icon">' + n.icon + '</div>' +
      '<div class="lock-notif-text">' +
        '<div class="lock-notif-title">' + n.title + '</div>' +
        '<div class="lock-notif-body">' + n.body + '</div>' +
      '</div>' +
      '<div class="lock-notif-time">' + n.time + '</div>';
    notifList.appendChild(el);
  });

})();
