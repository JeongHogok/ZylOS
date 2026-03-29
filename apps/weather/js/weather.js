// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 날씨 앱 — 현재 위치 기반 날씨 표시
// 수행범위: location 서비스에서 좌표 → 날씨 API 조회
// 의존방향: location 서비스, network (postMessage IPC)
// SOLID: SRP — 날씨 표시 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var locationName = document.getElementById('location-name');
  var temperature = document.getElementById('temperature');
  var condition = document.getElementById('condition');
  var humidity = document.getElementById('humidity');
  var wind = document.getElementById('wind');
  var pressure = document.getElementById('pressure');

  /* 위치 서비스에서 좌표 조회 */
  window.parent.postMessage(JSON.stringify({
    type: 'service.request', service: 'location', method: 'getLastKnown'
  }), '*');

  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;
      if (msg.service === 'location' && msg.method === 'getLastKnown' && msg.data) {
        fetchWeather(msg.data.latitude, msg.data.longitude);
      }
    } catch (err) {}
  });

  function fetchWeather(lat, lon) {
    /* Open-Meteo API (무료, 키 불필요) */
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat + '&longitude=' + lon +
      '&current=temperature_2m,relative_humidity_2m,wind_speed_10m,surface_pressure,weather_code';
    
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.onreadystatechange = function () {
      if (xhr.readyState === 4 && xhr.status === 200) {
        try {
          var data = JSON.parse(xhr.responseText);
          renderWeather(data, lat, lon);
        } catch (err) {
          if (locationName) locationName.textContent = 'Weather data unavailable';
        }
      }
    };
    xhr.onerror = function () {
      if (locationName) locationName.textContent = 'Network error';
    };
    xhr.send();
  }

  function renderWeather(data, lat, lon) {
    var c = data.current || {};
    if (locationName) locationName.textContent = lat.toFixed(2) + ', ' + lon.toFixed(2);
    if (temperature) temperature.textContent = Math.round(c.temperature_2m || 0) + '\u00b0';
    if (condition) condition.textContent = weatherCodeToText(c.weather_code);
    if (humidity) humidity.textContent = (c.relative_humidity_2m || 0) + '%';
    if (wind) wind.textContent = (c.wind_speed_10m || 0) + ' m/s';
    if (pressure) pressure.textContent = Math.round(c.surface_pressure || 0) + ' hPa';
  }

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
})();
