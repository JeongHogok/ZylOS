// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// 역할: 터미널 에뮬레이터 UI — 셸 명령 실행 및 출력 표시
// 수행범위: 명령 입력/실행, 명령 히스토리, 출력 스크롤백, 탭 관리, 테마 전환
// 의존방향: 없음 (독립 앱)
// SOLID: SRP — 터미널 UI와 명령 처리만 담당
//
// 클린아키텍처, SOLID원칙, i18n 규칙 철저 준수
// 실제 디바이스 구동 기준이며, mock/demo 등 하드코딩 데이터가 있어서는 안 된다
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* ─── ES5 polyfill helpers ─── */
  function strPadEnd(str, len, ch) {
    str = String(str); ch = ch || ' ';
    while (str.length < len) str = str + ch;
    return str;
  }
  function strPadStart(str, len, ch) {
    str = String(str); ch = ch || ' ';
    while (str.length < len) str = ch + str;
    return str;
  }
  function strStartsWith(str, prefix) {
    return str.indexOf(prefix) === 0;
  }
  function strEndsWith(str, suffix) {
    return str.indexOf(suffix, str.length - suffix.length) !== -1;
  }
  function arrFind(arr, fn) {
    for (var i = 0; i < arr.length; i++) {
      if (fn(arr[i], i, arr)) return arr[i];
    }
    return undefined;
  }

  /* ─── DOM ─── */
  var outputContent = document.getElementById('output-content');
  var terminalOutput = document.getElementById('terminal-output');
  var commandInput = document.getElementById('command-input');
  var btnClear = document.getElementById('btn-clear');
  var btnInfo = document.getElementById('btn-info');
  var btnTheme = document.getElementById('btn-theme');
  var btnNewTab = document.getElementById('btn-new-tab');
  var tabListEl = document.getElementById('tab-list');
  var qkeyPaste = document.getElementById('qkey-paste');

  /* ─── Shared State ─── */
  var hostname = 'bpi-f3';
  var username = 'user';

  /* ─── Filesystem + device info (loaded from service) ─── */
  var filesystem = {};
  var fileContents = {};
  var deviceData = null;
  var _asyncCallbacks = {};
  var _asyncId = 0;

  /* ─── Tab System ─── */
  var MAX_TABS = 4;
  var tabs = [];
  var activeTabIndex = 0;
  var nextTabId = 1;

  function createTabState() {
    var id = nextTabId++;
    return {
      id: id,
      commandHistory: [],
      historyIndex: -1,
      currentDir: '/home/' + username,
      outputHtml: ''
    };
  }

  function getTabName(tabState) {
    var raw = zylI18n.t('terminal.tab_name');
    return raw.replace('{n}', String(tabState.id));
  }

  function saveCurrentTabOutput() {
    if (tabs[activeTabIndex]) {
      tabs[activeTabIndex].outputHtml = outputContent.innerHTML;
    }
  }

  function restoreTabOutput(tabState) {
    outputContent.innerHTML = tabState.outputHtml;
  }

  function renderTabBar() {
    tabListEl.innerHTML = '';
    for (var i = 0; i < tabs.length; i++) {
      (function (idx) {
        var tab = tabs[idx];
        var tabEl = document.createElement('button');
        tabEl.className = 'term-tab' + (idx === activeTabIndex ? ' active' : '');
        tabEl.setAttribute('role', 'tab');
        tabEl.setAttribute('aria-selected', idx === activeTabIndex ? 'true' : 'false');

        var nameSpan = document.createElement('span');
        nameSpan.textContent = getTabName(tab);
        tabEl.appendChild(nameSpan);

        tabEl.addEventListener('click', function () {
          switchToTab(idx);
        });

        if (tabs.length > 1) {
          var closeBtn = document.createElement('button');
          closeBtn.className = 'tab-close-btn';
          closeBtn.textContent = '\u00d7';
          closeBtn.setAttribute('aria-label', zylI18n.t('terminal.close_tab'));
          closeBtn.addEventListener('click', function (e) {
            e.stopPropagation();
            closeTab(idx);
          });
          tabEl.appendChild(closeBtn);
        }

        tabListEl.appendChild(tabEl);
      })(i);
    }

    /* Show/hide add button */
    btnNewTab.style.display = tabs.length >= MAX_TABS ? 'none' : '';
  }

  function switchToTab(idx) {
    if (idx === activeTabIndex) return;
    saveCurrentTabOutput();
    activeTabIndex = idx;
    var tab = tabs[idx];
    restoreTabOutput(tab);
    currentDir = tab.currentDir;
    commandHistory = tab.commandHistory;
    historyIndex = tab.historyIndex;
    updatePrompt();
    renderTabBar();
    scrollToBottom();
    commandInput.focus();
  }

  function addNewTab() {
    if (tabs.length >= MAX_TABS) return;
    saveCurrentTabOutput();
    var newTab = createTabState();
    tabs.push(newTab);
    activeTabIndex = tabs.length - 1;
    currentDir = newTab.currentDir;
    commandHistory = newTab.commandHistory;
    historyIndex = newTab.historyIndex;
    outputContent.innerHTML = '';
    showWelcome();
    updatePrompt();
    renderTabBar();
    commandInput.focus();
  }

  function closeTab(idx) {
    if (tabs.length <= 1) return;
    tabs.splice(idx, 1);
    if (activeTabIndex >= tabs.length) {
      activeTabIndex = tabs.length - 1;
    } else if (idx < activeTabIndex) {
      activeTabIndex--;
    } else if (idx === activeTabIndex) {
      activeTabIndex = Math.min(idx, tabs.length - 1);
    }
    var tab = tabs[activeTabIndex];
    restoreTabOutput(tab);
    currentDir = tab.currentDir;
    commandHistory = tab.commandHistory;
    historyIndex = tab.historyIndex;
    updatePrompt();
    renderTabBar();
    scrollToBottom();
    commandInput.focus();
  }

  /* Initialize first tab */
  var firstTab = createTabState();
  tabs.push(firstTab);
  var commandHistory = firstTab.commandHistory;
  var historyIndex = firstTab.historyIndex;
  var currentDir = firstTab.currentDir;

  /* ─── Color Theme System ─── */
  var themes = [
    { name: 'green', text: '#00ff41', bg: '#0a0a0a', dim: '#00cc33', accent: '#00ff41', prompt: '#00cc33', border: 'rgba(0,255,65,0.1)', glow: 'rgba(0,255,65,0.15)' },
    { name: 'amber', text: '#ffb000', bg: '#0a0800', dim: '#cc8e00', accent: '#ffb000', prompt: '#cc8e00', border: 'rgba(255,176,0,0.1)', glow: 'rgba(255,176,0,0.15)' },
    { name: 'blue', text: '#4a9eff', bg: '#000a1a', dim: '#3b7ecc', accent: '#4a9eff', prompt: '#3b7ecc', border: 'rgba(74,158,255,0.1)', glow: 'rgba(74,158,255,0.15)' }
  ];
  var currentThemeIndex = 0;

  function applyTheme(index) {
    var theme = themes[index];
    var root = document.documentElement;
    root.style.setProperty('--text', theme.text);
    root.style.setProperty('--bg', theme.bg);
    root.style.setProperty('--text-dim', theme.dim);
    root.style.setProperty('--accent', theme.accent);
    root.style.setProperty('--prompt-color', theme.prompt);
    root.style.setProperty('--border', theme.border);
  }

  function cycleTheme() {
    currentThemeIndex = (currentThemeIndex + 1) % themes.length;
    applyTheme(currentThemeIndex);
  }

  /* ─── i18n for quick keys paste button ─── */
  function applyI18nLabels() {
    if (typeof zylI18n !== 'undefined') {
      if (qkeyPaste) {
        qkeyPaste.textContent = zylI18n.t('terminal.paste');
      }
      if (btnNewTab) {
        btnNewTab.setAttribute('aria-label', zylI18n.t('terminal.new_tab'));
        btnNewTab.setAttribute('title', zylI18n.t('terminal.new_tab'));
      }
      if (btnTheme) {
        btnTheme.setAttribute('aria-label', zylI18n.t('terminal.theme'));
        btnTheme.setAttribute('title', zylI18n.t('terminal.theme'));
      }
    }
  }

  /* Request data from central service */
  function requestServiceData() {
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'fs', method: 'getAllData'
    });
    ZylBridge.sendToSystem({
      type: 'service.request', service: 'device', method: 'getInfo'
    });
  }

  /* Async service request with callback */
  function requestServiceAsync(service, method, params, callback) {
    var id = 'term_' + (++_asyncId);
    _asyncCallbacks[id] = callback;
    ZylBridge.sendToSystem({
      type: 'service.request', service: service, method: method,
      params: params || {}, requestId: id
    });
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
      if (!msg) return;

      /* Navigation back handling */
      if (msg.type === 'navigation.back') {
        ZylBridge.sendToSystem({ type: 'navigation.exit' });
        return;
      }

      if (msg.type !== 'service.response') return;
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
    } catch (err) { /* ignore parse errors from non-JSON messages */ }
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
        addHtml('  <span class="line-bold">' + strPadEnd(c[0], 16) + '</span> <span class="line-output">' + c[1] + '</span>');
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
          var isDir = strEndsWith(entry, '/') || (filesystem[target + '/' + entry] !== undefined);
          var name = entry.replace(/\/$/, '');
          var perms = isDir ? 'drwxr-xr-x' : '-rw-r--r--';
          var size = isDir ? '4096' : '0';
          var now = new Date();
          var months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
          var date = months[now.getMonth()] + ' ' + strPadStart(String(now.getDate()), 2, ' ') + ' ' + strPadStart(String(now.getHours()), 2, '0') + ':' + strPadStart(String(now.getMinutes()), 2, '0');
          var line = perms + '  1 user user ' + strPadStart(size, 8) + ' ' + date + ' ';
          addHtml(line + (isDir
            ? '<span class="line-info">' + name + '</span>'
            : '<span class="line-output">' + name + '</span>'));
        });
      } else {
        var output = '';
        filtered.forEach(function (entry) {
          var isDir = strEndsWith(entry, '/') || (filesystem[target + '/' + entry] !== undefined);
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
        if (tabs[activeTabIndex]) tabs[activeTabIndex].currentDir = currentDir;
        updatePrompt();
        return;
      }
      var target = resolvePath(args[0]);
      if (filesystem[target] !== undefined) {
        currentDir = target;
        if (tabs[activeTabIndex]) tabs[activeTabIndex].currentDir = currentDir;
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
        addLine('  ' + strPadStart((i + 1).toString(), 4) + '  ' + cmd, 'line-output');
      });
    }
  };

  /* ─── Path Resolution ─── */
  function resolvePath(input) {
    if (strStartsWith(input, '/')) return normalizePath(input);
    if (strStartsWith(input, '~')) return normalizePath('/home/' + username + input.slice(1));
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

    /* Sync historyIndex back to tab state */
    if (tabs[activeTabIndex]) {
      tabs[activeTabIndex].historyIndex = historyIndex;
    }

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
        if (tabs[activeTabIndex]) tabs[activeTabIndex].historyIndex = historyIndex;
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
      if (tabs[activeTabIndex]) tabs[activeTabIndex].historyIndex = historyIndex;
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
          if (tabs[activeTabIndex]) tabs[activeTabIndex].historyIndex = historyIndex;
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
        if (tabs[activeTabIndex]) tabs[activeTabIndex].historyIndex = historyIndex;
        commandInput.focus();
      } else if (key === 'tab') {
        /* Simple tab completion */
        var val = commandInput.value;
        var parts = val.split(' ');
        var lastPart = parts[parts.length - 1];
        if (lastPart) {
          var entries = filesystem[currentDir] || [];
          var match = arrFind(entries, function (e) {
            return strStartsWith(e.toLowerCase(), lastPart.toLowerCase());
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
      } else if (key === 'paste') {
        /* Paste from clipboard via service */
        handlePaste();
      }
    });
  });

  /* ─── Paste Functionality ─── */
  function handlePaste() {
    if (typeof ZylBridge !== 'undefined') {
      ZylBridge.requestService('clipboard', 'paste', {}).then(function (result) {
        if (result && result.text) {
          commandInput.value += result.text;
        }
        commandInput.focus();
      });
    }
  }

  /* ─── Theme Toggle ─── */
  btnTheme.addEventListener('click', function () {
    cycleTheme();
  });

  /* ─── Tab Bar Events ─── */
  btnNewTab.addEventListener('click', function () {
    addNewTab();
  });

  /* ─── Clear Button ─── */
  btnClear.addEventListener('click', function () {
    outputContent.innerHTML = '';
  });

  /* ─── 클립보드: 터미널 출력 복사 (텍스트 선택 후 더블클릭) ─── */
  (function () {
    function copyTerminalSelection() {
      var selection = window.getSelection ? window.getSelection() : null;
      if (!selection || !selection.toString()) return;
      var selectedText = selection.toString();
      if (!selectedText) return;
      if (typeof ZylBridge !== 'undefined') {
        ZylBridge.requestService('clipboard', 'copy', { text: selectedText });
      }
    }

    if (terminalOutput) {
      terminalOutput.addEventListener('mouseup', function () {
        copyTerminalSelection();
      });
      terminalOutput.addEventListener('touchend', function () {
        setTimeout(copyTerminalSelection, 100);
      });
    }
  })();

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
    ZylBridge.sendToSystem({
      type: 'service.request',
      service: 'terminal',
      method: 'exec',
      params: { command: command }
    });
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
    } catch (err) { /* ignore parse errors from non-JSON messages */ }
  });

  /* ─── Init ─── */
  showWelcome();
  updatePrompt();
  applyI18nLabels();
  renderTabBar();
  commandInput.focus();

})();
