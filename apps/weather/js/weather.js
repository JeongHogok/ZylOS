// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Weather app — Samsung-grade UI with current, hourly, daily forecast
// Scope: Location service → Open-Meteo API → render weather data with dynamic backgrounds
// Dependency: location service (postMessage IPC), network service (Open-Meteo API)
// SOLID: SRP — weather display UI only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── DOM ─── */
  var locationName = document.getElementById('location-name');
  var temperature  = document.getElementById('temperature');
  var condition    = document.getElementById('condition');
  var humidity     = document.getElementById('humidity');
  var feelsLike    = document.getElementById('feels-like');
  var wind         = document.getElementById('wind');
  var pressure     = document.getElementById('pressure');
  var forecastEl   = document.getElementById('forecast-list');
  var hourlyEl     = document.getElementById('hourly-list');
  var refreshBtn   = document.getElementById('btn-refresh');
  var weatherIconLg = document.getElementById('weather-icon-lg');
  var sunrise      = document.getElementById('sunrise');
  var sunset       = document.getElementById('sunset');
  var uvIndex      = document.getElementById('uv-index');
  var cloudCover   = document.getElementById('cloud-cover');

  /* ─── State ─── */
  var lastLat = null;
  var lastLon = null;
  var locationTimeout = null;
  var isRefreshing = false;

  /* ─── i18n helper ─── */
  function t(key) {
    return (typeof zylI18n !== 'undefined') ? zylI18n.t(key) : key;
  }

  /* ─── Request location ─── */
  function requestLocation() {
    if (locationName) {
      locationName.textContent = t('weather.loading');
      locationName.setAttribute('data-loading', 'true');
    }

    /* 런타임 퍼미션: location 권한 체크 후 위치 요청 */
    if (typeof ZylPermissionDialog !== 'undefined') {
      ZylPermissionDialog.checkAndRequest('com.zylos.weather', 'location').then(function (granted) {
        if (granted) {
          ZylBridge.sendToSystem({
            type: 'service.request', service: 'location', method: 'getLastKnown'
          });
        } else {
          if (locationName) {
            locationName.textContent = t('weather.location_unavailable');
            locationName.removeAttribute('data-loading');
          }
          setRefreshing(false);
        }
      }).catch(function (err) {
        if (typeof console !== 'undefined') console.error('[Weather] permission check failed:', err);
        setRefreshing(false);
      });
    } else {
      /* ZylPermissionDialog 미로드 시 폴백 */
      ZylBridge.sendToSystem({
        type: 'service.request', service: 'location', method: 'getLastKnown'
      });
    }

    locationTimeout = setTimeout(function () {
      if (locationName && locationName.getAttribute('data-loading') === 'true') {
        locationName.textContent = t('weather.location_unavailable');
        locationName.removeAttribute('data-loading');
        setRefreshing(false);
      }
    }, 8000);
  }

  function setRefreshing(val) {
    isRefreshing = val;
    if (refreshBtn) {
      if (val) refreshBtn.classList.add('refreshing');
      else refreshBtn.classList.remove('refreshing');
    }
  }

  setRefreshing(true);
  requestLocation();

  /* ─── Message handler ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      if (msg.type === 'navigation.back') {
        ZylBridge.sendToSystem({ type: 'navigation.exit' });
        return;
      }

      if (msg.type !== 'service.response') return;

      /* Network response (weather data) */
      if (msg.service === 'network' && msg.method === 'fetch') {
        setRefreshing(false);
        if (msg.data && msg.data.error) {
          showError(t('weather.network_error'));
          return;
        }
        if (!msg.data) {
          showError(t('weather.network_error'));
          return;
        }
        try {
          var data = typeof msg.data === 'string' ? JSON.parse(msg.data) : msg.data;
          if (data.error) {
            showError(t('weather.network_error'));
            return;
          }
          renderCurrent(data);
          renderHourly(data);
          renderForecast(data);
          renderSunInfo(data);
          applyWeatherTheme(data.current ? data.current.weather_code : 0);
        } catch (err) { showError(t('weather.data_unavailable')); }
      }

      /* Location response */
      if (msg.service === 'location' && msg.method === 'getLastKnown' && msg.data) {
        clearTimeout(locationTimeout);
        lastLat = msg.data.latitude;
        lastLon = msg.data.longitude;
        var cityName = msg.data.city || '';
        var region = msg.data.region || '';
        var displayName = cityName;
        if (region && region !== cityName) displayName += ', ' + region;
        if (!displayName) displayName = lastLat.toFixed(2) + ', ' + lastLon.toFixed(2);
        if (locationName) {
          locationName.textContent = displayName;
          locationName.removeAttribute('data-loading');
        }
        fetchWeather(lastLat, lastLon);
      }
    } catch (err) { /* ignore */ }
  });

  /* ─── Refresh button ─── */
  if (refreshBtn) {
    refreshBtn.addEventListener('click', function () {
      if (isRefreshing) return;
      setRefreshing(true);
      requestLocation();
    });
  }

  /* ─── Fetch weather from Open-Meteo via network service ─── */
  function fetchWeather(lat, lon) {
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat + '&longitude=' + lon +
      '&current=temperature_2m,relative_humidity_2m,apparent_temperature,wind_speed_10m,surface_pressure,weather_code,cloud_cover' +
      '&hourly=temperature_2m,weather_code' +
      '&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,uv_index_max' +
      '&timezone=auto&forecast_days=7&forecast_hours=24';

    ZylBridge.sendToSystem({
      type: 'service.request', service: 'network', method: 'fetch', params: { url: url }
    });
  }

  /* ─── Render current weather ─── */
  function renderCurrent(data) {
    var c = data.current || {};
    if (temperature) temperature.textContent = Math.round(c.temperature_2m || 0) + '\u00B0';
    if (condition) condition.textContent = weatherCodeToText(c.weather_code);
    if (humidity) humidity.textContent = (c.relative_humidity_2m || 0) + '%';
    if (feelsLike) feelsLike.textContent = Math.round(c.apparent_temperature || c.temperature_2m || 0) + '\u00B0';
    if (wind) wind.textContent = (c.wind_speed_10m || 0) + ' m/s';
    if (pressure) pressure.textContent = Math.round(c.surface_pressure || 0) + ' hPa';
    if (weatherIconLg) weatherIconLg.textContent = weatherCodeToEmoji(c.weather_code);
    if (cloudCover) cloudCover.textContent = (c.cloud_cover || 0) + '%';
  }

  /* ─── Render hourly forecast ─── */
  function renderHourly(data) {
    if (!hourlyEl || !data.hourly) return;
    hourlyEl.innerHTML = '';
    var h = data.hourly;
    var now = new Date();
    var currentHour = now.getHours();
    var count = Math.min(h.time.length, 24);

    for (var i = 0; i < count; i++) {
      var time = new Date(h.time[i]);
      var hour = time.getHours();
      var label = (i === 0) ? t('weather.now') : (hour < 10 ? '0' : '') + hour + ':00';
      var temp = Math.round(h.temperature_2m[i]);
      var icon = weatherCodeToEmoji(h.weather_code[i]);

      var el = document.createElement('div');
      el.className = 'hourly-item';
      el.innerHTML =
        '<span class="hourly-time">' + label + '</span>' +
        '<span class="hourly-icon">' + icon + '</span>' +
        '<span class="hourly-temp">' + temp + '\u00B0</span>';
      hourlyEl.appendChild(el);
    }
  }

  /* ─── Render 7-day forecast ─── */
  function renderForecast(data) {
    if (!forecastEl || !data.daily) return;
    forecastEl.innerHTML = '';
    var days = data.daily;
    var count = Math.min(days.time.length, 7);
    var weekDayKeys = ['weather.day_sun', 'weather.day_mon', 'weather.day_tue', 'weather.day_wed', 'weather.day_thu', 'weather.day_fri', 'weather.day_sat'];

    /* Find global min/max for temp bar scaling */
    var globalMin = Infinity, globalMax = -Infinity;
    for (var j = 0; j < count; j++) {
      if (days.temperature_2m_min[j] < globalMin) globalMin = days.temperature_2m_min[j];
      if (days.temperature_2m_max[j] > globalMax) globalMax = days.temperature_2m_max[j];
    }
    var range = globalMax - globalMin || 1;

    for (var i = 0; i < count; i++) {
      var date = new Date(days.time[i] + 'T12:00:00');
      var dayName = (i === 0) ? t('weather.today') : t(weekDayKeys[date.getDay()]);
      var maxTemp = Math.round(days.temperature_2m_max[i]);
      var minTemp = Math.round(days.temperature_2m_min[i]);
      var icon = weatherCodeToEmoji(days.weather_code[i]);

      /* Temperature bar position */
      var left = ((days.temperature_2m_min[i] - globalMin) / range) * 100;
      var width = ((days.temperature_2m_max[i] - days.temperature_2m_min[i]) / range) * 100;
      if (width < 8) width = 8;

      var el = document.createElement('div');
      el.className = 'forecast-day';
      el.innerHTML =
        '<span class="forecast-name">' + dayName + '</span>' +
        '<span class="forecast-icon">' + icon + '</span>' +
        '<span class="forecast-temp-range">' +
          '<span class="forecast-min">' + minTemp + '\u00B0</span>' +
          '<span class="forecast-temp-bar"><span class="forecast-temp-fill" style="left:' + left + '%;width:' + width + '%"></span></span>' +
          '<span class="forecast-max">' + maxTemp + '\u00B0</span>' +
        '</span>';
      forecastEl.appendChild(el);
    }
  }

  /* ─── Render sun/UV info ─── */
  function renderSunInfo(data) {
    if (!data.daily) return;
    var d = data.daily;

    if (sunrise && d.sunrise && d.sunrise[0]) {
      var sr = new Date(d.sunrise[0]);
      sunrise.textContent = (sr.getHours() < 10 ? '0' : '') + sr.getHours() + ':' + (sr.getMinutes() < 10 ? '0' : '') + sr.getMinutes();
    }

    if (sunset && d.sunset && d.sunset[0]) {
      var ss = new Date(d.sunset[0]);
      sunset.textContent = (ss.getHours() < 10 ? '0' : '') + ss.getHours() + ':' + (ss.getMinutes() < 10 ? '0' : '') + ss.getMinutes();
    }

    if (uvIndex && d.uv_index_max && d.uv_index_max[0] !== undefined) {
      uvIndex.textContent = d.uv_index_max[0].toFixed(1);
    }
  }

  /* ─── Apply weather-based background theme ─── */
  function applyWeatherTheme(code) {
    var body = document.body;
    /* Remove all weather classes */
    body.className = body.className.replace(/weather-\w+/g, '').trim();

    var now = new Date();
    var hour = now.getHours();
    var isNight = (hour < 6 || hour >= 20);

    if (isNight) {
      body.classList.add('weather-night');
      return;
    }

    if (code === 0) body.classList.add('weather-clear');
    else if (code <= 3) body.classList.add('weather-cloudy');
    else if (code <= 49) body.classList.add('weather-fog');
    else if (code <= 69) body.classList.add('weather-rain');
    else if (code <= 79) body.classList.add('weather-snow');
    else if (code <= 99) body.classList.add('weather-storm');
    else body.classList.add('weather-clear');
  }

  function showError(msg) {
    if (condition) condition.textContent = msg;
  }

  /* ─── Weather code → text (i18n) ─── */
  function weatherCodeToText(code) {
    if (code === 0) return t('weather.clear');
    if (code <= 3) return t('weather.partly_cloudy');
    if (code <= 49) return t('weather.foggy');
    if (code <= 59) return t('weather.drizzle');
    if (code <= 69) return t('weather.rain');
    if (code <= 79) return t('weather.snow');
    if (code <= 99) return t('weather.thunderstorm');
    return t('weather.unknown');
  }

  /* ─── Weather code → emoji ─── */
  function weatherCodeToEmoji(code) {
    if (code === 0) return '\u2600\uFE0F';
    if (code <= 2) return '\u26C5';
    if (code <= 3) return '\u2601\uFE0F';
    if (code <= 49) return '\uD83C\uDF2B\uFE0F';
    if (code <= 55) return '\uD83C\uDF26\uFE0F';
    if (code <= 59) return '\uD83C\uDF27\uFE0F';
    if (code <= 65) return '\uD83C\uDF27\uFE0F';
    if (code <= 69) return '\uD83C\uDF28\uFE0F';
    if (code <= 75) return '\u2744\uFE0F';
    if (code <= 79) return '\uD83C\uDF28\uFE0F';
    if (code <= 82) return '\uD83C\uDF27\uFE0F';
    if (code <= 99) return '\u26C8\uFE0F';
    return '\u2753';
  }

  /* Enable touch scroll */
  if (typeof ZylTouch !== 'undefined') {
    var scrollArea = document.getElementById('weather-scroll');
    if (scrollArea) ZylTouch.enableScroll(scrollArea);
  }
})();
