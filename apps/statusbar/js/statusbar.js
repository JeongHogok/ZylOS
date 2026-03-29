// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Widget
//
// 역할: 상태바 위젯 — 시간, 배터리, 네트워크 상태 표시
// 수행범위: 시간 업데이트, 배터리 레벨 표시, 퀵 설정 패널
// 의존방향: ZylClock (clock.js)
// SOLID: SRP — 상태바 UI 렌더링만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── 시간 업데이트 (shared ZylClock 사용) ─── */
  var sbTime = document.getElementById('sb-time');
  var clock = ZylClock.create(sbTime, null, { showDate: false });

  /* ─── 배터리 시뮬레이션 ─── */
  var batteryPct = document.getElementById('sb-battery-pct');
  var batteryLevel = 85;

  function updateBattery() {
    if (navigator.getBattery) {
      navigator.getBattery().then(function (battery) {
        batteryLevel = Math.round(battery.level * 100);
        batteryPct.textContent = batteryLevel + '%';
      });
    }
  }
  updateBattery();

  /* ─── 빠른 설정 토글 ─── */
  document.querySelectorAll('.qs-btn').forEach(function (btn) {
    btn.addEventListener('click', function () {
      btn.classList.toggle('active');
    });
  });

  /* ─── 알림 드로어 토글 ─── */
  var drawer = document.getElementById('notification-drawer');
  var statusbar = document.getElementById('statusbar');
  var isDrawerOpen = false;

  /* 상태바 터치로 드로어 열기/닫기 */
  statusbar.addEventListener('click', function () {
    toggleDrawer();
  });

  function toggleDrawer() {
    isDrawerOpen = !isDrawerOpen;
    if (isDrawerOpen) {
      drawer.classList.remove('hidden');
      /* 강제 리플로우 후 애니메이션 */
      drawer.offsetHeight;
      drawer.classList.add('visible');
    } else {
      drawer.classList.remove('visible');
      setTimeout(function () {
        drawer.classList.add('hidden');
      }, 350);
    }
  }

  /* 드로어 핸들 드래그로 닫기 */
  var handle = drawer.querySelector('.drawer-handle');
  if (handle) {
    handle.addEventListener('click', function () {
      if (isDrawerOpen) toggleDrawer();
    });
  }

  /* ─── 알림 추가 (외부에서 호출 가능) ─── */
  window.addNotification = function (title, body, icon, time) {
    var list = document.getElementById('notification-list');
    var empty = list.querySelector('.notif-empty');
    if (empty) empty.remove();

    var notifDot = document.getElementById('sb-notif-dot');
    notifDot.classList.remove('hidden');

    var item = document.createElement('div');
    item.className = 'notif-item';
    item.innerHTML =
      '<div class="notif-icon">' + (icon || '\uD83D\uDCEC') + '</div>' +
      '<div class="notif-content">' +
        '<div class="notif-title">' + title + '</div>' +
        '<div class="notif-body">' + body + '</div>' +
      '</div>' +
      '<div class="notif-time">' + (time || '\uC9C0\uAE08') + '</div>';

    /* 스와이프로 삭제 */
    var startX = 0;
    item.addEventListener('touchstart', function (e) {
      startX = e.touches[0].clientX;
    });
    item.addEventListener('touchend', function (e) {
      var dx = e.changedTouches[0].clientX - startX;
      if (Math.abs(dx) > 100) {
        item.style.transform = 'translateX(' + (dx > 0 ? '100%' : '-100%') + ')';
        item.style.opacity = '0';
        setTimeout(function () { item.remove(); }, 300);
      }
    });

    list.prepend(item);
  };

})();
