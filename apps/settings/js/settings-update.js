// ----------------------------------------------------------
// [Clean Architecture] Presentation Layer - Software Update Domain Module
//
// 역할: 설정 앱 소프트웨어 업데이트 — 현재 버전, 진행 상태, 확인·설치·재시작
// 수행범위: updater 서비스 상태 렌더링, 상태에 맞는 단일 동작 버튼,
//          StateChanged 이벤트로 진행률 갱신
// 의존방향: ZylSettingsCore (settings.js)
// SOLID: SRP — 업데이트 UI만 담당. 확인·검증·설치는 updater 서비스 몫
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ----------------------------------------------------------

(function () {
  'use strict';

  var core = window.ZylSettingsCore;
  var versionEl = document.getElementById('update-version');
  var statusEl = document.getElementById('update-status');
  var actionEl = document.getElementById('update-action');
  if (!versionEl || !statusEl || !actionEl) return;

  /* The one thing the user can do in each state (method of the updater service) */
  var ACTIONS = {
    idle: 'check', up_to_date: 'check', failed: 'check',
    available: 'install', pending_reboot: 'restart'
  };

  function render(s) {
    if (!s || !s.state) return;
    versionEl.textContent = s.version || '';
    statusEl.textContent = core.t('settings.update_' + s.state, { v: s.available || '', p: s.progress || 0 });
    var act = ACTIONS[s.state];
    actionEl.classList.toggle('hidden', !act);
    if (act) {
      actionEl.textContent = core.t('settings.update_do_' + act);
      actionEl.setAttribute('data-action', act);
    }
  }

  function call(method) {
    core.requestService('updater', method).then(render, function () {
      render({ state: 'failed', version: versionEl.textContent });
    });
  }

  actionEl.addEventListener('click', function () {
    call(actionEl.getAttribute('data-action'));
  });
  core.onServiceEvent('updater', 'StateChanged', render);
  call('getState');
})();
