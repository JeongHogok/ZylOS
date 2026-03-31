// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// 역할: 클립보드 서비스 — 텍스트/mimeType 클립보드 copy/paste/clear
// 수행범위: 앱 간 클립보드 데이터 관리, sourceApp 추적, 타임스탬프 기록
//           실제 브라우저 Clipboard API에 의존하지 않으며 OS 내부 상태로 관리
// 의존방향: 없음 (deps.invoke 미사용 — 순수 인메모리 상태)
// SOLID: SRP — 클립보드 데이터 저장/조회만 담당
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.clipboard = function (deps) {
    // deps 매개변수는 서비스 팩토리 규약 상 수신하지만 이 모듈은 invoke를 사용하지 않는다
    void deps;

    /* 클립보드 내부 상태 */
    var _data = {
      text:      '',
      mimeType:  'text/plain',
      sourceApp: '',
      timestamp: 0
    };

    return {
      /**
       * 클립보드에 데이터를 복사한다.
       * p: { text, mimeType?, sourceApp? }
       */
      copy: function (p) {
        if (!p || typeof p.text !== 'string') {
          return { error: 'clipboard.copy: text is required' };
        }
        _data = {
          text:      p.text,
          mimeType:  p.mimeType  || 'text/plain',
          sourceApp: p.sourceApp || '',
          timestamp: Date.now()
        };
        return { success: true, length: _data.text.length };
      },

      /**
       * 클립보드에서 데이터를 붙여넣는다.
       * p: {} (파라미터 불필요)
       * 반환: { text, mimeType, sourceApp, timestamp } 또는 null (데이터 없음)
       */
      paste: function (p) {
        void p;
        if (!_data.timestamp) return null;
        return {
          text:      _data.text,
          mimeType:  _data.mimeType,
          sourceApp: _data.sourceApp,
          timestamp: _data.timestamp
        };
      },

      /**
       * 클립보드 데이터를 초기화한다.
       */
      clear: function () {
        _data = { text: '', mimeType: 'text/plain', sourceApp: '', timestamp: 0 };
        return { success: true };
      },

      /**
       * 클립보드에 데이터가 있는지 확인한다.
       * 반환: boolean
       */
      hasData: function () {
        return _data.timestamp > 0 && _data.text.length > 0;
      }
    };
  };

})(window.ZylServiceModules = window.ZylServiceModules || {});
