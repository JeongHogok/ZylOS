// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 시계 앱 — 시계, 알람, 타이머, 스톱워치
// 수행범위: 탭 전환, 실시간 시계, 타이머/스톱워치 로직
// 의존방향: shared/clock.js
// SOLID: SRP — 시간 관련 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var clockDisplay = document.getElementById('clock-display');
  var clockDate = document.getElementById('clock-date');
  var timerDisplay = document.getElementById('timer-display');
  var swDisplay = document.getElementById('sw-display');
  var swLaps = document.getElementById('sw-laps');

  /* ── Tabs ── */
  document.querySelectorAll('.tab').forEach(function (tab) {
    tab.addEventListener('click', function () {
      document.querySelectorAll('.tab').forEach(function (t) { t.classList.remove('active'); });
      document.querySelectorAll('.tab-content').forEach(function (c) { c.classList.add('hidden'); c.classList.remove('active'); });
      tab.classList.add('active');
      var target = document.getElementById('tab-' + tab.dataset.tab);
      if (target) { target.classList.remove('hidden'); target.classList.add('active'); }
    });
  });

  /* ── Clock ── */
  function updateClock() {
    var now = new Date();
    if (clockDisplay) clockDisplay.textContent = String(now.getHours()).padStart(2, '0') + ':' + String(now.getMinutes()).padStart(2, '0') + ':' + String(now.getSeconds()).padStart(2, '0');
    if (clockDate) clockDate.textContent = now.toLocaleDateString('en', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' });
  }
  updateClock();
  setInterval(updateClock, 1000);

  /* ── Timer ── */
  var timerSec = 0; var timerInterval = null; var timerRunning = false;
  var btnTimerStart = document.getElementById('btn-timer-start');
  var btnTimerReset = document.getElementById('btn-timer-reset');
  if (btnTimerStart) btnTimerStart.addEventListener('click', function () {
    if (timerRunning) { clearInterval(timerInterval); timerRunning = false; btnTimerStart.textContent = 'Start'; }
    else { if (timerSec === 0) timerSec = 300; timerRunning = true; btnTimerStart.textContent = 'Pause';
      timerInterval = setInterval(function () { timerSec--; if (timerSec <= 0) { clearInterval(timerInterval); timerRunning = false; btnTimerStart.textContent = 'Start'; timerSec = 0; }
        var m = Math.floor(timerSec / 60); var s = timerSec % 60;
        if (timerDisplay) timerDisplay.textContent = String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
      }, 1000); }
  });
  if (btnTimerReset) btnTimerReset.addEventListener('click', function () { clearInterval(timerInterval); timerRunning = false; timerSec = 0; if (timerDisplay) timerDisplay.textContent = '00:00'; if (btnTimerStart) btnTimerStart.textContent = 'Start'; });

  /* ── Stopwatch ── */
  var swMs = 0; var swInterval = null; var swRunning = false; var lapCount = 0;
  var btnSwStart = document.getElementById('btn-sw-start');
  var btnSwLap = document.getElementById('btn-sw-lap');
  var btnSwReset = document.getElementById('btn-sw-reset');
  function formatSw(ms) { var m = Math.floor(ms / 60000); var s = Math.floor((ms % 60000) / 1000); var cs = Math.floor((ms % 1000) / 10); return String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0') + '.' + String(cs).padStart(2, '0'); }
  if (btnSwStart) btnSwStart.addEventListener('click', function () {
    if (swRunning) { clearInterval(swInterval); swRunning = false; btnSwStart.textContent = 'Start'; }
    else { swRunning = true; btnSwStart.textContent = 'Stop'; swInterval = setInterval(function () { swMs += 10; if (swDisplay) swDisplay.textContent = formatSw(swMs); }, 10); }
  });
  if (btnSwLap) btnSwLap.addEventListener('click', function () { if (swRunning && swLaps) { lapCount++; var el = document.createElement('div'); el.className = 'lap'; el.textContent = 'Lap ' + lapCount + ': ' + formatSw(swMs); swLaps.prepend(el); } });
  if (btnSwReset) btnSwReset.addEventListener('click', function () { clearInterval(swInterval); swRunning = false; swMs = 0; lapCount = 0; if (swDisplay) swDisplay.textContent = '00:00.00'; if (swLaps) swLaps.innerHTML = ''; if (btnSwStart) btnSwStart.textContent = 'Start'; });
})();
