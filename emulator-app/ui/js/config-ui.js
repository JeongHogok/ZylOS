// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Widget
//
// 역할: 프리부팅 디바이스 설정 UI 로직
// 수행범위: 프로필 선택, 리소스 설정, 설정 확인, 부팅 트리거
// 의존방향: Tauri invoke (config 커맨드), boot-sequence.js
// SOLID: SRP — 설정 화면 로직만 담당, OCP — 프로필 추가 시 UI 자동 확장
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 에뮬레이터는 실제 디바이스 구동 환경 제공이 목적이며, OS 이미지 영역의 콘텐츠를 포함하지 않는다
// ──────────────────────────────────────────────────────────

/* global ZylConfigUI, ZylBootSequence */
/* eslint-disable no-unused-vars */

var ZylConfigUI = (function () {
  'use strict';

  var IS_TAURI = (typeof window !== 'undefined' && typeof window.__TAURI__ !== 'undefined');
  var _invoke = null;
  var _selectedProfile = null;
  var _selectedStorage = 8;
  var _selectedRam = 1024;
  var _selectedOsVersion = '0.1.0';
  var _selectedOsPath = ''; /* 선택된 OS 이미지 경로 (deploy-apps.sh에서 사용) */
  var _onBoot = null;

  function getInvoke() {
    if (_invoke) return _invoke;
    if (typeof window !== 'undefined' && window.__TAURI__ && window.__TAURI__.core) {
      _invoke = window.__TAURI__.core.invoke;
    }
    return _invoke;
  }

  function invoke(cmd, args) {
    var fn = getInvoke();
    if (!fn) return Promise.resolve(null);
    return fn(cmd, args || {});
  }

  function init(onBootCallback) {
    _onBoot = onBootCallback;
    loadProfiles();
    loadSavedDevices();
    loadHostResources();
    bindEvents();
  }

  // ── 프로필 로드 ──
  function loadProfiles() {
    // 즉시 기본 프로필 렌더링 (화면 빈칸 방지)
    renderProfiles(getDefaultProfiles());

    // Tauri 백엔드에서 프로필 조회 시도 (있으면 업데이트)
    var result = invoke('get_device_profiles');
    if (result && typeof result.then === 'function') {
      result.then(function (profiles) {
        if (profiles && profiles.length > 0) {
          renderProfiles(profiles);
        }
      }).catch(function () { /* 기본 프로필 유지 */ });
    }
  }

  function getDefaultProfiles() {
    return [
      { id: 'zyl-f3-gesture', name: 'BPI-F3 Gesture', description: 'iOS style gesture', soc: 'SpacemiT K1', screen_label: '1080x2400', nav_mode: 'Gesture', has_notch: true, frame_width: 393, frame_height: 852, frame_radius: 52, screen_width: 1080, screen_height: 2400, ram_options: [512, 1024, 2048, 4096] },
      { id: 'zyl-f3-softkeys', name: 'BPI-F3 Lite', description: '3-button softkeys', soc: 'SpacemiT K1', screen_label: '1080x2340', nav_mode: 'Softkeys', has_notch: false, frame_width: 393, frame_height: 852, frame_radius: 44, screen_width: 1080, screen_height: 2340, ram_options: [512, 1024, 2048] },
      { id: 'zyl-f3-hardware', name: 'BPI-F3 Classic', description: 'Hardware buttons', soc: 'SpacemiT K1', screen_label: '720x1280', nav_mode: 'Hardware', has_notch: false, frame_width: 380, frame_height: 820, frame_radius: 36, screen_width: 720, screen_height: 1280, ram_options: [512, 1024] }
    ];
  }

  function renderProfiles(profiles) {
    var container = document.getElementById('profile-cards');
    if (!container) return;
    container.innerHTML = '';

    profiles.forEach(function (p) {
      var card = document.createElement('div');
      card.className = 'device-card';
      card.setAttribute('data-profile-id', p.id);
      card.innerHTML =
        '<div class="device-card-name">' + escapeHtml(p.name) + '</div>' +
        '<div class="device-card-desc">' + escapeHtml(p.description) + '</div>' +
        '<div class="device-card-specs">' +
          '<span class="spec-tag">' + escapeHtml(p.soc) + '</span>' +
          '<span class="spec-tag">' + escapeHtml(p.screen_label) + '</span>' +
          '<span class="spec-tag">' + escapeHtml(String(p.nav_mode)) + '</span>' +
          (p.has_notch ? '<span class="spec-tag">Notch</span>' : '') +
        '</div>';

      card.addEventListener('click', function () {
        selectProfile(p);
        document.querySelectorAll('.device-card').forEach(function (c) { c.classList.remove('selected'); });
        card.classList.add('selected');
      });

      container.appendChild(card);
    });
  }

  function selectProfile(profile) {
    _selectedProfile = profile;
    showStep(2);
    renderResourceOptions(profile);
  }

  // ── 리소스 옵션 ──
  function renderResourceOptions(profile) {
    var osContainer = document.getElementById('os-version-options');
    var storageContainer = document.getElementById('storage-options');
    var ramContainer = document.getElementById('ram-options');
    if (!storageContainer || !ramContainer) return;

    // OS 이미지 선택 (Rust 백엔드에서 목록 조회)
    if (osContainer) {
      loadOsImages(osContainer);
    }

    var storageOpts = [
      { value: 4, label: '4 GB', description: 'Minimum' },
      { value: 8, label: '8 GB', description: 'Standard' },
      { value: 16, label: '16 GB', description: 'Recommended' },
      { value: 32, label: '32 GB', description: 'Large' }
    ];

    storageContainer.innerHTML = '';
    storageOpts.forEach(function (opt) {
      var btn = document.createElement('button');
      btn.className = 'option-btn' + (opt.value === _selectedStorage ? ' selected' : '');
      btn.innerHTML = '<span class="option-btn-label">' + opt.label + '</span><span class="option-btn-desc">' + opt.description + '</span>';
      btn.addEventListener('click', function () {
        _selectedStorage = opt.value;
        storageContainer.querySelectorAll('.option-btn').forEach(function (b) { b.classList.remove('selected'); });
        btn.classList.add('selected');
      });
      storageContainer.appendChild(btn);
    });

    var ramOpts = (profile.ram_options || [1024, 2048]).map(function (v) {
      return { value: v, label: v >= 1024 ? (v / 1024) + ' GB' : v + ' MB' };
    });

    ramContainer.innerHTML = '';
    ramOpts.forEach(function (opt) {
      var btn = document.createElement('button');
      btn.className = 'option-btn' + (opt.value === _selectedRam ? ' selected' : '');
      btn.innerHTML = '<span class="option-btn-label">' + opt.label + '</span>';
      btn.addEventListener('click', function () {
        _selectedRam = opt.value;
        ramContainer.querySelectorAll('.option-btn').forEach(function (b) { b.classList.remove('selected'); });
        btn.classList.add('selected');
      });
      ramContainer.appendChild(btn);
    });
  }

  // ── 호스트 리소스 ──
  function loadHostResources() {
    invoke('get_host_resources').then(function (info) {
      if (!info) return;
      var el = document.getElementById('host-info');
      if (!el) return;
      var parts = [];
      if (info.available_storage_gb) parts.push('Available storage: ' + info.available_storage_gb + ' GB');
      if (info.total_ram_mb) parts.push('Host RAM: ' + Math.round(info.total_ram_mb / 1024) + ' GB');
      el.textContent = parts.join(' | ') || 'Host resource info unavailable';
    }).catch(function () { /* host info unavailable — non-critical */ });
  }

  // ── 저장된 디바이스 ──
  function loadSavedDevices() {
    invoke('get_saved_devices').then(function (devices) {
      if (!devices || devices.length === 0) return;
      var section = document.getElementById('saved-devices-section');
      var list = document.getElementById('saved-devices-list');
      if (!section || !list) return;

      section.classList.remove('hidden');
      list.innerHTML = '';

      devices.forEach(function (d) {
        var card = document.createElement('div');
        card.className = 'device-card';
        var c = d.config;
        card.innerHTML =
          '<div class="device-card-name">' + escapeHtml(c.name) + '</div>' +
          '<div class="device-card-specs">' +
            '<span class="spec-tag">' + c.storage_gb + ' GB</span>' +
            '<span class="spec-tag">' + (c.ram_mb >= 1024 ? (c.ram_mb / 1024) + ' GB RAM' : c.ram_mb + ' MB RAM') + '</span>' +
            '<span class="spec-tag">' + escapeHtml(String(c.nav_mode)) + '</span>' +
          '</div>' +
          '<div class="saved-card-actions">' +
            '<button class="saved-card-boot">Boot</button>' +
            '<button class="saved-card-delete">Delete</button>' +
          '</div>';

        card.querySelector('.saved-card-boot').addEventListener('click', function (e) {
          e.stopPropagation();
          bootWithConfig(c);
        });

        card.querySelector('.saved-card-delete').addEventListener('click', function (e) {
          e.stopPropagation();
          invoke('delete_saved_device', { profile_id: c.profile_id }).then(function () {
            card.remove();
            if (list.children.length === 0) section.classList.add('hidden');
          });
        });

        list.appendChild(card);
      });
    }).catch(function () { /* saved devices unavailable — non-critical */ });
  }

  // ── 단계 전환 ──
  function showStep(step) {
    document.getElementById('config-step-1').classList.toggle('hidden', step !== 1);
    document.getElementById('config-step-2').classList.toggle('hidden', step !== 2);
    document.getElementById('config-step-3').classList.toggle('hidden', step !== 3);

    if (step === 3) {
      renderSummary();
    }
  }

  function renderSummary() {
    var el = document.getElementById('config-summary');
    if (!el || !_selectedProfile) return;

    var navLabel = String(_selectedProfile.nav_mode);
    var ramLabel = _selectedRam >= 1024 ? (_selectedRam / 1024) + ' GB' : _selectedRam + ' MB';

    el.innerHTML =
      '<div class="summary-row"><span class="summary-label">Device</span><span class="summary-value">' + escapeHtml(_selectedProfile.name) + '</span></div>' +
      '<div class="summary-row"><span class="summary-label">Screen</span><span class="summary-value">' + escapeHtml(_selectedProfile.screen_label) + '</span></div>' +
      '<div class="summary-row"><span class="summary-label">Navigation</span><span class="summary-value">' + escapeHtml(navLabel) + '</span></div>' +
      '<div class="summary-row"><span class="summary-label">Storage</span><span class="summary-value">' + _selectedStorage + ' GB</span></div>' +
      '<div class="summary-row"><span class="summary-label">RAM</span><span class="summary-value">' + ramLabel + '</span></div>' +
      '<div class="summary-row"><span class="summary-label">OS Image</span><span class="summary-value">v' + escapeHtml(_selectedOsVersion) + '</span></div>';
  }

  // ── 부팅 ──
  function buildConfig() {
    if (!_selectedProfile) return null;
    return {
      profile_id: _selectedProfile.id,
      name: _selectedProfile.name,
      screen_width: _selectedProfile.screen_width,
      screen_height: _selectedProfile.screen_height,
      frame_width: _selectedProfile.frame_width,
      frame_height: _selectedProfile.frame_height,
      frame_radius: _selectedProfile.frame_radius,
      nav_mode: _selectedProfile.nav_mode,
      has_notch: _selectedProfile.has_notch,
      storage_gb: _selectedStorage,
      ram_mb: _selectedRam,
      os_version: _selectedOsVersion,
      soc: _selectedProfile.soc
    };
  }

  function bootWithConfig(config) {
    if (_onBoot) {
      _onBoot(config);
    }
  }

  function startBoot() {
    var config = buildConfig();
    if (!config) return;
    bootWithConfig(config);
  }

  // ── 이벤트 바인딩 ──
  function bindEvents() {
    var backStep1 = document.getElementById('btn-back-step1');
    var nextStep3 = document.getElementById('btn-next-step3');
    var backStep2 = document.getElementById('btn-back-step2');
    var bootBtn = document.getElementById('btn-boot');

    if (backStep1) backStep1.addEventListener('click', function () { showStep(1); });
    if (nextStep3) nextStep3.addEventListener('click', function () { showStep(3); });
    if (backStep2) backStep2.addEventListener('click', function () { showStep(2); });
    if (bootBtn) bootBtn.addEventListener('click', startBoot);
  }

  // ── OS 이미지 관리 ──

  var _currentImageDir = '';

  function loadOsImages(container) {
    /* 기본 경로 조회 후 렌더링 */
    invoke('get_os_images_dir').then(function (dir) {
      _currentImageDir = dir || '';
      return invoke('list_os_images');
    }).then(function (images) {
      renderOsImagePanel(container, images || []);
    }).catch(function () {
      _currentImageDir = '';
      renderOsImagePanel(container, []);
    });
  }

  function renderOsImagePanel(container, images) {
    container.innerHTML = '';

    /* ── 경로 표시줄 (항상 표시, 세로 배치) ── */
    var pathBar = document.createElement('div');
    pathBar.style.cssText = 'margin-bottom:14px;padding:12px 14px;background:#111118;border:1px solid #252535;border-radius:10px';

    var pathTop = document.createElement('div');
    pathTop.style.cssText = 'display:flex;align-items:center;justify-content:space-between;margin-bottom:4px';

    var pathTitle = document.createElement('span');
    pathTitle.style.cssText = 'font-size:11px;color:#666;text-transform:uppercase;letter-spacing:0.5px';
    pathTitle.textContent = 'SCAN PATH';

    var changeBtn = document.createElement('button');
    changeBtn.style.cssText = 'background:#252535;border:1px solid #333;color:#a0a0f0;padding:4px 12px;border-radius:6px;font-size:11px;font-weight:600;cursor:pointer';
    changeBtn.textContent = 'Change';
    changeBtn.addEventListener('click', function () {
      changeScanDirectory(container);
    });

    pathTop.appendChild(pathTitle);
    pathTop.appendChild(changeBtn);
    pathBar.appendChild(pathTop);

    var pathValue = document.createElement('div');
    pathValue.style.cssText = 'font-size:12px;color:#999;word-break:break-all;line-height:1.4';
    pathValue.textContent = _currentImageDir || 'Set image directory path';
    pathBar.appendChild(pathValue);

    container.appendChild(pathBar);

    /* ── 이미지 목록 ── */
    if (images.length > 0) {
      var listDiv = document.createElement('div');
      listDiv.style.cssText = 'display:flex;flex-wrap:wrap;gap:10px';

      images.forEach(function (img) {
        var btn = document.createElement('button');
        btn.className = 'option-btn' + (img.version === _selectedOsVersion ? ' selected' : '');
        var sizeText = img.size_bytes ? ' (' + formatBytes(img.size_bytes) + ')' : '';
        btn.innerHTML =
          '<span class="option-btn-label">' + escapeHtml(img.label) + '</span>' +
          '<span class="option-btn-desc">' + escapeHtml(img.description) + sizeText + '</span>';
        btn.addEventListener('click', function () {
          _selectedOsVersion = img.version;
          _selectedOsPath = img.path || '';
          listDiv.querySelectorAll('.option-btn').forEach(function (b) { b.classList.remove('selected'); });
          btn.classList.add('selected');
        });
        listDiv.appendChild(btn);
      });

      container.appendChild(listDiv);
    } else {
      var empty = document.createElement('div');
      empty.style.cssText = 'color:#555;font-size:13px;padding:12px 0';
      empty.textContent = 'No OS images found in this directory';
      container.appendChild(empty);
    }
  }

  function changeScanDirectory(container) {
    if (IS_TAURI && window.__TAURI__ && window.__TAURI__.dialog) {
      window.__TAURI__.dialog.open({
        directory: true,
        multiple: false,
        title: 'Select OS Image Folder',
        defaultPath: _currentImageDir || undefined
      }).then(function (selected) {
        if (!selected) return;
        _currentImageDir = selected;
        scanDirectory(container, selected);
      }).catch(function () {
        showManualPathInput(container);
      });
    } else {
      showManualPathInput(container);
    }
  }

  function scanDirectory(container, dirPath) {
    invoke('scan_os_images_dir', { dirPath: dirPath }).then(function (images) {
      renderOsImagePanel(container, images || []);
    }).catch(function (err) {
      renderOsImagePanel(container, []);
      var errEl = document.createElement('div');
      errEl.style.cssText = 'color:#ef4444;font-size:12px;padding:6px 0';
      errEl.textContent = 'Scan failed: ' + (err || 'unknown error');
      container.appendChild(errEl);
      setTimeout(function () { if (errEl.parentNode) errEl.remove(); }, 4000);
    });
  }

  function showManualPathInput(container) {
    /* 이미 입력창이 있으면 중복 생성 안 함 */
    if (container.querySelector('.manual-path-input')) return;

    var wrapper = document.createElement('div');
    wrapper.className = 'manual-path-input';
    wrapper.style.cssText = 'display:flex;gap:8px;margin-top:8px;align-items:center';

    var input = document.createElement('input');
    input.type = 'text';
    input.value = _currentImageDir || '';
    input.placeholder = '/path/to/os-images';
    input.style.cssText = 'flex:1;padding:8px 12px;background:#0f0f1a;border:1px solid #333;border-radius:8px;color:#fff;font-size:13px';

    var scanBtn = document.createElement('button');
    scanBtn.className = 'btn btn-primary';
    scanBtn.style.cssText = 'padding:8px 16px;font-size:13px';
    scanBtn.textContent = 'Scan';
    scanBtn.addEventListener('click', function () {
      var path = input.value.trim();
      if (path) {
        _currentImageDir = path;
        wrapper.remove();
        scanDirectory(container, path);
      }
    });

    input.addEventListener('keydown', function (e) {
      if (e.key === 'Enter') scanBtn.click();
    });

    wrapper.appendChild(input);
    wrapper.appendChild(scanBtn);
    container.appendChild(wrapper);
    input.focus();
  }

  function formatBytes(bytes) {
    if (!bytes) return '0 B';
    var units = ['B', 'KB', 'MB', 'GB'];
    var i = Math.floor(Math.log(bytes) / Math.log(1024));
    if (i >= units.length) i = units.length - 1;
    return (bytes / Math.pow(1024, i)).toFixed(1) + ' ' + units[i];
  }

  // ── Helpers ──
  function escapeHtml(str) {
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(str || ''));
    return div.innerHTML;
  }

  return {
    init: init
  };
})();
