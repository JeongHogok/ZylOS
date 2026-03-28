/*
 * BPI-OS 상태바
 */

(function () {
  'use strict';

  /* ─── 시간 업데이트 ─── */
  var sbTime = document.getElementById('sb-time');

  function updateTime() {
    var now = new Date();
    var h = String(now.getHours()).padStart(2, '0');
    var m = String(now.getMinutes()).padStart(2, '0');
    sbTime.textContent = h + ':' + m;
  }

  updateTime();
  setInterval(updateTime, 1000);

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
      '<div class="notif-icon">' + (icon || '📬') + '</div>' +
      '<div class="notif-content">' +
        '<div class="notif-title">' + title + '</div>' +
        '<div class="notif-body">' + body + '</div>' +
      '</div>' +
      '<div class="notif-time">' + (time || '지금') + '</div>';

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
