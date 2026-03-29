// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Weather app — location-based current weather + 7-day forecast
// Scope: Location service → Open-Meteo API → render weather data
// Dependency: location service (postMessage IPC), Open-Meteo API (free, no key)
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
  var wind         = document.getElementById('wind');
  var pressure     = document.getElementById('pressure');
  var forecastEl   = document.getElementById('forecast-list');
  var refreshBtn   = document.getElementById('btn-refresh');
  var weatherIcon  = document.getElementById('weather-icon');

  /* ─── State ─── */
  var lastLat = null;
  var lastLon = null;
  var locationTimeout = null;

  /* ─── Request location ─── */
  function requestLocation() {
    if (locationName) locationName.textContent = 'Loading...';
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'location', method: 'getLastKnown'
    }), '*');

    /* Timeout: if no response in 8 seconds */
    locationTimeout = setTimeout(function () {
      if (locationName && locationName.textContent === 'Loading...') {
        locationName.textContent = 'Location unavailable';
      }
    }, 8000);
  }

  requestLocation();

  /* ─── Message handler ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;
      if (msg.service === 'location' && msg.method === 'getLastKnown' && msg.data) {
        clearTimeout(locationTimeout);
        lastLat = msg.data.latitude;
        lastLon = msg.data.longitude;
        /* Show city name from location service if available */
        var cityName = msg.data.city || '';
        var region = msg.data.region || '';
        var displayName = cityName;
        if (region && region !== cityName) displayName += ', ' + region;
        if (!displayName) displayName = lastLat.toFixed(2) + ', ' + lastLon.toFixed(2);
        if (locationName) locationName.textContent = displayName;
        fetchWeather(lastLat, lastLon);
      }
    } catch (err) { /* ignore */ }
  });

  /* ─── Refresh button ─── */
  if (refreshBtn) {
    refreshBtn.addEventListener('click', function () {
      requestLocation();
    });
  }

  /* ─── Fetch weather from Open-Meteo ─── */
  function fetchWeather(lat, lon) {
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat + '&longitude=' + lon +
      '&current=temperature_2m,relative_humidity_2m,wind_speed_10m,surface_pressure,weather_code' +
      '&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=7';

    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.timeout = 10000;
    xhr.onreadystatechange = function () {
      if (xhr.readyState === 4) {
        if (xhr.status === 200) {
          try {
            var data = JSON.parse(xhr.responseText);
            renderCurrent(data);
            renderForecast(data);
          } catch (err) {
            showError('Weather data unavailable');
          }
        } else {
          showError('Weather service error');
        }
      }
    };
    xhr.onerror = function () { showError('Network error'); };
    xhr.ontimeout = function () { showError('Request timeout'); };
    xhr.send();
  }

  /* ─── Render current weather ─── */
  function renderCurrent(data) {
    var c = data.current || {};
    if (temperature) temperature.textContent = Math.round(c.temperature_2m || 0) + '\u00B0';
    if (condition) condition.textContent = weatherCodeToText(c.weather_code);
    if (humidity) humidity.textContent = (c.relative_humidity_2m || 0) + '%';
    if (wind) wind.textContent = (c.wind_speed_10m || 0) + ' m/s';
    if (pressure) pressure.textContent = Math.round(c.surface_pressure || 0) + ' hPa';
    if (weatherIcon) weatherIcon.textContent = weatherCodeToEmoji(c.weather_code);
  }

  /* ─── Render 7-day forecast ─── */
  function renderForecast(data) {
    if (!forecastEl || !data.daily) return;
    forecastEl.innerHTML = '';
    var days = data.daily;
    var count = Math.min(days.time.length, 7);
    var weekDays = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

    for (var i = 0; i < count; i++) {
      var date = new Date(days.time[i] + 'T12:00:00');
      var dayName = (i === 0) ? 'Today' : weekDays[date.getDay()];
      var maxTemp = Math.round(days.temperature_2m_max[i]);
      var minTemp = Math.round(days.temperature_2m_min[i]);
      var icon = weatherCodeToEmoji(days.weather_code[i]);

      var el = document.createElement('div');
      el.className = 'forecast-day';
      el.innerHTML =
        '<span class="forecast-name">' + dayName + '</span>' +
        '<span class="forecast-icon">' + icon + '</span>' +
        '<span class="forecast-temp">' + maxTemp + '\u00B0 / ' + minTemp + '\u00B0</span>';
      forecastEl.appendChild(el);
    }
  }

  function showError(msg) {
    if (condition) condition.textContent = msg;
  }

  /* ─── Weather code → text ─── */
  function weatherCodeToText(code) {
    if (code === 0) return 'Clear';
    if (code <= 3) return 'Partly Cloudy';
    if (code <= 49) return 'Foggy';
    if (code <= 59) return 'Drizzle';
    if (code <= 69) return 'Rain';
    if (code <= 79) return 'Snow';
    if (code <= 99) return 'Thunderstorm';
    return 'Unknown';
  }

  /* ─── Weather code → emoji ─── */
  function weatherCodeToEmoji(code) {
    if (code === 0) return '\u2600\uFE0F';
    if (code <= 3) return '\u26C5';
    if (code <= 49) return '\uD83C\uDF2B\uFE0F';
    if (code <= 59) return '\uD83C\uDF26\uFE0F';
    if (code <= 69) return '\uD83C\uDF27\uFE0F';
    if (code <= 79) return '\u2744\uFE0F';
    if (code <= 99) return '\u26C8\uFE0F';
    return '\u2753';
  }
})();
