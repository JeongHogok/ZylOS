/*
 * BPI-OS 잠금화면
 */

(function () {
  'use strict';

  var lockMain = document.getElementById('lock-main');
  var pinScreen = document.getElementById('pin-screen');
  var pinDots = document.querySelectorAll('.pin-dot');
  var pinError = document.getElementById('pin-error');
  var enteredPin = '';
  var correctPin = '0000'; /* 기본 PIN */

  /* ─── 시계 ─── */
  var lockTime = document.getElementById('lock-time');
  var lockDate = document.getElementById('lock-date');

  var DAYS_KO = ['일요일', '월요일', '화요일', '수요일', '목요일', '금요일', '토요일'];

  function updateClock() {
    var now = new Date();
    lockTime.textContent =
      String(now.getHours()).padStart(2, '0') + ':' +
      String(now.getMinutes()).padStart(2, '0');

    var y = now.getFullYear();
    var m = now.getMonth() + 1;
    var d = now.getDate();
    lockDate.textContent = y + '년 ' + m + '월 ' + d + '일 ' + DAYS_KO[now.getDay()];
  }
  updateClock();
  setInterval(updateClock, 1000);

  /* ─── 스와이프로 PIN 화면 열기 ─── */
  var touchStartY = 0;

  lockMain.addEventListener('touchstart', function (e) {
    touchStartY = e.touches[0].clientY;
  });

  lockMain.addEventListener('touchend', function (e) {
    var dy = touchStartY - e.changedTouches[0].clientY;
    if (dy > 80) {
      showPinScreen();
    }
  });

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

  /* ─── PIN 검증 ─── */
  function verifyPin() {
    if (enteredPin === correctPin) {
      /* 잠금 해제 성공 */
      document.body.style.transition = 'opacity 0.3s';
      document.body.style.opacity = '0';
      setTimeout(function () {
        /* WAM에게 잠금 해제 알림 */
        if (window.navigator && window.navigator.system) {
          window.navigator.system.app.close();
        }
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
    { icon: '💬', title: '메시지', body: '안녕하세요! BPI-OS에 오신 것을 환영합니다.', time: '2분 전' },
    { icon: '📧', title: '이메일', body: '시스템 업데이트가 준비되었습니다.', time: '15분 전' },
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
