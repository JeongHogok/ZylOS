// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Wallpaper Domain Module
//
// 역할: 설정 앱 배경화면 도메인 — 배경화면 그리드 렌더링 및 선택
// 수행범위: 배경화면 옵션 표시, 선택 시 시스템 알림
// 의존방향: ZylSettingsCore (settings.js), ZylBridge (bridge.js)
// SOLID: SRP — 배경화면 설정 UI만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;

  /* Wallpaper color map for swatches */
  var WALLPAPER_COLORS = {
    'default':          'linear-gradient(135deg, #1a1a2e, #16213e)',
    'gradient-blue':    'linear-gradient(135deg, #0077b6, #00b4d8)',
    'gradient-purple':  'linear-gradient(135deg, #7b2ff7, #c471f5)',
    'gradient-dark':    'linear-gradient(135deg, #0d0d0d, #333333)',
    'gradient-sunset':  'linear-gradient(135deg, #f97316, #ef4444, #ec4899)'
  };

  /* ─── Render Wallpaper Grid ─── */
  function renderWallpaperGrid(data) {
    var grid = document.getElementById('wallpaper-grid');
    if (!grid || !data) return;
    grid.innerHTML = '';

    var options = data.options || [];
    var current = data.current || 'default';

    options.forEach(function (opt) {
      var swatch = document.createElement('div');
      swatch.className = 'wallpaper-swatch' + (opt === current ? ' selected' : '');
      swatch.style.cssText = 'width:100%;aspect-ratio:9/16;border-radius:12px;cursor:pointer;border:3px solid ' +
        (opt === current ? '#4a9eff' : 'transparent') +
        ';background:' + (WALLPAPER_COLORS[opt] || '#333') +
        ';display:flex;align-items:center;justify-content:center;font-size:11px;color:rgba(255,255,255,0.7);transition:border-color 0.2s;';
      swatch.textContent = opt === 'default' ? 'Default' : opt.replace('gradient-', '').charAt(0).toUpperCase() + opt.replace('gradient-', '').slice(1);
      swatch.dataset.wallpaper = opt;

      swatch.addEventListener('click', function () {
        core.updateSetting('wallpaper', 'current', opt);
        /* Update UI selection */
        grid.querySelectorAll('.wallpaper-swatch').forEach(function (s) {
          s.style.borderColor = 'transparent';
          s.classList.remove('selected');
        });
        swatch.style.borderColor = '#4a9eff';
        swatch.classList.add('selected');

        /* Broadcast wallpaper change to home screen via parent */
        ZylBridge.sendToSystem({
          type: 'settings.wallpaperChanged',
          wallpaper: opt
        });

        if (core.settingsCache.wallpaper) core.settingsCache.wallpaper.current = opt;
      });

      grid.appendChild(swatch);
    });
  }

  /* ─── Register with core ─── */
  core.handlers.wallpaper = {
    onSettingsGet: renderWallpaperGrid,
    onSettingsUpdated: null
  };

})();
