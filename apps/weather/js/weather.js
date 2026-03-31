// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Weather app — Samsung-grade UI with current, hourly, daily forecast + multi-city
// Scope: Location service → Open-Meteo API → render weather data with dynamic backgrounds, multi-city management
// Dependency: location service (postMessage IPC), network service (Open-Meteo API), settings service
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
  var cityIndicator = document.getElementById('city-indicator');

  /* City management DOM */
  var btnManageCities = document.getElementById('btn-manage-cities');
  var cityPanel = document.getElementById('city-panel');
  var btnCityBack = document.getElementById('btn-city-back');
  var savedCitiesList = document.getElementById('saved-cities-list');
  var cityAddInput = document.getElementById('city-add-input');
  var citySearchResults = document.getElementById('city-search-results');
  var weatherScroll = document.getElementById('weather-scroll');

  /* ─── State ─── */
  var lastLat = null;
  var lastLon = null;
  var locationTimeout = null;
  var isRefreshing = false;

  /* Multi-city state */
  var savedCities = []; /* Array of { name, lat, lon } */
  var currentCityIndex = -1; /* -1 = current location */
  var currentLocationCity = null; /* { name, lat, lon } from device location */

  /* Well-known cities for search */
  var CITY_DB = [
    { name: 'Seoul', lat: 37.5665, lon: 126.978 },
    { name: 'Tokyo', lat: 35.6762, lon: 139.6503 },
    { name: 'New York', lat: 40.7128, lon: -74.006 },
    { name: 'London', lat: 51.5074, lon: -0.1278 },
    { name: 'Paris', lat: 48.8566, lon: 2.3522 },
    { name: 'Berlin', lat: 52.52, lon: 13.405 },
    { name: 'Moscow', lat: 55.7558, lon: 37.6173 },
    { name: 'Beijing', lat: 39.9042, lon: 116.4074 },
    { name: 'Shanghai', lat: 31.2304, lon: 121.4737 },
    { name: 'Hong Kong', lat: 22.3193, lon: 114.1694 },
    { name: 'Singapore', lat: 1.3521, lon: 103.8198 },
    { name: 'Sydney', lat: -33.8688, lon: 151.2093 },
    { name: 'Dubai', lat: 25.2048, lon: 55.2708 },
    { name: 'Mumbai', lat: 19.076, lon: 72.8777 },
    { name: 'Bangkok', lat: 13.7563, lon: 100.5018 },
    { name: 'Los Angeles', lat: 34.0522, lon: -118.2437 },
    { name: 'Chicago', lat: 41.8781, lon: -87.6298 },
    { name: 'Toronto', lat: 43.6532, lon: -79.3832 },
    { name: 'Mexico City', lat: 19.4326, lon: -99.1332 },
    { name: 'Sao Paulo', lat: -23.5505, lon: -46.6333 },
    { name: 'Cairo', lat: 30.0444, lon: 31.2357 },
    { name: 'Istanbul', lat: 41.0082, lon: 28.9784 },
    { name: 'Rome', lat: 41.9028, lon: 12.4964 },
    { name: 'Madrid', lat: 40.4168, lon: -3.7038 },
    { name: 'Osaka', lat: 34.6937, lon: 135.5023 },
    { name: 'Jakarta', lat: -6.2088, lon: 106.8456 },
    { name: 'Taipei', lat: 25.033, lon: 121.5654 },
    { name: 'Buenos Aires', lat: -34.6037, lon: -58.3816 },
    { name: 'Amsterdam', lat: 52.3676, lon: 4.9041 },
    { name: 'Auckland', lat: -36.8485, lon: 174.7633 }
  ];

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

  /* ─── Settings persistence ─── */
  function saveCities() {
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'settings', method: 'update',
      params: { category: 'weather', key: 'cities', value: JSON.stringify(savedCities) }
    });
  }

  function loadCities() {
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'settings', method: 'get',
      params: { category: 'weather' }
    });
  }

  /* ─── Init ─── */
  setRefreshing(true);
  requestLocation();
  loadCities();

  /* ─── Message handler ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      if (msg.type === 'navigation.back') {
        if (cityPanel && !cityPanel.classList.contains('hidden')) {
          closeCityPanel();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
        } else {
          ZylBridge.sendToSystem({ type: 'navigation.exit' });
        }
        return;
      }

      if (msg.type !== 'service.response') return;

      /* Settings response */
      if (msg.service === 'settings' && msg.method === 'get' && msg.params && msg.params.category === 'weather') {
        if (msg.data && msg.data.cities) {
          try {
            var parsed = JSON.parse(msg.data.cities);
            if (Array.isArray(parsed)) {
              savedCities = parsed;
              updateCityIndicator();
            }
          } catch (err) { /* ignore */ }
        }
        return;
      }

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
        if (!displayName && lastLat != null && lastLon != null) displayName = lastLat.toFixed(2) + ', ' + lastLon.toFixed(2);

        currentLocationCity = { name: displayName, lat: lastLat, lon: lastLon };

        if (currentCityIndex === -1) {
          if (locationName) {
            locationName.textContent = displayName;
            locationName.removeAttribute('data-loading');
          }
          fetchWeather(lastLat, lastLon);
        }
        updateCityIndicator();
      }
    } catch (err) { /* ignore */ }
  });

  /* ─── Refresh button ─── */
  if (refreshBtn) {
    refreshBtn.addEventListener('click', function () {
      if (isRefreshing) return;
      setRefreshing(true);
      if (currentCityIndex === -1) {
        requestLocation();
      } else {
        var city = savedCities[currentCityIndex];
        if (city) {
          fetchWeather(city.lat, city.lon);
        }
      }
    });
  }

  /* ─── City switching via swipe/indicator ─── */
  function switchToCity(index) {
    /* index: -1 = current location, 0+ = saved city */
    currentCityIndex = index;
    updateCityIndicator();

    if (index === -1) {
      if (currentLocationCity) {
        if (locationName) locationName.textContent = currentLocationCity.name;
        setRefreshing(true);
        fetchWeather(currentLocationCity.lat, currentLocationCity.lon);
      } else {
        setRefreshing(true);
        requestLocation();
      }
    } else {
      var city = savedCities[index];
      if (city) {
        if (locationName) locationName.textContent = city.name;
        setRefreshing(true);
        fetchWeather(city.lat, city.lon);
      }
    }
  }

  function updateCityIndicator() {
    if (!cityIndicator) return;
    var totalDots = 1 + savedCities.length; /* current location + saved */
    if (totalDots <= 1) {
      cityIndicator.innerHTML = '';
      return;
    }
    var html = '';
    for (var i = -1; i < savedCities.length; i++) {
      var activeClass = (i === currentCityIndex) ? ' dot-active' : '';
      html += '<span class="city-dot' + activeClass + '" data-city-idx="' + i + '"></span>';
    }
    cityIndicator.innerHTML = html;
  }

  if (cityIndicator) {
    cityIndicator.addEventListener('click', function (e) {
      var dot = e.target.closest('.city-dot');
      if (!dot) return;
      var idx = parseInt(dot.getAttribute('data-city-idx'), 10);
      if (!isNaN(idx)) switchToCity(idx);
    });
  }

  /* ─── City Management Panel ─── */
  if (btnManageCities) {
    btnManageCities.addEventListener('click', function () {
      openCityPanel();
    });
  }

  if (btnCityBack) {
    btnCityBack.addEventListener('click', function () {
      closeCityPanel();
    });
  }

  function openCityPanel() {
    if (cityPanel) cityPanel.classList.remove('hidden');
    if (weatherScroll) weatherScroll.classList.add('hidden');
    if (cityIndicator) cityIndicator.classList.add('hidden');
    renderSavedCities();
    if (cityAddInput) { cityAddInput.value = ''; }
    if (citySearchResults) citySearchResults.innerHTML = '';
    renderCitySearchResults('');
  }

  function closeCityPanel() {
    if (cityPanel) cityPanel.classList.add('hidden');
    if (weatherScroll) weatherScroll.classList.remove('hidden');
    if (cityIndicator) cityIndicator.classList.remove('hidden');
  }

  function renderSavedCities() {
    if (!savedCitiesList) return;
    savedCitiesList.innerHTML = '';

    /* Current location (non-deletable) */
    var locItem = document.createElement('div');
    locItem.className = 'saved-city-item';
    if (currentCityIndex === -1) locItem.classList.add('active');
    var locName = document.createElement('span');
    locName.className = 'saved-city-name';
    locName.textContent = (currentLocationCity ? currentLocationCity.name : t('weather.current_location')) + ' (' + t('weather.current_location') + ')';
    locItem.appendChild(locName);
    locItem.addEventListener('click', function () {
      switchToCity(-1);
      closeCityPanel();
    });
    savedCitiesList.appendChild(locItem);

    for (var i = 0; i < savedCities.length; i++) {
      (function (city, idx) {
        var item = document.createElement('div');
        item.className = 'saved-city-item';
        if (idx === currentCityIndex) item.classList.add('active');

        var nameEl = document.createElement('span');
        nameEl.className = 'saved-city-name';
        nameEl.textContent = city.name;

        var delBtn = document.createElement('button');
        delBtn.className = 'saved-city-delete';
        delBtn.textContent = '\u00D7';
        delBtn.addEventListener('click', function (e) {
          e.stopPropagation();
          savedCities.splice(idx, 1);
          saveCities();
          if (currentCityIndex === idx) {
            switchToCity(-1);
          } else if (currentCityIndex > idx) {
            currentCityIndex--;
          }
          renderSavedCities();
          updateCityIndicator();
        });

        item.appendChild(nameEl);
        item.appendChild(delBtn);
        item.addEventListener('click', function () {
          switchToCity(idx);
          closeCityPanel();
        });
        savedCitiesList.appendChild(item);
      })(savedCities[i], i);
    }
  }

  /* City search */
  if (cityAddInput) {
    cityAddInput.addEventListener('input', function () {
      renderCitySearchResults(cityAddInput.value.trim().toLowerCase());
    });
  }

  function renderCitySearchResults(query) {
    if (!citySearchResults) return;
    citySearchResults.innerHTML = '';
    if (!query) return;

    var existingNames = {};
    for (var e = 0; e < savedCities.length; e++) {
      existingNames[savedCities[e].name.toLowerCase()] = true;
    }

    for (var i = 0; i < CITY_DB.length; i++) {
      var city = CITY_DB[i];
      if (existingNames[city.name.toLowerCase()]) continue;
      if (city.name.toLowerCase().indexOf(query) === -1) continue;

      (function (c) {
        var el = document.createElement('div');
        el.className = 'city-search-item';
        el.textContent = c.name;
        el.addEventListener('click', function () {
          savedCities.push({ name: c.name, lat: c.lat, lon: c.lon });
          saveCities();
          updateCityIndicator();
          renderSavedCities();
          if (cityAddInput) cityAddInput.value = '';
          citySearchResults.innerHTML = '';
        });
        citySearchResults.appendChild(el);
      })(city);
    }
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
