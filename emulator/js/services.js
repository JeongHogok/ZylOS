// ──────────────────────────────────────────────────────────
// [Clean Architecture] Infrastructure Layer - Service
//
// 역할: Zyl OS 중앙 시스템 서비스 — 모든 앱에 데이터 제공
// 수행범위: 파일시스템, WiFi, 블루투스, 디바이스정보, 저장공간, 앱목록, 브라우저
// 의존방향: emulator.js에서 로드 → postMessage IPC로 앱에 배포
// SOLID: SRP — 데이터 정의 및 서비스 메서드만 담당, OCP — 서비스 추가 확장 가능
// ──────────────────────────────────────────────────────────

var ZylServices = (function () {
  'use strict';

  /* ═══════════════════════════════════════════════════════
     1. FileSystemService — 가상 파일시스템
     ═══════════════════════════════════════════════════════ */
  var fs = {
    tree: {
      '/': [
        { name: 'Documents', type: 'folder', date: '2026-03-27', size: null },
        { name: 'Downloads', type: 'folder', date: '2026-03-28', size: null },
        { name: 'Pictures', type: 'folder', date: '2026-03-25', size: null },
        { name: 'Music', type: 'folder', date: '2026-03-20', size: null },
        { name: 'Videos', type: 'folder', date: '2026-03-15', size: null },
        { name: 'readme.txt', type: 'document', date: '2026-03-28', size: 2400 },
        { name: 'system.log', type: 'code', date: '2026-03-28', size: 15360 }
      ],
      '/Documents': [
        { name: 'Work', type: 'folder', date: '2026-03-26', size: null },
        { name: 'report_2026.pdf', type: 'document', date: '2026-03-26', size: 1258291 },
        { name: 'notes.md', type: 'code', date: '2026-03-27', size: 4096 },
        { name: 'budget.xlsx', type: 'document', date: '2026-03-20', size: 52480 },
        { name: 'presentation.pptx', type: 'document', date: '2026-03-18', size: 3145728 }
      ],
      '/Downloads': [
        { name: 'zylos-image-v0.1.img.gz', type: 'archive', date: '2026-03-28', size: 524288000 },
        { name: 'linux-6.6.63.tar.xz', type: 'archive', date: '2026-03-25', size: 142606336 },
        { name: 'wallpaper.jpg', type: 'image', date: '2026-03-27', size: 2097152 },
        { name: 'setup.sh', type: 'code', date: '2026-03-24', size: 8192 }
      ],
      '/Pictures': [
        { name: 'Screenshots', type: 'folder', date: '2026-03-28', size: null },
        { name: 'photo_001.jpg', type: 'image', date: '2026-03-27', size: 3145728 },
        { name: 'photo_002.jpg', type: 'image', date: '2026-03-26', size: 2621440 },
        { name: 'avatar.png', type: 'image', date: '2026-03-20', size: 524288 },
        { name: 'banner.svg', type: 'image', date: '2026-03-15', size: 12288 }
      ],
      '/Music': [
        { name: 'playlist_01.mp3', type: 'audio', date: '2026-03-10', size: 5242880 },
        { name: 'podcast_ep12.m4a', type: 'audio', date: '2026-03-22', size: 31457280 },
        { name: 'ringtone.ogg', type: 'audio', date: '2026-02-15', size: 204800 }
      ],
      '/Videos': [
        { name: 'demo_riscv.mp4', type: 'video', date: '2026-03-18', size: 157286400 },
        { name: 'screen_record_01.webm', type: 'video', date: '2026-03-25', size: 52428800 }
      ],
      '/Documents/Work': [],
      '/Pictures/Screenshots': []
    },

    /* 터미널용 UNIX 파일시스템 뷰 (ls용 배열) */
    unixTree: {
      '/': ['bin', 'boot', 'dev', 'etc', 'home', 'lib', 'mnt', 'opt', 'proc', 'root', 'run', 'sbin', 'sys', 'tmp', 'usr', 'var'],
      '/home/user': ['Documents', 'Downloads', 'Pictures', 'Music', '.bashrc', '.profile', 'readme.txt'],
      '/home/user/Documents': ['report.pdf', 'notes.md', 'project/', 'budget.xlsx'],
      '/home/user/Downloads': ['zylos-v0.1.img.gz', 'linux-6.6.63.tar.xz'],
      '/home/user/Pictures': ['photo_001.jpg', 'photo_002.jpg', 'screenshots/'],
      '/home/user/Music': ['playlist.mp3', 'podcast.m4a']
    },

    /* cat 명령용 파일 내용 */
    fileContents: {
      '/home/user/readme.txt': 'Welcome to Zyl OS!\n\nThis is a Linux-based mobile operating system\ndesigned for Banana Pi RISC-V boards.\n\nVisit: https://zylos.dev',
      '/home/user/.bashrc': '# ~/.bashrc\nexport PATH=$HOME/bin:$PATH\nexport EDITOR=nano\nalias ll="ls -la"\nalias ..="cd .."\n\n# Zyl OS environment\nexport BPIOS_VERSION=0.1.0',
      '/home/user/.profile': '# ~/.profile\n[ -f ~/.bashrc ] && . ~/.bashrc',
      '/home/user/Documents/notes.md': '# Project Notes\n\n## Zyl OS Development\n- UI framework complete\n- Browser app in progress\n- Terminal emulator done\n\n## TODO\n- [ ] Camera app\n- [ ] File manager polish\n- [x] Settings app'
    },

    /* 서비스 메서드 */
    getDirectory: function (path) {
      return fs.tree[path] || null;
    },
    getUnixDirectory: function (path) {
      return fs.unixTree[path] || null;
    },
    getFileContent: function (path) {
      return fs.fileContents[path] || null;
    },
    getAllData: function () {
      return { tree: fs.tree, unixTree: fs.unixTree, fileContents: fs.fileContents };
    }
  };


  /* ═══════════════════════════════════════════════════════
     2. WiFiService — 네트워크 목록
     ═══════════════════════════════════════════════════════ */
  var wifi = {
    /* HAL에서 호스트 네트워크 정보를 가져와 목록 구성 */
    getNetworks: function () {
      if (typeof ZylHalBrowser !== 'undefined') {
        var info = ZylHalBrowser.network.getWifiInfo();
        return info.networks;
      }
      return [];
    },
    getConnected: function () {
      var nets = wifi.getNetworks();
      return nets.find(function (n) { return n.connected; }) || null;
    }
  };


  /* ═══════════════════════════════════════════════════════
     3. BluetoothService — 페어링/사용 가능 디바이스
     ═══════════════════════════════════════════════════════ */
  var bluetooth = {
    /* HAL에서 BT 상태를 가져옴. Web Bluetooth API는 제한적이므로 지원 여부만 조회 */
    getDevices: function () {
      if (typeof ZylHalBrowser !== 'undefined') {
        var state = ZylHalBrowser.bluetooth.getState();
        return state.devices || [];
      }
      return [];
    },
    getPaired: function () {
      return bluetooth.getDevices().filter(function (d) { return d.paired; });
    },
    getConnected: function () {
      return bluetooth.getDevices().filter(function (d) { return d.connected; });
    },
    isSupported: function () {
      return typeof ZylHalBrowser !== 'undefined' && ZylHalBrowser.bluetooth.isSupported();
    }
  };


  /* ═══════════════════════════════════════════════════════
     4. DeviceInfoService — 디바이스 하드웨어/소프트웨어 정보
     부팅 시 선택한 프로필로 갱신된다
     ═══════════════════════════════════════════════════════ */
  var deviceInfo = {
    deviceName: 'BPI-F3',
    osVersion: 'Zyl OS 0.1.0',
    soc: 'SpacemiT K1 (RISC-V)',
    ram: '16 GB LPDDR4X',
    kernel: 'Linux 6.6.63',
    build: 'Zyl OS.20260328.dev',
    resolution: '1080x2400',
    gpu: 'IMG BXE-2-32 (PVR)',
    shell: 'bash 5.2.26',
    hostname: 'bpi-f3',
    username: 'user',

    getInfo: function () {
      /* HAL에서 호스트 정보 보강 */
      var info = {
        deviceName: deviceInfo.deviceName,
        osVersion: deviceInfo.osVersion,
        soc: deviceInfo.soc,
        ram: deviceInfo.ram,
        kernel: deviceInfo.kernel,
        build: deviceInfo.build,
        resolution: deviceInfo.resolution,
        gpu: deviceInfo.gpu,
        shell: deviceInfo.shell,
        hostname: deviceInfo.hostname,
        username: deviceInfo.username,
      };
      if (typeof ZylHalBrowser !== 'undefined') {
        var halInfo = ZylHalBrowser.deviceInfo.getInfo();
        info.hostPlatform = halInfo.hostPlatform;
        info.hostLanguage = halInfo.hostLanguage;
      }
      return info;
    },

    /* 부팅 시 디바이스 프로필로 갱신 */
    applyProfile: function (profile) {
      if (!profile) return;
      deviceInfo.deviceName = profile.name || deviceInfo.deviceName;
      deviceInfo.soc = profile.soc ? profile.soc + ' (RISC-V)' : deviceInfo.soc;
      deviceInfo.ram = profile.ram ? profile.ram + ' LPDDR4X' : deviceInfo.ram;
      deviceInfo.resolution = profile.screen || deviceInfo.resolution;
      /* hostname은 기종 id에서 추출 */
      if (profile.id) {
        deviceInfo.hostname = profile.id.replace(/^zyl-/, '').replace(/-/g, '_');
      }
      /* HAL에도 프로필 전달 */
      if (typeof ZylHalBrowser !== 'undefined') {
        ZylHalBrowser.deviceInfo.applyProfile(profile);
      }
      /* 스토리지 캐시 초기화 (부팅 시 새로 조회) */
      if (typeof storage !== 'undefined') storage._fetchFromHal();
    }
  };


  /* ═══════════════════════════════════════════════════════
     5. StorageService — 디스크 사용량
     ═══════════════════════════════════════════════════════ */
  var storage = {
    /* HAL에서 호스트의 실제 스토리지 정보를 가져옴 */
    _cache: null,
    _cacheTime: 0,

    _fetchFromHal: function () {
      if (typeof ZylHalBrowser !== 'undefined') {
        return ZylHalBrowser.storage.getState().then(function (s) {
          storage._cache = s;
          storage._cacheTime = Date.now();
          return s;
        });
      }
      return Promise.resolve({ total: 0, used: 0, available: 0 });
    },

    getUsage: function () {
      if (storage._cache && Date.now() - storage._cacheTime < 30000) {
        return storage._cache;
      }
      /* 캐시가 없으면 비동기 프리페치 시작하고 현재 캐시(또는 기본값) 반환 */
      /* 다음 호출 시 캐시에서 실제 값을 받을 수 있음 */
      storage._fetchFromHal();
      return storage._cache || { total: 0, used: 0, available: 0 };
    },

    /* 서비스 초기화 시 호출 — 부팅 시 캐시를 미리 채움 */
    prefetch: function () {
      storage._fetchFromHal();
    },

    getFormatted: function () {
      var fmt = (typeof ZylHalBrowser !== 'undefined') ? ZylHalBrowser.storage.formatBytes : function (b) { return b + ' B'; };
      var s = storage.getUsage();
      return {
        total: fmt(s.total),
        used: fmt(s.used),
        available: fmt(s.available),
      };
    },
  };


  /* ═══════════════════════════════════════════════════════
     6. AppRegistryService — 설치된 앱 목록
     ═══════════════════════════════════════════════════════ */
  var apps = {
    installed: [
      { id: 'com.zylos.camera',   nameKey: 'app.camera',   icon: 'camera',   color: 'icon-red',     version: '1.0.0' },
      { id: 'com.zylos.gallery',  nameKey: 'app.gallery',  icon: 'gallery',  color: 'icon-pink',    version: '1.0.0' },
      { id: 'com.zylos.music',    nameKey: 'app.music',    icon: 'music',    color: 'icon-red',     version: '1.0.0' },
      { id: 'com.zylos.clock',    nameKey: 'app.clock',    icon: 'clock',    color: 'icon-indigo',  version: '1.0.0' },
      { id: 'com.zylos.calc',     nameKey: 'app.calc',     icon: 'calc',     color: 'icon-orange',  version: '1.0.0' },
      { id: 'com.zylos.notes',    nameKey: 'app.notes',    icon: 'notes',    color: 'icon-amber',   version: '1.0.0' },
      { id: 'com.zylos.weather',  nameKey: 'app.weather',  icon: 'weather',  color: 'icon-cyan',    version: '1.0.0' },
      { id: 'com.zylos.store',    nameKey: 'app.store',    icon: 'store',    color: 'icon-emerald', version: '1.0.0' }
    ],

    getInstalled: function () {
      return apps.installed;
    },
    getById: function (id) {
      return apps.installed.find(function (a) { return a.id === id; }) || null;
    }
  };


  /* ═══════════════════════════════════════════════════════
     7. BrowserService — 북마크, 퀵링크
     ═══════════════════════════════════════════════════════ */
  var browser = {
    bookmarks: [
      { name: 'Zyl OS Official',    url: 'https://www.zylos.dev',           favicon: 'B' },
      { name: 'GitHub - Banana Pi',  url: 'https://github.com/banana-pi',   favicon: 'G' },
      { name: 'Banana Pi Wiki',      url: 'https://wiki.banana-pi.org',     favicon: 'W' },
      { name: 'RISC-V Foundation',   url: 'https://riscv.org',              favicon: 'R' },
      { name: 'The Linux Kernel',    url: 'https://kernel.org',             favicon: 'K' }
    ],

    quickLinks: [
      { name: 'GitHub',    url: 'https://github.com',    iconBg: 'linear-gradient(135deg, #333, #111)',           svgPath: 'M12 2C6.477 2 2 6.477 2 12c0 4.42 2.87 8.17 6.84 9.5.5.08.66-.23.66-.5v-1.69c-2.77.6-3.36-1.34-3.36-1.34-.46-1.16-1.11-1.47-1.11-1.47-.91-.62.07-.6.07-.6 1 .07 1.53 1.03 1.53 1.03.87 1.52 2.34 1.07 2.91.83.09-.65.35-1.09.63-1.34-2.22-.25-4.55-1.11-4.55-4.92 0-1.11.38-2 1.03-2.71-.1-.25-.45-1.29.1-2.64 0 0 .84-.27 2.75 1.02.79-.22 1.65-.33 2.5-.33.85 0 1.71.11 2.5.33 1.91-1.29 2.75-1.02 2.75-1.02.55 1.35.2 2.39.1 2.64.65.71 1.03 1.6 1.03 2.71 0 3.82-2.34 4.66-4.57 4.91.36.31.69.92.69 1.85V21c0 .27.16.59.67.5C19.14 20.16 22 16.42 22 12A10 10 0 0012 2z' },
      { name: 'YouTube',   url: 'https://youtube.com',   iconBg: 'linear-gradient(135deg, #ff0000, #cc0000)',     svgPath: 'M10 15l5.19-3L10 9v6m11.56-7.83c.13.47.22 1.1.28 1.9.07.8.1 1.49.1 2.09L22 12c0 2.19-.16 3.8-.44 4.83-.25.9-.83 1.48-1.73 1.73-.47.13-1.33.22-2.65.28-1.3.07-2.49.1-3.59.1L12 19c-4.19 0-6.8-.16-7.83-.44-.9-.25-1.48-.83-1.73-1.73-.13-.47-.22-1.1-.28-1.9-.07-.8-.1-1.49-.1-2.09L2 12c0-2.19.16-3.8.44-4.83.25-.9.83-1.48 1.73-1.73.47-.13 1.33-.22 2.65-.28 1.3-.07 2.49-.1 3.59-.1L12 5c4.19 0 6.8.16 7.83.44.9.25 1.48.83 1.73 1.73z' },
      { name: 'Wikipedia', url: 'https://wikipedia.org',  iconBg: 'linear-gradient(135deg, #eee, #ccc)',           svgPath: 'M14.97 18.95L12 12.52l-2.97 6.43a.5.5 0 01-.91-.01L4.94 9.04a.5.5 0 11.92-.38l3.18 7.72L12 10.04l2.96 6.34 3.18-7.72a.5.5 0 01.92.38l-3.18 9.9a.5.5 0 01-.91.01z', svgFill: '#333' },
      { name: 'RISC-V',    url: 'https://riscv.org',     iconBg: 'linear-gradient(135deg, #4a9eff, #2563eb)',     svgPath: 'M20 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0 14H4V8h16v10zm-2-1h-6v-2h6v2zM7.5 17l-1.41-1.41L8.67 13l-2.59-2.59L7.5 9l4 4-4 4z' }
    ],

    simulatedPages: {
      'zylos.dev': {
        title: 'Zyl OS Official',
        html: '<div class="sim-header"><h1>Zyl OS</h1><p>A modern Linux-based mobile operating system designed for Banana Pi single-board computers with RISC-V architecture.</p></div>' +
          '<div class="sim-section"><h3>Features</h3><p>Custom UI toolkit, WebKitGTK browser, hardware-accelerated graphics, and full RISC-V support.</p></div>' +
          '<div class="sim-section"><h3>Getting Started</h3><p>Flash the Zyl OS image to your SD card and boot your Banana Pi F3 board.</p></div>'
      },
      'github.com': {
        title: 'GitHub',
        html: '<div class="sim-header"><h1>GitHub</h1><p>Where the world builds software. Millions of developers use GitHub to build, ship, and maintain their software.</p></div>' +
          '<div class="sim-section"><h3>Trending Repositories</h3><p>Explore popular open source projects and contribute to the community.</p></div>'
      },
      'riscv.org': {
        title: 'RISC-V International',
        html: '<div class="sim-header"><h1>RISC-V International</h1><p>RISC-V is a free and open ISA enabling a new era of processor innovation.</p></div>' +
          '<div class="sim-section"><h3>Open Standard</h3><p>The RISC-V ISA is provided under open source licenses with no fees.</p></div>'
      }
    },

    getBookmarks: function () {
      return browser.bookmarks;
    },
    getQuickLinks: function () {
      return browser.quickLinks;
    },
    getSimulatedPages: function () {
      return browser.simulatedPages;
    },
    getSimulatedPage: function (domain) {
      return browser.simulatedPages[domain] || null;
    }
  };


  /* ═══════════════════════════════════════════════════════
     8. SettingsService — 설정 상태 저장소
     ═══════════════════════════════════════════════════════ */
  var settings = {
    state: {
      wifi:      { enabled: true },
      bluetooth: { enabled: true },
      display:   { brightness: 80, darkMode: true, autoBrightness: true, fontSize: 'medium' },
      sound:     { mediaVolume: 70, notifVolume: 80, alarmVolume: 90, vibration: true },
      security:  { lockType: 'PIN', pin: '0000', fingerprint: false },
      wallpaper: { current: 'default', options: ['default', 'gradient-blue', 'gradient-purple', 'gradient-dark', 'gradient-sunset'] }
    },

    getSetting: function (category) {
      return settings.state[category] || null;
    },

    updateSetting: function (category, key, value) {
      if (settings.state[category]) {
        settings.state[category][key] = value;
      }
      return settings.state[category] || null;
    }
  };


  /* ═══════════════════════════════════════════════════════
     Service Router — method 이름으로 서비스 호출
     ═══════════════════════════════════════════════════════ */
  var serviceMap = {
    fs: {
      getDirectory:     function (p) { return fs.getDirectory(p.path); },
      getUnixDirectory: function (p) { return fs.getUnixDirectory(p.path); },
      getFileContent:   function (p) { return fs.getFileContent(p.path); },
      getAllData:        function ()  { return fs.getAllData(); }
    },
    wifi: {
      getNetworks:  function () { return wifi.getNetworks(); },
      getConnected: function () { return wifi.getConnected(); }
    },
    bluetooth: {
      getDevices:   function () { return bluetooth.getDevices(); },
      getPaired:    function () { return bluetooth.getPaired(); },
      getConnected: function () { return bluetooth.getConnected(); }
    },
    device: {
      getInfo: function () { return deviceInfo.getInfo(); }
    },
    storage: {
      getUsage:     function () { return storage.getUsage(); },
      getFormatted: function () { return storage.getFormatted(); }
    },
    apps: {
      getInstalled: function () { return apps.getInstalled(); },
      getById:      function (p) { return apps.getById(p.id); }
    },
    browser: {
      getBookmarks:      function () { return browser.getBookmarks(); },
      getQuickLinks:     function () { return browser.getQuickLinks(); },
      getSimulatedPages: function () { return browser.getSimulatedPages(); },
      getSimulatedPage:  function (p) { return browser.getSimulatedPage(p.domain); }
    },
    settings: {
      get:    function (p) { return settings.getSetting(p.category); },
      update: function (p) { return settings.updateSetting(p.category, p.key, p.value); }
    }
  };

  /**
   * 서비스 요청 처리
   * @param {string} service  - 서비스 이름 (fs, wifi, bluetooth, device, storage, apps, browser)
   * @param {string} method   - 메서드 이름
   * @param {object} [params] - 파라미터 객체
   * @returns {*} 결과 데이터 또는 null
   */
  function handleRequest(service, method, params) {
    var svc = serviceMap[service];
    if (!svc) return null;
    var fn = svc[method];
    if (!fn) return null;
    return fn(params || {});
  }

  /* ═══ Public API ═══ */
  return {
    fs: fs,
    wifi: wifi,
    bluetooth: bluetooth,
    device: deviceInfo,
    storage: storage,
    apps: apps,
    browser: browser,
    settings: settings,
    handleRequest: handleRequest
  };

})();
