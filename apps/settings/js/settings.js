// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 설정 앱 UI — 시스템 설정 관리 페이지
// 수행범위: 언어 변경, 디스플레이 설정, 시스템 정보, 개발자 옵션
// 의존방향: bpiI18n (i18n.js), BpiBridge (bridge.js)
// SOLID: SRP — 설정 UI 렌더링과 설정값 관리만 담당
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── Language display names ─── */
  var LANG_NAMES = { ko: '한국어', en: 'English', ja: '日本語', zh: '中文', es: 'Español' };

  /* ─── i18n helper (delegates to shared bpiI18n) ─── */
  function t(key, params) {
    return bpiI18n.t(key, params);
  }

  function applyTranslations() {
    bpiI18n.applyTranslations();
    document.getElementById('current-lang').textContent = LANG_NAMES[bpiI18n.getLocale()];
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
    var locale = bpiI18n.getLocale();
    document.querySelectorAll('.lang-option').forEach(function (opt) {
      var check = opt.querySelector('.check-icon');
      if (opt.dataset.lang === locale) {
        check.classList.remove('hidden');
      } else {
        check.classList.add('hidden');
      }
    });
  }

  document.querySelectorAll('.lang-option').forEach(function (opt) {
    opt.addEventListener('click', function () {
      var newLocale = opt.dataset.lang;
      bpiI18n.setLocale(newLocale);
      updateLangChecks();
      applyTranslations();

      /* 시스템 전체에 언어 변경 알림 (bridge 사용) */
      BpiBridge.setLocale(newLocale);

      /* 헤더 제목도 업데이트 */
      headerTitle.textContent = t('settings.language');
    });
  });

  /* ─── 초기화 ─── */
  applyTranslations();
  updateLangChecks();

})();
