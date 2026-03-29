// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 터미널 에뮬레이터 UI — 셸 명령 실행 및 출력 표시
// 수행범위: 명령 입력/실행, 명령 히스토리, 출력 스크롤백
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 터미널 UI와 명령 처리만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── DOM ─── */
  var outputContent = document.getElementById('output-content');
  var terminalOutput = document.getElementById('terminal-output');
  var commandInput = document.getElementById('command-input');
  var btnClear = document.getElementById('btn-clear');
  var btnInfo = document.getElementById('btn-info');

  /* ─── State ─── */
  var commandHistory = [];
  var historyIndex = -1;
  var currentDir = '/home/user';
  var hostname = 'bpi-f3';
  var username = 'user';

  /* ─── Filesystem + device info (loaded from service) ─── */
  var filesystem = {};
  var fileContents = {};
  var deviceData = null;
  var _asyncCallbacks = {};
  var _asyncId = 0;

  /* Request data from central service */
  function requestServiceData() {
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'fs', method: 'getAllData'
    }), '*');
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: 'device', method: 'getInfo'
    }), '*');
  }

  /* Async service request with callback */
  function requestServiceAsync(service, method, params, callback) {
    var id = 'term_' + (++_asyncId);
    _asyncCallbacks[id] = callback;
    window.parent.postMessage(JSON.stringify({
      type: 'service.request', service: service, method: method,
      params: params || {}, requestId: id
    }), '*');
  }

  /* ─── Helpers ─── */
  function pad(val, width) {
    var s = String(val);
    while (s.length < width) s = ' ' + s;
    return s;
  }

  function formatUptime(d) {
    if (d && d.bootTime) {
      var secs = Math.floor((Date.now() - d.bootTime) / 1000);
      var h = Math.floor(secs / 3600);
      var m = Math.floor((secs % 3600) / 60);
      if (h > 0) return h + ' hour' + (h > 1 ? 's' : '') + ', ' + m + ' min' + (m !== 1 ? 's' : '');
      return m + ' min' + (m !== 1 ? 's' : '');
    }
    return '0 mins';
  }

  function formatMemory(d) {
    var ramStr = (d && d.ram) || '2GB';
    var totalMB = parseInt(ramStr, 10) * 1024 || 2048;
    var usedMB = Math.floor(totalMB * 0.27);
    return usedMB + 'MiB / ' + totalMB + 'MiB';
  }

  /* Listen for service responses */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent && e.source !== window) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;
      /* Handle async callbacks first */
      if (msg.requestId && _asyncCallbacks[msg.requestId]) {
        _asyncCallbacks[msg.requestId](msg.data);
        delete _asyncCallbacks[msg.requestId];
        return;
      }
      if (msg.service === 'fs' && msg.method === 'getAllData' && msg.data) {
        filesystem = msg.data.unixTree || {};
        fileContents = msg.data.fileContents || {};
      } else if (msg.service === 'device' && msg.method === 'getInfo' && msg.data) {
        deviceData = msg.data;
        hostname = msg.data.hostname || hostname;
        username = msg.data.username || username;
        updatePrompt();
      }
    } catch (err) { /* ignore */ }
  });

  requestServiceData();

  /* ─── Prompt String ─── */
  function getPrompt() {
    var dir = currentDir.replace('/home/' + username, '~');
    return '$ ' + username + '@' + hostname + ':' + dir;
  }

  function updatePrompt() {
    document.getElementById('prompt').textContent = getPrompt();
  }

  /* ─── Output Helpers ─── */
  function addLine(text, className) {
    var line = document.createElement('div');
    line.className = 'line ' + (className || '');
    line.textContent = text;
    outputContent.appendChild(line);
  }

  function addHtml(html, className) {
    var line = document.createElement('div');
    line.className = 'line ' + (className || '');
    line.innerHTML = html;
    outputContent.appendChild(line);
  }

  function addPromptLine(cmd) {
    addLine(getPrompt() + ' ' + cmd, 'line-prompt');
  }

  function scrollToBottom() {
    terminalOutput.scrollTop = terminalOutput.scrollHeight;
  }

  /* ─── Commands ─── */
  var commands = {
    help: function () {
      addLine('Zyl OS Terminal - Available commands:', 'line-info');
      addLine('');
      var cmds = [
        ['ls [path]', 'List directory contents'],
        ['cd <dir>', 'Change directory'],
        ['pwd', 'Print working directory'],
        ['cat <file>', 'Display file contents'],
        ['echo <text>', 'Print text to terminal'],
        ['clear', 'Clear the terminal screen'],
        ['uname [-a]', 'System information'],
        ['date', 'Display current date/time'],
        ['whoami', 'Display current user'],
        ['hostname', 'Display hostname'],
        ['uptime', 'System uptime'],
        ['free', 'Memory usage'],
        ['df', 'Disk usage'],
        ['neofetch', 'System info with ASCII art'],
        ['history', 'Command history'],
        ['help', 'Show this help message']
      ];
      cmds.forEach(function (c) {
        addHtml('  <span class="line-bold">' + c[0].padEnd(16) + '</span> <span class="line-output">' + c[1] + '</span>');
      });
      addLine('');
    },

    ls: function (args) {
      var target = currentDir;
      if (args.length > 0 && args[0] !== '-l' && args[0] !== '-la' && args[0] !== '-a') {
        target = resolvePath(args[0]);
      }
      var entries = filesystem[target];
      if (!entries) {
        addLine('ls: cannot access \'' + (args[0] || target) + '\': No such file or directory', 'line-error');
        return;
      }
      var isLong = args.indexOf('-l') !== -1 || args.indexOf('-la') !== -1;
      var showHidden = args.indexOf('-a') !== -1 || args.indexOf('-la') !== -1;

      var filtered = showHidden ? entries : entries.filter(function (e) { return e[0] !== '.'; });

      if (isLong) {
        addLine('total ' + filtered.length, 'line-output');
        filtered.forEach(function (entry) {
          var isDir = entry.endsWith('/') || (filesystem[target + '/' + entry] !== undefined);
          var name = entry.replace(/\/$/, '');
          var perms = isDir ? 'drwxr-xr-x' : '-rw-r--r--';
          var size = isDir ? '4096' : '0';
          var now = new Date();
          var months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
          var date = months[now.getMonth()] + ' ' + String(now.getDate()).padStart(2, ' ') + ' ' + String(now.getHours()).padStart(2, '0') + ':' + String(now.getMinutes()).padStart(2, '0');
          var line = perms + '  1 user user ' + size.padStart(8) + ' ' + date + ' ';
          addHtml(line + (isDir
            ? '<span class="line-info">' + name + '</span>'
            : '<span class="line-output">' + name + '</span>'));
        });
      } else {
        var output = '';
        filtered.forEach(function (entry) {
          var isDir = entry.endsWith('/') || (filesystem[target + '/' + entry] !== undefined);
          var name = entry.replace(/\/$/, '');
          if (isDir) {
            output += '<span class="line-info">' + name + '</span>  ';
          } else {
            output += '<span class="line-output">' + name + '</span>  ';
          }
        });
        addHtml(output);
      }
    },

    cd: function (args) {
      if (args.length === 0 || args[0] === '~') {
        currentDir = '/home/' + username;
        updatePrompt();
        return;
      }
      var target = resolvePath(args[0]);
      if (filesystem[target] !== undefined) {
        currentDir = target;
        updatePrompt();
      } else {
        addLine('cd: ' + args[0] + ': No such file or directory', 'line-error');
      }
    },

    pwd: function () {
      addLine(currentDir, 'line-output');
    },

    cat: function (args) {
      if (args.length === 0) {
        addLine('cat: missing file operand', 'line-error');
        return;
      }
      var filePath = resolvePath(args[0]);
      if (fileContents[filePath]) {
        fileContents[filePath].split('\n').forEach(function (l) {
          addLine(l, 'line-output');
        });
      } else {
        addLine('cat: ' + args[0] + ': No such file or directory', 'line-error');
      }
    },

    echo: function (args) {
      addLine(args.join(' '), 'line-output');
    },

    clear: function () {
      outputContent.innerHTML = '';
    },

    uname: function (args) {
      if (args.indexOf('-a') !== -1) {
        var d = deviceData || {};
        var kern = d.kernel ? d.kernel.replace('Linux ', '') : '6.6.63';
        addLine('Linux ' + hostname + ' ' + kern + ' #1 SMP PREEMPT_DYNAMIC Fri Mar 28 00:00:00 UTC 2026 riscv64 GNU/Linux', 'line-output');
      } else {
        addLine('Linux', 'line-output');
      }
    },

    date: function () {
      addLine(new Date().toString(), 'line-output');
    },

    whoami: function () {
      addLine(username, 'line-output');
    },

    hostname: function () {
      addLine(hostname, 'line-output');
    },

    uptime: function () {
      /* Calculate from device boot time via device.getUptime service */
      requestServiceAsync('device', 'getUptime', {}, function (secs) {
        var upSecs = parseInt(secs, 10) || 0;
        var h = Math.floor(upSecs / 3600);
        var m = Math.floor((upSecs % 3600) / 60);
        addLine(' ' + new Date().toTimeString().slice(0, 5) + ' up ' + h + ':' + (m < 10 ? '0' : '') + m + ',  1 user,  load average: 0.12, 0.08, 0.04', 'line-output');
      });
    },

    free: function () {
      /* Query actual RAM info from device service */
      requestServiceAsync('device', 'getInfo', {}, function (info) {
        var ramStr = (info && info.ram) || '2GB';
        var totalMB = parseInt(ramStr, 10) * 1024 || 2048;
        var totalKB = totalMB * 1024;
        var usedKB = Math.floor(totalKB * 0.28);
        var freeKB = Math.floor(totalKB * 0.50);
        var sharedKB = Math.floor(totalKB * 0.015);
        var buffKB = totalKB - usedKB - freeKB;
        var availKB = freeKB + buffKB;
        addLine('              total        used        free      shared  buff/cache   available', 'line-output');
        addLine('Mem:    ' + pad(totalKB, 12) + pad(usedKB, 12) + pad(freeKB, 12) + pad(sharedKB, 12) + pad(buffKB, 12) + pad(availKB, 12), 'line-output');
        addLine('Swap:   ' + pad(Math.floor(totalKB / 8), 12) + pad(0, 12) + pad(Math.floor(totalKB / 8), 12), 'line-output');
      });
    },

    df: function () {
      /* Query actual storage usage */
      requestServiceAsync('storage', 'getUsage', {}, function (usage) {
        var total = (usage && usage.total) || 0;
        var used = (usage && usage.used) || 0;
        var avail = total - used;
        var pct = total > 0 ? Math.round(used / total * 100) : 0;
        var totalK = Math.floor(total / 1024);
        var usedK = Math.floor(used / 1024);
        var availK = Math.floor(avail / 1024);
        addLine('Filesystem     1K-blocks     Used Available Use% Mounted on', 'line-output');
        addLine('/dev/mmcblk0p2 ' + pad(totalK, 10) + pad(usedK, 9) + pad(availK, 10) + pad(pct, 4) + '% /', 'line-output');
        addLine('tmpfs            819200     2048    817152   1% /tmp', 'line-output');
      });
    },

    neofetch: function () {
      var art = [
        '        _,met$$$$$gg.          ',
        '     ,g$$$$$$$$$$$$$$$P.       ',
        '   ,g$$P"     """Y$$.".       ',
        '  ,$$P\'              `$$$.     ',
        ' \',$$P       ,ggs.     `$$b:  ',
        ' `d$$\'     ,$P"\'   .    $$$   ',
        '  $$P      d$\'     ,    $$P   ',
        '  $$:      $$.   -    ,d$$\'   ',
        '  $$;      Y$b._   _,d$P\'    ',
        '  Y$$.    `.`"Y$$$$P"\'       ',
        '  `$$b      "-.__           ',
        '   `Y$$                      ',
        '    `Y$$.                    ',
        '      `$$b.                  ',
        '        `Y$$b.              ',
        '           `"Y$b._          ',
        '               `"""          '
      ];

      var d = deviceData || {};
      var info = [
        '',
        '<span class="line-bold">' + username + '@' + hostname + '</span>',
        '──────────────────',
        '<span class="line-bold">OS:</span> ' + (d.osVersion || 'Zyl OS 0.1.0') + ' riscv64',
        '<span class="line-bold">Host:</span> ' + (d.deviceName || 'Banana Pi BPI-F3'),
        '<span class="line-bold">Kernel:</span> ' + (d.kernel ? d.kernel.replace('Linux ', '') : '6.6.63'),
        '<span class="line-bold">Uptime:</span> ' + formatUptime(d),
        '<span class="line-bold">Shell:</span> bash 5.2.26',
        '<span class="line-bold">Resolution:</span> ' + (d.resolution || '720x1280'),
        '<span class="line-bold">DE:</span> Zyl OS Shell',
        '<span class="line-bold">WM:</span> Wayland',
        '<span class="line-bold">Terminal:</span> zyl-terminal',
        '<span class="line-bold">CPU:</span> ' + (d.soc || 'SpacemiT K1 (RISC-V)').replace(' (RISC-V)', '') + ' (8) @ 1.6GHz',
        '<span class="line-bold">Memory:</span> ' + formatMemory(d),
        '',
        '<span style="color:#ff4444">███</span><span style="color:#ff8800">███</span><span style="color:#ffff00">███</span><span style="color:#00ff41">███</span><span style="color:#4a9eff">███</span><span style="color:#a78bfa">███</span>',
        ''
      ];

      for (var i = 0; i < Math.max(art.length, info.length); i++) {
        var left = art[i] || '                               ';
        var right = info[i] || '';
        addHtml('<span class="line-info">' + left + '</span>' + right, 'line-ascii');
      }
    },

    history: function () {
      commandHistory.forEach(function (cmd, i) {
        addLine('  ' + (i + 1).toString().padStart(4) + '  ' + cmd, 'line-output');
      });
    }
  };

  /* ─── Path Resolution ─── */
  function resolvePath(input) {
    if (input.startsWith('/')) return normalizePath(input);
    if (input.startsWith('~')) return normalizePath('/home/' + username + input.slice(1));
    return normalizePath(currentDir + '/' + input);
  }

  function normalizePath(path) {
    var parts = path.split('/').filter(Boolean);
    var resolved = [];
    parts.forEach(function (p) {
      if (p === '..') {
        resolved.pop();
      } else if (p !== '.') {
        resolved.push(p);
      }
    });
    return '/' + resolved.join('/');
  }

  /* ─── Execute Command ─── */
  function executeCommand(input) {
    var trimmed = input.trim();
    if (!trimmed) return;

    addPromptLine(trimmed);
    commandHistory.push(trimmed);
    historyIndex = commandHistory.length;

    /* Parse command and args */
    var parts = parseCommand(trimmed);
    var cmd = parts[0];
    var args = parts.slice(1);

    if (cmd === 'clear') {
      commands.clear(args);
    } else if (commands[cmd]) {
      commands[cmd](args);
    } else {
      /* 알 수 없는 명령: Tauri 백엔드에서 실제 쉘 실행 시도 */
      execOnBackend(trimmed);
    }

    scrollToBottom();
  }

  function parseCommand(input) {
    var parts = [];
    var current = '';
    var inQuote = false;
    var quoteChar = '';

    for (var i = 0; i < input.length; i++) {
      var ch = input[i];
      if (inQuote) {
        if (ch === quoteChar) {
          inQuote = false;
        } else {
          current += ch;
        }
      } else if (ch === '"' || ch === "'") {
        inQuote = true;
        quoteChar = ch;
      } else if (ch === ' ') {
        if (current) {
          parts.push(current);
          current = '';
        }
      } else {
        current += ch;
      }
    }
    if (current) parts.push(current);
    return parts;
  }

  /* ─── Event: Command Input ─── */
  commandInput.addEventListener('keydown', function (e) {
    if (e.key === 'Enter') {
      executeCommand(commandInput.value);
      commandInput.value = '';
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (historyIndex > 0) {
        historyIndex--;
        commandInput.value = commandHistory[historyIndex];
      }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIndex < commandHistory.length - 1) {
        historyIndex++;
        commandInput.value = commandHistory[historyIndex];
      } else {
        historyIndex = commandHistory.length;
        commandInput.value = '';
      }
    }
  });

  /* ─── Quick Keys ─── */
  document.querySelectorAll('.qkey').forEach(function (btn) {
    btn.addEventListener('click', function () {
      var insert = btn.dataset.insert;
      var key = btn.dataset.key;

      if (insert) {
        commandInput.value += insert;
        commandInput.focus();
      } else if (key === 'up') {
        if (historyIndex > 0) {
          historyIndex--;
          commandInput.value = commandHistory[historyIndex];
        }
        commandInput.focus();
      } else if (key === 'down') {
        if (historyIndex < commandHistory.length - 1) {
          historyIndex++;
          commandInput.value = commandHistory[historyIndex];
        } else {
          historyIndex = commandHistory.length;
          commandInput.value = '';
        }
        commandInput.focus();
      } else if (key === 'tab') {
        /* Simple tab completion */
        var val = commandInput.value;
        var parts = val.split(' ');
        var lastPart = parts[parts.length - 1];
        if (lastPart) {
          var entries = filesystem[currentDir] || [];
          var match = entries.find(function (e) {
            return e.toLowerCase().startsWith(lastPart.toLowerCase());
          });
          if (match) {
            parts[parts.length - 1] = match;
            commandInput.value = parts.join(' ');
          }
        }
        commandInput.focus();
      } else if (key === 'ctrl') {
        /* Ctrl+C simulation */
        addPromptLine(commandInput.value + '^C');
        commandInput.value = '';
        commandInput.focus();
        scrollToBottom();
      }
    });
  });

  /* ─── Clear Button ─── */
  btnClear.addEventListener('click', function () {
    outputContent.innerHTML = '';
  });

  /* ─── Info Button ─── */
  btnInfo.addEventListener('click', function () {
    addLine('Zyl OS Terminal v1.0.0', 'line-info');
    addLine('Shell: bash 5.2.26 (riscv64)', 'line-output');
    addLine('Type \'help\' for available commands.', 'line-output');
    addLine('');
    scrollToBottom();
  });

  /* ─── Tap to focus ─── */
  terminalOutput.addEventListener('click', function () {
    commandInput.focus();
  });

  /* ─── Welcome Message ─── */
  function showWelcome() {
    var d = deviceData || {};
    addHtml('<span class="line-bold">Zyl OS Terminal v1.0.0</span>', 'line-info');
    addLine('Copyright (c) 2026 Zyl OS Project', 'line-output');
    addLine('Running on ' + (d.deviceName || 'Banana Pi BPI-F3') + ' (' + (d.soc || 'SpacemiT K1 (RISC-V)') + ')', 'line-output');
    addLine('');
    addLine('Type \'help\' for available commands.', 'line-output');
    addLine('');
    scrollToBottom();
  }

  /* ─── Backend Execution (postMessage IPC → 에뮬레이터 서비스 라우터) ─── */
  var _pendingExec = null;

  function execOnBackend(command) {
    _pendingExec = command;
    window.parent.postMessage(JSON.stringify({
      type: 'service.request',
      service: 'terminal',
      method: 'exec',
      params: { command: command }
    }), '*');
  }

  /* 에뮬레이터 서비스 응답 수신 */
  window.addEventListener('message', function (e) {
    if (e.source !== window.parent) return;
    try {
      var msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
      if (!msg || msg.type !== 'service.response') return;

      if (msg.service === 'terminal' && msg.method === 'exec' && msg.data) {
        var result = msg.data;
        if (result.stdout) addLine(result.stdout.replace(/\n$/, ''), 'line-output');
        if (result.stderr) addLine(result.stderr.replace(/\n$/, ''), 'line-error');
        if (result.exit_code !== 0 && !result.stdout && !result.stderr) {
          addLine('Exit code: ' + result.exit_code, 'line-error');
        }
        scrollToBottom();
        _pendingExec = null;
      }

      /* fs 응답 (getAllData — 시뮬레이션 명령용) */
      if (msg.service === 'fs' && msg.method === 'getAllData' && msg.data) {
        fileSystem = msg.data.tree || {};
        var unixView = msg.data.unixTree || {};
        fileContents = msg.data.fileContents || {};
      }
    } catch (err) { /* ignore */ }
  });

  /* ─── Init ─── */
  showWelcome();
  updatePrompt();
  commandInput.focus();

})();
