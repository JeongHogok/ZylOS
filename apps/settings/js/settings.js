/*
 * BPI-OS 설정 앱
 */

(function () {
  'use strict';

  /* ─── i18n 번역 (설정 앱 전용) ─── */
  var translations = {
    ko: {
      'settings.title': '설정',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': '블루투스',
      'settings.display': '디스플레이',
      'settings.sound': '사운드',
      'settings.language': '언어',
      'settings.wallpaper': '배경화면',
      'settings.security': '보안',
      'settings.storage': '저장공간',
      'settings.about': '이 기기 정보',
      'settings.connected': '연결됨',
      'settings.on': '켜짐',
      'settings.off': '꺼짐',
      'settings.brightness': '밝기',
      'settings.dark_mode': '다크 모드',
      'settings.auto_brightness': '자동 밝기',
      'settings.font_size': '글꼴 크기',
      'settings.device_name': '기기 이름',
      'settings.os_version': 'OS 버전',
      'settings.kernel': '커널',
      'settings.build': '빌드',
    },
    en: {
      'settings.title': 'Settings',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': 'Bluetooth',
      'settings.display': 'Display',
      'settings.sound': 'Sound',
      'settings.language': 'Language',
      'settings.wallpaper': 'Wallpaper',
      'settings.security': 'Security',
      'settings.storage': 'Storage',
      'settings.about': 'About This Device',
      'settings.connected': 'Connected',
      'settings.on': 'On',
      'settings.off': 'Off',
      'settings.brightness': 'Brightness',
      'settings.dark_mode': 'Dark Mode',
      'settings.auto_brightness': 'Auto Brightness',
      'settings.font_size': 'Font Size',
      'settings.device_name': 'Device Name',
      'settings.os_version': 'OS Version',
      'settings.kernel': 'Kernel',
      'settings.build': 'Build',
    },
    ja: {
      'settings.title': '設定',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': 'Bluetooth',
      'settings.display': 'ディスプレイ',
      'settings.sound': 'サウンド',
      'settings.language': '言語',
      'settings.wallpaper': '壁紙',
      'settings.security': 'セキュリティ',
      'settings.storage': 'ストレージ',
      'settings.about': 'このデバイスについて',
      'settings.connected': '接続済み',
      'settings.on': 'オン',
      'settings.off': 'オフ',
      'settings.brightness': '明るさ',
      'settings.dark_mode': 'ダークモード',
      'settings.auto_brightness': '自動明るさ',
      'settings.font_size': 'フォントサイズ',
      'settings.device_name': 'デバイス名',
      'settings.os_version': 'OSバージョン',
      'settings.kernel': 'カーネル',
      'settings.build': 'ビルド',
    },
    zh: {
      'settings.title': '设置',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': '蓝牙',
      'settings.display': '显示',
      'settings.sound': '声音',
      'settings.language': '语言',
      'settings.wallpaper': '壁纸',
      'settings.security': '安全',
      'settings.storage': '存储',
      'settings.about': '关于本机',
      'settings.connected': '已连接',
      'settings.on': '开',
      'settings.off': '关',
      'settings.brightness': '亮度',
      'settings.dark_mode': '深色模式',
      'settings.auto_brightness': '自动亮度',
      'settings.font_size': '字体大小',
      'settings.device_name': '设备名称',
      'settings.os_version': '系统版本',
      'settings.kernel': '内核',
      'settings.build': '版本号',
    },
    es: {
      'settings.title': 'Ajustes',
      'settings.wifi': 'Wi-Fi',
      'settings.bluetooth': 'Bluetooth',
      'settings.display': 'Pantalla',
      'settings.sound': 'Sonido',
      'settings.language': 'Idioma',
      'settings.wallpaper': 'Fondo de pantalla',
      'settings.security': 'Seguridad',
      'settings.storage': 'Almacenamiento',
      'settings.about': 'Acerca del dispositivo',
      'settings.connected': 'Conectado',
      'settings.on': 'Activado',
      'settings.off': 'Desactivado',
      'settings.brightness': 'Brillo',
      'settings.dark_mode': 'Modo oscuro',
      'settings.auto_brightness': 'Brillo automático',
      'settings.font_size': 'Tamaño de fuente',
      'settings.device_name': 'Nombre del dispositivo',
      'settings.os_version': 'Versión del SO',
      'settings.kernel': 'Kernel',
      'settings.build': 'Compilación',
    },
  };

  var currentLocale = 'ko';
  var LANG_NAMES = { ko: '한국어', en: 'English', ja: '日本語', zh: '中文', es: 'Español' };

  function t(key) {
    var dict = translations[currentLocale] || translations['ko'];
    return dict[key] || key;
  }

  function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(function (el) {
      el.textContent = t(el.getAttribute('data-i18n'));
    });
    document.getElementById('current-lang').textContent = LANG_NAMES[currentLocale];
    document.documentElement.lang = currentLocale;
  }

  /* ─── 네비게이션 ─── */
  var mainMenu = document.getElementById('main-menu');
  var btnBack = document.getElementById('btn-back');
  var headerTitle = document.getElementById('header-title');
  var currentPage = null;

  var PAGE_TITLES = {
    language: 'settings.language',
    display: 'settings.display',
    about: 'settings.about',
  };

  /* 메뉴 항목 클릭 → 서브 페이지 */
  document.querySelectorAll('.setting-item[data-page]').forEach(function (item) {
    item.addEventListener('click', function () {
      var pageId = item.dataset.page;
      var page = document.getElementById('page-' + pageId);
      if (!page) return;

      mainMenu.classList.add('hidden');
      page.classList.remove('hidden');
      btnBack.classList.remove('hidden');
      currentPage = pageId;

      headerTitle.textContent = t(PAGE_TITLES[pageId] || 'settings.' + pageId);
    });
  });

  /* 뒤로가기 */
  btnBack.addEventListener('click', function () {
    if (currentPage) {
      var page = document.getElementById('page-' + currentPage);
      if (page) page.classList.add('hidden');
      mainMenu.classList.remove('hidden');
      btnBack.classList.add('hidden');
      headerTitle.textContent = t('settings.title');
      currentPage = null;
    }
  });

  /* ─── 언어 선택 ─── */
  function updateLangChecks() {
    document.querySelectorAll('.lang-option').forEach(function (opt) {
      var check = opt.querySelector('.check-icon');
      if (opt.dataset.lang === currentLocale) {
        check.classList.remove('hidden');
      } else {
        check.classList.add('hidden');
      }
    });
  }

  document.querySelectorAll('.lang-option').forEach(function (opt) {
    opt.addEventListener('click', function () {
      currentLocale = opt.dataset.lang;
      updateLangChecks();
      applyTranslations();

      /* 시스템 전체에 언어 변경 알림 (D-Bus 통해) */
      if (window.navigator && window.navigator.system) {
        window.webkit.messageHandlers.bridge.postMessage(
          JSON.stringify({ type: 'system.setLocale', locale: currentLocale })
        );
      }

      /* 헤더 제목도 업데이트 */
      headerTitle.textContent = t('settings.language');
    });
  });

  /* ─── 초기화 ─── */
  applyTranslations();
  updateLangChecks();

})();
