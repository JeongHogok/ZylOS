// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Calculator app — basic + scientific operations with safe expression evaluation
// Scope: Keypad input, expression parsing (no eval/Function), result display, history, scientific functions
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
  var sciMode = false;

  /* ─── History ─── */
  var history = [];
  var MAX_HISTORY = 50;
  var historyPanel = document.getElementById('history-panel');
  var historyList = document.getElementById('history-list');
  var historyEmpty = document.getElementById('history-empty');
  var btnHistory = document.getElementById('btn-history');
  var btnClearHistory = document.getElementById('btn-clear-history');
  var historyVisible = false;

  /* ─── Scientific mode ─── */
  var btnMode = document.getElementById('btn-mode');
  var sciKeypad = document.getElementById('sci-keypad');
  var keypad = document.getElementById('keypad');

  function update() {
    if (result) result.textContent = current;
    if (expr) expr.textContent = expression;
  }

  /* ─── Safe math evaluator (no eval/Function) ─── */
  function safeEval(str) {
    /* Tokenize: split into numbers, operators, functions, parentheses */
    var tokens = [];
    var num = '';
    for (var i = 0; i < str.length; i++) {
      var ch = str[i];
      if (ch === ' ') continue;

      /* Check for function names */
      if (ch >= 'a' && ch <= 'z') {
        var funcName = '';
        while (i < str.length && str[i] >= 'a' && str[i] <= 'z') {
          funcName += str[i];
          i++;
        }
        i--; /* back up one */
        if (num !== '') { tokens.push(parseFloat(num)); num = ''; }
        tokens.push({ func: funcName });
        continue;
      }

      if (ch === '(' || ch === ')') {
        if (num !== '') { tokens.push(parseFloat(num)); num = ''; }
        tokens.push(ch);
        continue;
      }

      if ((ch >= '0' && ch <= '9') || ch === '.') {
        num += ch;
      } else if (ch === '-' && (tokens.length === 0 || typeof tokens[tokens.length - 1] === 'string' && (tokens[tokens.length - 1] === '(' || '+-*/^'.indexOf(tokens[tokens.length - 1]) !== -1))) {
        /* Negative number */
        num += ch;
      } else {
        if (num !== '') { tokens.push(parseFloat(num)); num = ''; }
        if (ch === '+' || ch === '-' || ch === '*' || ch === '/' || ch === '^') {
          tokens.push(ch);
        }
        if (ch === '!') {
          tokens.push('!');
        }
      }
    }
    if (num !== '') tokens.push(parseFloat(num));

    if (tokens.length === 0) return 0;

    /* Recursive descent parser for proper order of operations */
    var pos = { idx: 0 };

    function peek() { return pos.idx < tokens.length ? tokens[pos.idx] : null; }
    function next() { return tokens[pos.idx++]; }

    function parseExpr() {
      var left = parseTerm();
      while (peek() === '+' || peek() === '-') {
        var op = next();
        var right = parseTerm();
        if (op === '+') left = left + right;
        else left = left - right;
      }
      return left;
    }

    function parseTerm() {
      var left = parsePower();
      while (peek() === '*' || peek() === '/') {
        var op = next();
        var right = parsePower();
        if (op === '*') left = left * right;
        else {
          if (right === 0) return NaN;
          left = left / right;
        }
      }
      return left;
    }

    function parsePower() {
      var left = parseUnary();
      if (peek() === '^') {
        next();
        var right = parsePower(); /* right associative */
        left = Math.pow(left, right);
      }
      return left;
    }

    function parseUnary() {
      var val = parsePostfix();
      return val;
    }

    function parsePostfix() {
      var val = parsePrimary();
      while (peek() === '!') {
        next();
        val = factorial(val);
      }
      return val;
    }

    function parsePrimary() {
      var tok = peek();

      /* Function call */
      if (tok && typeof tok === 'object' && tok.func) {
        next();
        var arg;
        if (peek() === '(') {
          next(); /* consume ( */
          arg = parseExpr();
          if (peek() === ')') next(); /* consume ) */
        } else {
          arg = parsePrimary();
        }
        return applyFunc(tok.func, arg);
      }

      /* Parenthesized expression */
      if (tok === '(') {
        next();
        var val = parseExpr();
        if (peek() === ')') next();
        return val;
      }

      /* Number */
      if (typeof tok === 'number') {
        next();
        return tok;
      }

      /* Unary minus */
      if (tok === '-') {
        next();
        return -parsePrimary();
      }

      next();
      return 0;
    }

    function applyFunc(name, arg) {
      switch (name) {
        case 'sin': return Math.sin(arg);
        case 'cos': return Math.cos(arg);
        case 'tan': return Math.tan(arg);
        case 'log': return Math.log10(arg);
        case 'ln': return Math.log(arg);
        case 'sqrt': return Math.sqrt(arg);
        default: return arg;
      }
    }

    function factorial(n) {
      if (n < 0 || n !== Math.floor(n) || n > 170) return NaN;
      if (n === 0 || n === 1) return 1;
      var r = 1;
      for (var fi = 2; fi <= n; fi++) r *= fi;
      return r;
    }

    var total = parseExpr();

    if (isNaN(total) || !isFinite(total)) return 'Error';

    /* Clean up floating point display */
    var s = String(total);
    if (s.indexOf('.') !== -1 && s.length > 12) {
      s = String(parseFloat(total.toPrecision(10)));
    }
    return s;
  }

  /* ─── Add to history ─── */
  function addHistory(exprStr, resultStr) {
    if (resultStr === 'Error') return;
    history.unshift({ expr: exprStr, result: resultStr });
    if (history.length > MAX_HISTORY) history.pop();
    renderHistory();
  }

  function renderHistory() {
    if (!historyList) return;
    historyList.innerHTML = '';
    if (history.length === 0) {
      if (historyEmpty) historyEmpty.classList.remove('hidden');
      return;
    }
    if (historyEmpty) historyEmpty.classList.add('hidden');
    for (var i = 0; i < history.length; i++) {
      (function (item) {
        var el = document.createElement('div');
        el.className = 'history-item';
        var exprDiv = document.createElement('div');
        exprDiv.className = 'history-expr';
        exprDiv.textContent = item.expr;
        var resultDiv = document.createElement('div');
        resultDiv.className = 'history-result';
        resultDiv.textContent = '= ' + item.result;
        el.appendChild(exprDiv);
        el.appendChild(resultDiv);
        el.addEventListener('click', function () {
          current = item.result;
          expression = '';
          lastOp = false;
          update();
          toggleHistory();
        });
        historyList.appendChild(el);
      })(history[i]);
    }
  }

  function toggleHistory() {
    historyVisible = !historyVisible;
    if (historyPanel) historyPanel.classList.toggle('hidden', !historyVisible);
    if (keypad) keypad.classList.toggle('hidden', historyVisible);
    if (sciKeypad && sciMode) sciKeypad.classList.toggle('hidden', historyVisible);
  }

  if (btnHistory) {
    btnHistory.addEventListener('click', function () {
      toggleHistory();
    });
  }

  if (btnClearHistory) {
    btnClearHistory.addEventListener('click', function () {
      history = [];
      renderHistory();
    });
  }

  /* ─── Scientific mode toggle ─── */
  if (btnMode) {
    btnMode.addEventListener('click', function () {
      sciMode = !sciMode;
      btnMode.classList.toggle('active', sciMode);
      if (sciKeypad) sciKeypad.classList.toggle('hidden', !sciMode || historyVisible);
    });
  }

  /* ─── Scientific keypad handler ─── */
  if (sciKeypad) {
    sciKeypad.addEventListener('click', function (e) {
      var btn = e.target.closest('.key');
      if (!btn) return;
      var sci = btn.dataset.sci;
      if (!sci) return;

      switch (sci) {
        case 'sin':
        case 'cos':
        case 'tan':
        case 'log':
        case 'ln':
        case 'sqrt':
          expression += current + ' ';
          current = sci + '(';
          lastOp = false;
          break;
        case 'pow':
          expression += current + ' ^ ';
          lastOp = true;
          break;
        case 'pi':
          current = String(Math.PI);
          lastOp = false;
          break;
        case 'e':
          current = String(Math.E);
          lastOp = false;
          break;
        case 'paren_open':
          if (lastOp || current === '0') {
            current = '(';
          } else {
            current += '(';
          }
          lastOp = false;
          break;
        case 'paren_close':
          current += ')';
          lastOp = false;
          break;
        case 'factorial':
          current += '!';
          lastOp = false;
          break;
      }
      update();
    });
  }

  /* ─── Main keypad handler ─── */
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
      var fullExpr = expression + current;
      var evalExpr = fullExpr
        .replace(/\u00D7/g, '*')
        .replace(/\u00F7/g, '/')
        .replace(/\u2212/g, '-');
      var evalResult = String(safeEval(evalExpr));
      addHistory(fullExpr, evalResult);
      current = evalResult;
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
        if (historyVisible) {
          toggleHistory();
          ZylBridge.sendToSystem({ type: 'navigation.handled' });
          return;
        }
        ZylBridge.sendToSystem({ type: 'navigation.exit' });
        return;
      }
    } catch (err) { /* ignore */ }
  });

  update();
})();
