// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Calculator app — four basic operations with safe expression evaluation
// Scope: Keypad input, expression parsing (no eval/Function), result display
// Dependency: None (standalone app)
// SOLID: SRP — calculator UI and arithmetic only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';
  var expr = document.getElementById('expression');
  var result = document.getElementById('result');
  var current = '0';
  var expression = '';
  var lastOp = false;

  function update() {
    if (result) result.textContent = current;
    if (expr) expr.textContent = expression;
  }

  /* ─── Safe math evaluator (no eval/Function) ─── */
  function safeEval(str) {
    /* Tokenize: split into numbers and operators */
    var tokens = [];
    var num = '';
    for (var i = 0; i < str.length; i++) {
      var ch = str[i];
      if (ch === ' ') continue;
      if ((ch >= '0' && ch <= '9') || ch === '.') {
        num += ch;
      } else if (ch === '-' && (tokens.length === 0 || typeof tokens[tokens.length - 1] === 'string')) {
        /* Negative number */
        num += ch;
      } else {
        if (num !== '') { tokens.push(parseFloat(num)); num = ''; }
        if (ch === '+' || ch === '-' || ch === '*' || ch === '/') {
          tokens.push(ch);
        }
      }
    }
    if (num !== '') tokens.push(parseFloat(num));

    if (tokens.length === 0) return 0;

    /* Pass 1: multiplication and division (left to right) */
    var pass1 = [tokens[0]];
    for (var j = 1; j < tokens.length; j += 2) {
      var op = tokens[j];
      var right = tokens[j + 1];
      if (right === undefined) break;
      if (op === '*') {
        pass1[pass1.length - 1] = pass1[pass1.length - 1] * right;
      } else if (op === '/') {
        if (right === 0) return 'Error';
        pass1[pass1.length - 1] = pass1[pass1.length - 1] / right;
      } else {
        pass1.push(op);
        pass1.push(right);
      }
    }

    /* Pass 2: addition and subtraction */
    var total = pass1[0];
    for (var k = 1; k < pass1.length; k += 2) {
      var op2 = pass1[k];
      var val = pass1[k + 1];
      if (val === undefined) break;
      if (op2 === '+') total += val;
      else if (op2 === '-') total -= val;
    }

    if (isNaN(total) || !isFinite(total)) return 'Error';

    /* Clean up floating point display */
    var s = String(total);
    if (s.indexOf('.') !== -1 && s.length > 12) {
      s = String(parseFloat(total.toPrecision(10)));
    }
    return s;
  }

  document.getElementById('keypad').addEventListener('click', function (e) {
    var btn = e.target.closest('.key');
    if (!btn) return;
    var val = btn.dataset.value;
    var action = btn.dataset.action;

    if (val !== undefined) {
      if (lastOp || current === '0') {
        current = val === '.' ? '0.' : val;
        lastOp = false;
      } else {
        if (val === '.' && current.indexOf('.') !== -1) return;
        current += val;
      }
    } else if (action === 'clear') {
      current = '0';
      expression = '';
      lastOp = false;
    } else if (action === 'backspace') {
      current = current.length > 1 ? current.slice(0, -1) : '0';
    } else if (action === 'percent') {
      current = String(parseFloat(current) / 100);
    } else if (action === 'equals') {
      expression += current;
      var evalExpr = expression
        .replace(/×/g, '*')
        .replace(/÷/g, '/')
        .replace(/−/g, '-');
      current = String(safeEval(evalExpr));
      expression = '';
      lastOp = false;
    } else {
      var ops = { add: '+', subtract: '\u2212', multiply: '\u00D7', divide: '\u00F7' };
      expression += current + ' ' + (ops[action] || '') + ' ';
      lastOp = true;
    }
    update();
  });

  /* ─── Message Handler ─── */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        ZylBridge.sendToSystem({ type: 'navigation.exit' });
        return;
      }
    } catch (err) { /* ignore */ }
  });

  update();
})();
