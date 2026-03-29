// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 시계 앱 — 시계, 알람, 타이머, 스톱워치
// 수행범위: 탭 전환, 실시간 시계, 알람 관리/체크, 타이머 카운트다운, 스톱워치 랩
// 의존방향: shared/clock.js, shared/bridge.js → postMessage IPC (settings, notification)
// SOLID: SRP — 시간 관련 UI만 담당, OCP — 알람 목록 확장 가능
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ═══════════════════════════════════════════════════════════
     IPC — postMessage service calls
     ═══════════════════════════════════════════════════════════ */

  function requestService(service, method, params) {
    if (window.parent && window.parent !== window) {
      window.parent.postMessage(JSON.stringify({
        type: 'service.request',
        service: service,
        method: method,
        params: params || {}
      }), '*');
    }
  }

  /** Map of pending callbacks keyed by "service.method" */
  var serviceCallbacks = {};

  function onServiceResponse(service, method, cb) {
    serviceCallbacks[service + '.' + method] = cb;
  }

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      if (!e.data) return;
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        if (alarmForm && !alarmForm.classList.contains('hidden')) {
          alarmForm.classList.add('hidden');
          if (btnAddAlarm) btnAddAlarm.classList.remove('hidden');
          window.parent.postMessage(JSON.stringify({ type: 'navigation.handled' }), '*');
        } else {
          window.parent.postMessage(JSON.stringify({ type: 'navigation.exit' }), '*');
        }
        return;
      }

      if (msg.type !== 'service.response') return;
      var key = msg.service + '.' + msg.method;
      if (serviceCallbacks[key]) {
        serviceCallbacks[key](msg.params, msg.data);
      }
    } catch (err) { /* ignore parse errors */ }
  });

  /* ═══════════════════════════════════════════════════════════
     Audio — generate beep via Web Audio API
     ═══════════════════════════════════════════════════════════ */

  var audioCtx = null;

  function getAudioContext() {
    if (!audioCtx) {
      var Ctor = window.AudioContext || window.webkitAudioContext;
      if (Ctor) audioCtx = new Ctor();
    }
    return audioCtx;
  }

  function playBeep(frequency, durationMs, repeat) {
    var ctx = getAudioContext();
    if (!ctx) return;
    var count = repeat || 3;
    var gap = 200; // ms between beeps
    var i = 0;

    function beepOnce() {
      if (i >= count) return;
      var osc = ctx.createOscillator();
      var gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.value = frequency || 880;
      gain.gain.value = 0.3;
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(ctx.currentTime);
      osc.stop(ctx.currentTime + (durationMs || 200) / 1000);
      i++;
      setTimeout(beepOnce, (durationMs || 200) + gap);
    }
    beepOnce();
  }

  /* ═══════════════════════════════════════════════════════════
     DOM references
     ═══════════════════════════════════════════════════════════ */

  var clockDisplay = document.getElementById('clock-display');
  var clockDate = document.getElementById('clock-date');
  var timerDisplay = document.getElementById('timer-display');
  var swDisplay = document.getElementById('sw-display');
  var swLaps = document.getElementById('sw-laps');
  var alarmList = document.getElementById('alarm-list');
  var alarmEmpty = document.getElementById('alarm-empty');
  var alarmForm = document.getElementById('alarm-form');
  var btnAddAlarm = document.getElementById('btn-add-alarm');

  /* Timer inputs */
  var timerHoursInput = document.getElementById('timer-hours');
  var timerMinutesInput = document.getElementById('timer-minutes');
  var timerSecondsInput = document.getElementById('timer-seconds');
  var timerSetArea = document.getElementById('timer-set');

  /* ═══════════════════════════════════════════════════════════
     Utility
     ═══════════════════════════════════════════════════════════ */

  function pad(n) {
    return n < 10 ? '0' + n : String(n);
  }

  function generateId() {
    return Date.now().toString(36) + Math.random().toString(36).substr(2, 5);
  }

  /* ═══════════════════════════════════════════════════════════
     Tabs
     ═══════════════════════════════════════════════════════════ */

  var tabs = document.querySelectorAll('.tab');
  for (var ti = 0; ti < tabs.length; ti++) {
    (function (tab) {
      tab.addEventListener('click', function () {
        for (var j = 0; j < tabs.length; j++) {
          tabs[j].classList.remove('active');
        }
        var contents = document.querySelectorAll('.tab-content');
        for (var k = 0; k < contents.length; k++) {
          contents[k].classList.add('hidden');
          contents[k].classList.remove('active');
        }
        tab.classList.add('active');
        var target = document.getElementById('tab-' + tab.dataset.tab);
        if (target) {
          target.classList.remove('hidden');
          target.classList.add('active');
        }
      });
    })(tabs[ti]);
  }

  /* ═══════════════════════════════════════════════════════════
     Clock Tab
     ═══════════════════════════════════════════════════════════ */

  function updateClock() {
    var now = new Date();
    if (clockDisplay) {
      clockDisplay.textContent = pad(now.getHours()) + ':' + pad(now.getMinutes()) + ':' + pad(now.getSeconds());
    }
    if (clockDate) {
      if (window.zylI18n && typeof window.zylI18n.formatDate === 'function') {
        clockDate.textContent = window.zylI18n.formatDate(now);
      } else {
        clockDate.textContent = now.toLocaleDateString(zylI18n.getLocale(), {
          weekday: 'long', year: 'numeric', month: 'long', day: 'numeric'
        });
      }
    }
  }
  updateClock();
  setInterval(updateClock, 1000);

  /* ═══════════════════════════════════════════════════════════
     Alarm Tab
     ═══════════════════════════════════════════════════════════ */

  var alarms = []; // Array of alarm objects: { id, hour, minute, label, days[], enabled }
  var alarmsLoaded = false;

  var DAY_NAMES = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

  /* ── Persist alarms via settings service ── */

  function saveAlarms() {
    var serialized = JSON.stringify(alarms);
    requestService('settings', 'update', {
      category: 'alarms',
      key: 'list',
      value: serialized
    });
  }

  function loadAlarms() {
    requestService('settings', 'get', { category: 'alarms' });
  }

  onServiceResponse('settings', 'get', function (params, data) {
    if (!params || params.category !== 'alarms') return;
    if (data && data.list) {
      try {
        var parsed = JSON.parse(data.list);
        if (Array.isArray(parsed)) {
          alarms = parsed;
          alarmsLoaded = true;
          renderAlarms();
        }
      } catch (err) {
        alarms = [];
        alarmsLoaded = true;
        renderAlarms();
      }
    } else {
      alarms = [];
      alarmsLoaded = true;
      renderAlarms();
    }
  });

  onServiceResponse('settings', 'update', function (params, data) {
    // Acknowledge update; no action needed
  });

  /* ── Render alarm list ── */

  function renderAlarms() {
    if (!alarmList) return;
    alarmList.innerHTML = '';

    if (alarms.length === 0) {
      if (alarmEmpty) alarmEmpty.classList.remove('hidden');
      return;
    }
    if (alarmEmpty) alarmEmpty.classList.add('hidden');

    for (var i = 0; i < alarms.length; i++) {
      (function (alarm, idx) {
        var row = document.createElement('div');
        row.className = 'alarm-row';

        var timeEl = document.createElement('div');
        timeEl.className = 'alarm-time';
        timeEl.textContent = pad(alarm.hour) + ':' + pad(alarm.minute);

        var infoEl = document.createElement('div');
        infoEl.className = 'alarm-info';

        var labelEl = document.createElement('span');
        labelEl.className = 'alarm-label-text';
        labelEl.textContent = alarm.label || zylI18n.t('clock.alarm');

        var daysEl = document.createElement('span');
        daysEl.className = 'alarm-days-text';
        if (alarm.days && alarm.days.length > 0 && alarm.days.length < 7) {
          var dayLabels = [];
          for (var d = 0; d < alarm.days.length; d++) {
            dayLabels.push(DAY_NAMES[alarm.days[d]] || '');
          }
          daysEl.textContent = dayLabels.join(', ');
        } else if (alarm.days && alarm.days.length === 7) {
          daysEl.textContent = zylI18n.t('clock.every_day');
        } else {
          daysEl.textContent = zylI18n.t('clock.once');
        }

        infoEl.appendChild(labelEl);
        infoEl.appendChild(daysEl);

        var controls = document.createElement('div');
        controls.className = 'alarm-controls';

        /* Toggle switch */
        var toggleLabel = document.createElement('label');
        toggleLabel.className = 'alarm-toggle';
        var toggleInput = document.createElement('input');
        toggleInput.type = 'checkbox';
        toggleInput.checked = alarm.enabled !== false;
        toggleInput.addEventListener('change', function () {
          alarms[idx].enabled = this.checked;
          saveAlarms();
        });
        var toggleSlider = document.createElement('span');
        toggleSlider.className = 'toggle-slider';
        toggleLabel.appendChild(toggleInput);
        toggleLabel.appendChild(toggleSlider);

        /* Delete button */
        var delBtn = document.createElement('button');
        delBtn.className = 'alarm-delete';
        delBtn.textContent = '\u00D7';
        delBtn.setAttribute('aria-label', 'Delete alarm');
        delBtn.addEventListener('click', function () {
          alarms.splice(idx, 1);
          saveAlarms();
          renderAlarms();
        });

        controls.appendChild(toggleLabel);
        controls.appendChild(delBtn);

        row.appendChild(timeEl);
        row.appendChild(infoEl);
        row.appendChild(controls);
        alarmList.appendChild(row);
      })(alarms[i], i);
    }
  }

  /* ── Alarm form ── */

  if (btnAddAlarm) {
    btnAddAlarm.addEventListener('click', function () {
      if (alarmForm) {
        alarmForm.classList.remove('hidden');
        btnAddAlarm.classList.add('hidden');
        // Reset form
        var hourInput = document.getElementById('alarm-hour');
        var minInput = document.getElementById('alarm-minute');
        var labelInput = document.getElementById('alarm-label');
        if (hourInput) hourInput.value = '8';
        if (minInput) minInput.value = '0';
        if (labelInput) labelInput.value = '';
        // Deselect all days
        var dayBtns = document.querySelectorAll('.day-btn');
        for (var d = 0; d < dayBtns.length; d++) {
          dayBtns[d].classList.remove('active');
        }
      }
    });
  }

  /* Day buttons toggle */
  var dayButtons = document.querySelectorAll('.day-btn');
  for (var di = 0; di < dayButtons.length; di++) {
    dayButtons[di].addEventListener('click', function () {
      this.classList.toggle('active');
    });
  }

  /* Cancel */
  var alarmFormCancel = document.getElementById('alarm-form-cancel');
  if (alarmFormCancel) {
    alarmFormCancel.addEventListener('click', function () {
      if (alarmForm) alarmForm.classList.add('hidden');
      if (btnAddAlarm) btnAddAlarm.classList.remove('hidden');
    });
  }

  /* Save */
  var alarmFormSave = document.getElementById('alarm-form-save');
  if (alarmFormSave) {
    alarmFormSave.addEventListener('click', function () {
      var hourInput = document.getElementById('alarm-hour');
      var minInput = document.getElementById('alarm-minute');
      var labelInput = document.getElementById('alarm-label');

      var hour = parseInt(hourInput ? hourInput.value : '8', 10);
      var minute = parseInt(minInput ? minInput.value : '0', 10);
      if (isNaN(hour) || hour < 0 || hour > 23) hour = 8;
      if (isNaN(minute) || minute < 0 || minute > 59) minute = 0;

      var label = (labelInput ? labelInput.value : '').trim() || 'Alarm';

      var selectedDays = [];
      var activeDayBtns = document.querySelectorAll('.day-btn.active');
      for (var d = 0; d < activeDayBtns.length; d++) {
        selectedDays.push(parseInt(activeDayBtns[d].dataset.day, 10));
      }
      selectedDays.sort();

      var newAlarm = {
        id: generateId(),
        hour: hour,
        minute: minute,
        label: label,
        days: selectedDays,
        enabled: true
      };

      alarms.push(newAlarm);
      saveAlarms();
      renderAlarms();

      if (alarmForm) alarmForm.classList.add('hidden');
      if (btnAddAlarm) btnAddAlarm.classList.remove('hidden');
    });
  }

  /* ── Alarm checker — runs every 30 seconds ── */

  var lastTriggeredKey = ''; // Prevent duplicate triggers within same minute

  function checkAlarms() {
    if (alarms.length === 0) return;
    var now = new Date();
    var currentHour = now.getHours();
    var currentMinute = now.getMinutes();
    var currentDay = now.getDay(); // 0=Sun, 1=Mon, ... 6=Sat
    var timeKey = currentHour + ':' + currentMinute;

    for (var i = 0; i < alarms.length; i++) {
      var alarm = alarms[i];
      if (!alarm.enabled) continue;
      if (alarm.hour !== currentHour || alarm.minute !== currentMinute) continue;

      var alarmKey = alarm.id + '-' + timeKey;
      if (lastTriggeredKey === alarmKey) continue;

      /* Check day match */
      var dayMatch = false;
      if (!alarm.days || alarm.days.length === 0) {
        dayMatch = true; // One-time alarm
      } else {
        for (var d = 0; d < alarm.days.length; d++) {
          if (alarm.days[d] === currentDay) {
            dayMatch = true;
            break;
          }
        }
      }
      if (!dayMatch) continue;

      /* Trigger alarm */
      lastTriggeredKey = alarmKey;
      triggerAlarm(alarm, i);
    }
  }

  function triggerAlarm(alarm, index) {
    /* Send notification via IPC */
    requestService('notification', 'post', {
      appId: 'com.zylos.clock',
      title: alarm.label || 'Alarm',
      body: pad(alarm.hour) + ':' + pad(alarm.minute),
      icon: '',
      priority: 2
    });

    /* Play beep sound */
    playBeep(880, 200, 5);

    /* Disable one-time alarms (no repeat days) */
    if (!alarm.days || alarm.days.length === 0) {
      alarms[index].enabled = false;
      saveAlarms();
      renderAlarms();
    }
  }

  setInterval(checkAlarms, 30000);

  /* Load alarms from settings on startup */
  loadAlarms();

  /* ═══════════════════════════════════════════════════════════
     Timer Tab
     ═══════════════════════════════════════════════════════════ */

  var timerTotalSec = 0;
  var timerRemaining = 0;
  var timerInterval = null;
  var timerRunning = false;
  var timerFlashInterval = null;

  var btnTimerStart = document.getElementById('btn-timer-start');
  var btnTimerReset = document.getElementById('btn-timer-reset');

  function renderTimerDisplay(totalSec) {
    var h = Math.floor(totalSec / 3600);
    var m = Math.floor((totalSec % 3600) / 60);
    var s = totalSec % 60;
    if (timerDisplay) {
      timerDisplay.textContent = pad(h) + ':' + pad(m) + ':' + pad(s);
    }
  }

  function getTimerInputSeconds() {
    var h = parseInt(timerHoursInput ? timerHoursInput.value : '0', 10) || 0;
    var m = parseInt(timerMinutesInput ? timerMinutesInput.value : '0', 10) || 0;
    var s = parseInt(timerSecondsInput ? timerSecondsInput.value : '0', 10) || 0;
    return (h * 3600) + (m * 60) + s;
  }

  function setTimerInputs(totalSec) {
    var h = Math.floor(totalSec / 3600);
    var m = Math.floor((totalSec % 3600) / 60);
    var s = totalSec % 60;
    if (timerHoursInput) timerHoursInput.value = String(h);
    if (timerMinutesInput) timerMinutesInput.value = String(m);
    if (timerSecondsInput) timerSecondsInput.value = String(s);
  }

  function stopTimerFlash() {
    if (timerFlashInterval) {
      clearInterval(timerFlashInterval);
      timerFlashInterval = null;
    }
    if (timerDisplay) timerDisplay.classList.remove('flash');
  }

  function onTimerComplete() {
    timerRunning = false;
    timerRemaining = 0;
    if (btnTimerStart) btnTimerStart.textContent = zylI18n.t('clock.start');

    /* Notification */
    requestService('notification', 'post', {
      appId: 'com.zylos.clock',
      title: 'Timer',
      body: 'Time is up!',
      icon: '',
      priority: 2
    });

    /* Audio beep */
    playBeep(1000, 150, 5);

    /* Visual flash */
    if (timerDisplay) {
      var flashCount = 0;
      timerFlashInterval = setInterval(function () {
        timerDisplay.classList.toggle('flash');
        flashCount++;
        if (flashCount >= 10) {
          stopTimerFlash();
        }
      }, 500);
    }
  }

  /* Preset buttons */
  var presetBtns = document.querySelectorAll('.preset-btn');
  for (var pi = 0; pi < presetBtns.length; pi++) {
    (function (btn) {
      btn.addEventListener('click', function () {
        var sec = parseInt(btn.dataset.seconds, 10) || 300;
        if (!timerRunning) {
          setTimerInputs(sec);
          renderTimerDisplay(sec);
        }
      });
    })(presetBtns[pi]);
  }

  /* Start / Pause */
  if (btnTimerStart) {
    btnTimerStart.addEventListener('click', function () {
      stopTimerFlash();

      if (timerRunning) {
        /* Pause */
        clearInterval(timerInterval);
        timerRunning = false;
        btnTimerStart.textContent = zylI18n.t('clock.start');
        if (timerSetArea) timerSetArea.classList.remove('hidden');
        return;
      }

      /* Start */
      if (timerRemaining <= 0) {
        timerTotalSec = getTimerInputSeconds();
        if (timerTotalSec <= 0) return;
        timerRemaining = timerTotalSec;
      }

      timerRunning = true;
      btnTimerStart.textContent = zylI18n.t('clock.pause');
      if (timerSetArea) timerSetArea.classList.add('hidden');

      timerInterval = setInterval(function () {
        timerRemaining--;
        renderTimerDisplay(timerRemaining);
        if (timerRemaining <= 0) {
          clearInterval(timerInterval);
          timerInterval = null;
          if (timerSetArea) timerSetArea.classList.remove('hidden');
          onTimerComplete();
        }
      }, 1000);
    });
  }

  /* Reset */
  if (btnTimerReset) {
    btnTimerReset.addEventListener('click', function () {
      clearInterval(timerInterval);
      timerInterval = null;
      timerRunning = false;
      timerRemaining = 0;
      stopTimerFlash();
      renderTimerDisplay(0);
      if (btnTimerStart) btnTimerStart.textContent = zylI18n.t('clock.start');
      if (timerSetArea) timerSetArea.classList.remove('hidden');
    });
  }

  /* Initialize display */
  renderTimerDisplay(0);

  /* ═══════════════════════════════════════════════════════════
     Stopwatch Tab
     ═══════════════════════════════════════════════════════════ */

  var swMs = 0;
  var swInterval = null;
  var swRunning = false;
  var lapCount = 0;
  var lastLapMs = 0;

  var btnSwStart = document.getElementById('btn-sw-start');
  var btnSwLap = document.getElementById('btn-sw-lap');
  var btnSwReset = document.getElementById('btn-sw-reset');

  function formatSw(ms) {
    var m = Math.floor(ms / 60000);
    var s = Math.floor((ms % 60000) / 1000);
    var cs = Math.floor((ms % 1000) / 10);
    return pad(m) + ':' + pad(s) + '.' + pad(cs);
  }

  if (btnSwStart) {
    btnSwStart.addEventListener('click', function () {
      if (swRunning) {
        /* Stop */
        clearInterval(swInterval);
        swRunning = false;
        btnSwStart.textContent = zylI18n.t('clock.start');
      } else {
        /* Start */
        swRunning = true;
        btnSwStart.textContent = zylI18n.t('clock.stop');
        swInterval = setInterval(function () {
          swMs += 10;
          if (swDisplay) swDisplay.textContent = formatSw(swMs);
        }, 10);
      }
    });
  }

  if (btnSwLap) {
    btnSwLap.addEventListener('click', function () {
      if (swRunning && swLaps) {
        lapCount++;
        var lapMs = swMs - lastLapMs;
        lastLapMs = swMs;
        var el = document.createElement('div');
        el.className = 'lap';
        var numSpan = document.createElement('span');
        numSpan.className = 'lap-number';
        numSpan.textContent = zylI18n.t('clock.lap') + ' ' + lapCount;
        var lapTimeSpan = document.createElement('span');
        lapTimeSpan.className = 'lap-split';
        lapTimeSpan.textContent = formatSw(lapMs);
        var totalSpan = document.createElement('span');
        totalSpan.className = 'lap-total';
        totalSpan.textContent = formatSw(swMs);
        el.appendChild(numSpan);
        el.appendChild(lapTimeSpan);
        el.appendChild(totalSpan);
        swLaps.insertBefore(el, swLaps.firstChild);
      }
    });
  }

  if (btnSwReset) {
    btnSwReset.addEventListener('click', function () {
      clearInterval(swInterval);
      swRunning = false;
      swMs = 0;
      lapCount = 0;
      lastLapMs = 0;
      if (swDisplay) swDisplay.textContent = '00:00.00';
      if (swLaps) swLaps.innerHTML = '';
      if (btnSwStart) btnSwStart.textContent = zylI18n.t('clock.start');
    });
  }

})();
