// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 시계 앱 — 시계, 알람(스누즈 포함), 세계시계, 타이머, 스톱워치
// 수행범위: 탭 전환, 실시간 시계, 알람 관리/체크/스누즈, 세계시계 관리, 타이머 카운트다운, 스톱워치 랩
// 의존방향: shared/clock.js, shared/bridge.js → postMessage IPC (settings, notification)
// SOLID: SRP — 시간 관련 UI만 담당, OCP — 알람/도시 목록 확장 가능
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
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: service,
      method: method,
      params: params || {}
    });
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
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else if (snoozeOverlay && !snoozeOverlay.classList.contains('hidden')) {
          dismissSnooze();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else if (cityPicker && !cityPicker.classList.contains('hidden')) {
          cityPicker.classList.add('hidden');
          if (btnAddCity) btnAddCity.classList.remove('hidden');
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type === 'audio.volumeChanged' && msg.data && msg.data.stream === 'alarm') {
        systemAlarmVolume = msg.data.value;
        return;
      }

      if (msg.type !== 'service.response') return;

      if (msg.service === 'audio' && msg.method === 'getVolume' && msg.data != null) {
        systemAlarmVolume = typeof msg.data === 'object' ? msg.data.value : msg.data;
        return;
      }

      var key = msg.service + '.' + msg.method;
      if (serviceCallbacks[key]) {
        serviceCallbacks[key](msg.params, msg.data);
      }
    } catch (err) { /* ignore parse errors */ }
  });

  /* ═══════════════════════════════════════════════════════════
     Audio — delegate to audio.playBeep service
     ═══════════════════════════════════════════════════════════ */

  var systemAlarmVolume = 90;

  function playBeep(frequency, durationMs, repeat) {
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'audio', method: 'playBeep',
      params: { frequency: frequency, duration: durationMs, repeat: repeat }
    });
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

  /* Snooze overlay */
  var snoozeOverlay = document.getElementById('alarm-snooze-overlay');
  var snoozeLabel = document.getElementById('snooze-label');
  var snoozeTime = document.getElementById('snooze-time');
  var btnSnoozeDismiss = document.getElementById('btn-snooze-dismiss');
  var btnSnooze = document.getElementById('btn-snooze');

  /* World clock */
  var worldList = document.getElementById('world-list');
  var worldEmpty = document.getElementById('world-empty');
  var btnAddCity = document.getElementById('btn-add-city');
  var cityPicker = document.getElementById('city-picker');
  var cityPickerCancel = document.getElementById('city-picker-cancel');
  var citySearch = document.getElementById('city-search');
  var cityResults = document.getElementById('city-results');

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
  var clockInterval = setInterval(updateClock, 1000);

  /* ═══════════════════════════════════════════════════════════
     Alarm Tab
     ═══════════════════════════════════════════════════════════ */

  var alarms = []; // Array of alarm objects: { id, hour, minute, label, days[], enabled }
  var alarmsLoaded = false;
  var SNOOZE_MINUTES = 5;
  var activeSnoozeAlarm = null;
  var snoozeTimerId = null;

  var DAY_KEYS = ['clock.day_sun', 'clock.day_mon', 'clock.day_tue', 'clock.day_wed', 'clock.day_thu', 'clock.day_fri', 'clock.day_sat'];

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
    if (!params) return;
    if (params.category === 'alarms') {
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
    }
    if (params.category === 'worldclock') {
      if (data && data.cities) {
        try {
          var parsed = JSON.parse(data.cities);
          if (Array.isArray(parsed)) {
            worldCities = parsed;
            renderWorldClock();
          }
        } catch (err) { /* ignore */ }
      }
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
            dayLabels.push(zylI18n.t(DAY_KEYS[alarm.days[d]]) || '');
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

      var label = (labelInput ? labelInput.value : '').trim() || zylI18n.t('clock.alarm');

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

  /* ── Snooze UI ── */

  function showSnoozeUI(alarm) {
    activeSnoozeAlarm = alarm;
    if (snoozeLabel) snoozeLabel.textContent = alarm.label || zylI18n.t('clock.alarm');
    if (snoozeTime) snoozeTime.textContent = pad(alarm.hour) + ':' + pad(alarm.minute);
    if (snoozeOverlay) snoozeOverlay.classList.remove('hidden');
  }

  function dismissSnooze() {
    activeSnoozeAlarm = null;
    if (snoozeTimerId) { clearTimeout(snoozeTimerId); snoozeTimerId = null; }
    if (snoozeOverlay) snoozeOverlay.classList.add('hidden');
  }

  if (btnSnoozeDismiss) {
    btnSnoozeDismiss.addEventListener('click', function () {
      dismissSnooze();
    });
  }

  if (btnSnooze) {
    btnSnooze.addEventListener('click', function () {
      if (!activeSnoozeAlarm) return;
      var alarm = activeSnoozeAlarm;
      if (snoozeOverlay) snoozeOverlay.classList.add('hidden');

      /* Re-trigger after SNOOZE_MINUTES */
      snoozeTimerId = setTimeout(function () {
        triggerAlarm(alarm, -1); /* -1 = snooze re-trigger, don't disable */
      }, SNOOZE_MINUTES * 60 * 1000);
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
      title: alarm.label || zylI18n.t('clock.alarm'),
      body: pad(alarm.hour) + ':' + pad(alarm.minute),
      icon: '',
      priority: 2
    });

    /* Play beep sound */
    playBeep(880, 200, 5);

    /* Show snooze UI */
    showSnoozeUI(alarm);

    /* Disable one-time alarms (no repeat days) — only if not a snooze re-trigger */
    if (index >= 0 && (!alarm.days || alarm.days.length === 0)) {
      alarms[index].enabled = false;
      saveAlarms();
      renderAlarms();
    }
  }

  /* FIX: Was 30000ms (30s) — could miss alarms by up to 30s if the minute boundary
   *       was crossed between checks. Reduced to 5000ms (5s) to ensure alarms fire
   *       within 5s of the target minute. lastTriggeredKey still prevents double-fires. */
  var alarmCheckInterval = setInterval(checkAlarms, 5000);

  /* Load alarms from settings on startup */
  loadAlarms();
  /* FIX: Run an initial alarm check immediately after load in case the app
   *       opens exactly at an alarm time (before the first interval tick). */
  setTimeout(checkAlarms, 500);

  /* Request system alarm volume */
  requestService('audio', 'getVolume', { stream: 'alarm' });

  /* ═══════════════════════════════════════════════════════════
     World Clock Tab
     ═══════════════════════════════════════════════════════════ */

  var worldCities = []; /* Array of { name, tz } — IANA timezone IDs */
  var worldInterval = null;

  /* Common timezone database (subset of IANA) */
  var TIMEZONE_DB = [
    { name: 'New York', tz: 'America/New_York' },
    { name: 'Los Angeles', tz: 'America/Los_Angeles' },
    { name: 'Chicago', tz: 'America/Chicago' },
    { name: 'London', tz: 'Europe/London' },
    { name: 'Paris', tz: 'Europe/Paris' },
    { name: 'Berlin', tz: 'Europe/Berlin' },
    { name: 'Moscow', tz: 'Europe/Moscow' },
    { name: 'Dubai', tz: 'Asia/Dubai' },
    { name: 'Mumbai', tz: 'Asia/Kolkata' },
    { name: 'Bangkok', tz: 'Asia/Bangkok' },
    { name: 'Singapore', tz: 'Asia/Singapore' },
    { name: 'Hong Kong', tz: 'Asia/Hong_Kong' },
    { name: 'Shanghai', tz: 'Asia/Shanghai' },
    { name: 'Tokyo', tz: 'Asia/Tokyo' },
    { name: 'Seoul', tz: 'Asia/Seoul' },
    { name: 'Sydney', tz: 'Australia/Sydney' },
    { name: 'Auckland', tz: 'Pacific/Auckland' },
    { name: 'Honolulu', tz: 'Pacific/Honolulu' },
    { name: 'Anchorage', tz: 'America/Anchorage' },
    { name: 'Denver', tz: 'America/Denver' },
    { name: 'Toronto', tz: 'America/Toronto' },
    { name: 'Mexico City', tz: 'America/Mexico_City' },
    { name: 'Sao Paulo', tz: 'America/Sao_Paulo' },
    { name: 'Buenos Aires', tz: 'America/Argentina/Buenos_Aires' },
    { name: 'Cairo', tz: 'Africa/Cairo' },
    { name: 'Johannesburg', tz: 'Africa/Johannesburg' },
    { name: 'Istanbul', tz: 'Europe/Istanbul' },
    { name: 'Rome', tz: 'Europe/Rome' },
    { name: 'Madrid', tz: 'Europe/Madrid' },
    { name: 'Amsterdam', tz: 'Europe/Amsterdam' },
    { name: 'Jakarta', tz: 'Asia/Jakarta' },
    { name: 'Taipei', tz: 'Asia/Taipei' },
    { name: 'Kuala Lumpur', tz: 'Asia/Kuala_Lumpur' }
  ];

  function getTimeInTimezone(tz) {
    try {
      var now = new Date();
      var parts = now.toLocaleString('en-US', { timeZone: tz, hour: '2-digit', minute: '2-digit', hour12: false });
      return parts;
    } catch (err) {
      return '--:--';
    }
  }

  function getDateInTimezone(tz) {
    try {
      var now = new Date();
      var parts = now.toLocaleString('en-US', { timeZone: tz, weekday: 'short', month: 'short', day: 'numeric' });
      return parts;
    } catch (err) {
      return '';
    }
  }

  function getOffsetLabel(tz) {
    try {
      var now = new Date();
      var localOffset = now.getTimezoneOffset(); /* in minutes, inverted sign */
      /* Get target offset by comparing date strings */
      var localStr = now.toLocaleString('en-US', { timeZone: Intl.DateTimeFormat().resolvedOptions().timeZone });
      var targetStr = now.toLocaleString('en-US', { timeZone: tz });
      var localDate = new Date(localStr);
      var targetDate = new Date(targetStr);
      var diffMs = targetDate.getTime() - localDate.getTime();
      var diffHours = Math.round(diffMs / 3600000);
      if (diffHours === 0) return zylI18n.t('clock.same_time');
      var sign = diffHours > 0 ? '+' : '';
      return sign + diffHours + 'h';
    } catch (err) {
      return '';
    }
  }

  function renderWorldClock() {
    if (!worldList) return;
    worldList.innerHTML = '';

    if (worldCities.length === 0) {
      if (worldEmpty) worldEmpty.classList.remove('hidden');
      return;
    }
    if (worldEmpty) worldEmpty.classList.add('hidden');

    for (var i = 0; i < worldCities.length; i++) {
      (function (city, idx) {
        var row = document.createElement('div');
        row.className = 'world-row';

        var infoDiv = document.createElement('div');
        infoDiv.className = 'world-info';

        var nameEl = document.createElement('div');
        nameEl.className = 'world-city-name';
        nameEl.textContent = city.name;

        var dateEl = document.createElement('div');
        dateEl.className = 'world-city-date';
        dateEl.textContent = getDateInTimezone(city.tz) + ' \u00B7 ' + getOffsetLabel(city.tz);

        infoDiv.appendChild(nameEl);
        infoDiv.appendChild(dateEl);

        var timeEl = document.createElement('div');
        timeEl.className = 'world-city-time';
        timeEl.setAttribute('data-tz', city.tz);
        timeEl.textContent = getTimeInTimezone(city.tz);

        var delBtn = document.createElement('button');
        delBtn.className = 'alarm-delete';
        delBtn.textContent = '\u00D7';
        delBtn.addEventListener('click', function () {
          worldCities.splice(idx, 1);
          saveWorldCities();
          renderWorldClock();
        });

        row.appendChild(infoDiv);
        row.appendChild(timeEl);
        row.appendChild(delBtn);
        worldList.appendChild(row);
      })(worldCities[i], i);
    }

    /* Auto-update world clocks */
    if (worldInterval) clearInterval(worldInterval);
    worldInterval = setInterval(function () {
      var timeEls = worldList.querySelectorAll('.world-city-time');
      for (var t = 0; t < timeEls.length; t++) {
        var tz = timeEls[t].getAttribute('data-tz');
        if (tz) timeEls[t].textContent = getTimeInTimezone(tz);
      }
    }, 30000);
  }

  function saveWorldCities() {
    requestService('settings', 'update', {
      category: 'worldclock',
      key: 'cities',
      value: JSON.stringify(worldCities)
    });
  }

  function loadWorldCities() {
    requestService('settings', 'get', { category: 'worldclock' });
  }

  /* ── City picker ── */
  if (btnAddCity) {
    btnAddCity.addEventListener('click', function () {
      if (cityPicker) {
        cityPicker.classList.remove('hidden');
        btnAddCity.classList.add('hidden');
        if (citySearch) { citySearch.value = ''; citySearch.focus(); }
        renderCityResults('');
      }
    });
  }

  if (cityPickerCancel) {
    cityPickerCancel.addEventListener('click', function () {
      if (cityPicker) cityPicker.classList.add('hidden');
      if (btnAddCity) btnAddCity.classList.remove('hidden');
    });
  }

  if (citySearch) {
    citySearch.addEventListener('input', function () {
      renderCityResults(citySearch.value.trim().toLowerCase());
    });
  }

  function renderCityResults(query) {
    if (!cityResults) return;
    cityResults.innerHTML = '';
    var existingTzs = {};
    for (var e = 0; e < worldCities.length; e++) {
      existingTzs[worldCities[e].tz] = true;
    }

    for (var i = 0; i < TIMEZONE_DB.length; i++) {
      var city = TIMEZONE_DB[i];
      if (existingTzs[city.tz]) continue; /* Already added */
      if (query && city.name.toLowerCase().indexOf(query) === -1) continue;

      (function (c) {
        var el = document.createElement('div');
        el.className = 'city-result-item';

        var nameSpan = document.createElement('span');
        nameSpan.textContent = c.name;

        var timeSpan = document.createElement('span');
        timeSpan.className = 'city-result-time';
        timeSpan.textContent = getTimeInTimezone(c.tz);

        el.appendChild(nameSpan);
        el.appendChild(timeSpan);

        el.addEventListener('click', function () {
          worldCities.push({ name: c.name, tz: c.tz });
          saveWorldCities();
          renderWorldClock();
          if (cityPicker) cityPicker.classList.add('hidden');
          if (btnAddCity) btnAddCity.classList.remove('hidden');
        });
        cityResults.appendChild(el);
      })(city);
    }
  }

  loadWorldCities();

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
      title: zylI18n.t('clock.timer'),
      body: zylI18n.t('clock.timer_done'),
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
