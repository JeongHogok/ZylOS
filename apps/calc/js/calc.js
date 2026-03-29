// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 계산기 앱 — 사칙연산
// 수행범위: 키패드 입력, 수식 평가, 결과 표시
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 계산기 UI와 연산만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var expr = document.getElementById('expression');
  var result = document.getElementById('result');
  var current = '0'; var expression = ''; var lastOp = false;

  function update() { if (result) result.textContent = current; if (expr) expr.textContent = expression; }

  document.getElementById('keypad').addEventListener('click', function (e) {
    var btn = e.target.closest('.key');
    if (!btn) return;
    var val = btn.dataset.value;
    var action = btn.dataset.action;

    if (val !== undefined) {
      if (lastOp || current === '0') { current = val === '.' ? '0.' : val; lastOp = false; }
      else { if (val === '.' && current.indexOf('.') !== -1) return; current += val; }
    } else if (action === 'clear') { current = '0'; expression = ''; lastOp = false; }
    else if (action === 'backspace') { current = current.length > 1 ? current.slice(0, -1) : '0'; }
    else if (action === 'percent') { current = String(parseFloat(current) / 100); }
    else if (action === 'equals') {
      expression += current;
      try { current = String(Function('"use strict"; return (' + expression.replace(/×/g, '*').replace(/÷/g, '/').replace(/−/g, '-') + ')')());  }
      catch (err) { current = 'Error'; }
      expression = ''; lastOp = false;
    } else {
      var ops = { add: '+', subtract: '−', multiply: '×', divide: '÷' };
      expression += current + ' ' + (ops[action] || '') + ' ';
      lastOp = true;
    }
    update();
  });
  update();
})();
