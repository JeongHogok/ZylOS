/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Performance Gate
 *
 * 역할: 앱 성능 예산 검사 — 시작 시간·메인스레드 블로킹·메모리·DOM 크기,
 *       시작 중 미처리 예외(스크립트가 죽으면 다른 지표는 멀쩡해 보인다)
 * 수행범위: 헤드리스 Chrome 을 CDP 로 직접 구동(외부 패키지 없음), CPU 스로틀로
 *           저사양 SoC 근사, 앱마다 반복 측정 후 중앙값을 budgets.json 과 비교
 * 의존방향: Node 22+ (내장 WebSocket), Chrome/Chromium
 * SOLID: SRP — 측정과 판정만. 예산 값은 budgets.json 이 소유
 *
 * 사용: node tests/perf/run.js [--update] [--app=<name>] [--runs=N]
 *   --update  측정값에 여유를 둔 예산을 budgets.json 에 다시 쓴다
 *
 * shortcut: Chrome 은 실기기 엔진(WebKitGTK)이 아니다. 앱 코드의 JS/DOM/CSS
 *   비용 회귀는 잡지만 엔진 고유의 렌더링 차이는 못 본다. CPU 스로틀 배율도
 *   추정치다 — 실기기 측정이 생기면 budgets.json 의 throttle 을 보정할 것.
 * ────────────────────────────────────────────────────────── */
'use strict';

const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..');
const BUDGETS_FILE = path.join(__dirname, 'budgets.json');
const args = process.argv.slice(2);
const UPDATE = args.includes('--update');
const ONLY = (args.find((a) => a.startsWith('--app=')) || '').slice(6);
const RUNS = parseInt((args.find((a) => a.startsWith('--runs=')) || '--runs=3').slice(7), 10);

const budgets = JSON.parse(fs.readFileSync(BUDGETS_FILE, 'utf8'));

/* ── Static file server for the OS image ───────────────────── */
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
               '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png' };
function serve() {
  return new Promise((resolve) => {
    const srv = http.createServer((req, res) => {
      const rel = decodeURIComponent(req.url.split('?')[0]);
      const file = path.join(ROOT, rel);
      if (!file.startsWith(ROOT)) { res.writeHead(403); return res.end(); }
      fs.readFile(file, (err, buf) => {
        if (err) { res.writeHead(404); return res.end(); }
        res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
        res.end(buf);
      });
    });
    srv.listen(0, '127.0.0.1', () => resolve(srv));
  });
}

/* ── Chrome over the DevTools protocol ─────────────────────── */
function findChrome() {
  const candidates = [
    process.env.CHROME_PATH,
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/usr/bin/google-chrome', '/usr/bin/google-chrome-stable',
    '/usr/bin/chromium', '/usr/bin/chromium-browser',
  ].filter(Boolean);
  const hit = candidates.find((p) => fs.existsSync(p));
  if (!hit) throw new Error('Chrome not found — set CHROME_PATH');
  return hit;
}

async function launchChrome() {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'zyl-perf-'));
  const proc = spawn(findChrome(), [
    '--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-extensions',
    /* The device renders on the CPU today (llvmpipe), so measure without GPU */
    '--disable-gpu', '--disable-background-timer-throttling',
    /* Ubuntu 24.04 restricts unprivileged user namespaces, which Chrome's
       sandbox needs. Only our own repository is loaded, so CI drops it. */
    ...(process.env.CI ? ['--no-sandbox'] : []),
    /* No crash reporter: it is a separate process that outlives the browser
       and keeps writing into the profile */
    '--disable-crash-reporter', '--disable-breakpad',
  ], { stdio: ['ignore', 'ignore', 'pipe'], detached: true });
  /* Keep Chrome's last words: a launch failure on CI was otherwise just ENOENT */
  let stderr = '';
  let exited = null;
  proc.stderr.on('data', (d) => { stderr = (stderr + d).slice(-4000); });
  proc.on('exit', (code, sig) => { exited = sig || code; });
  const portFile = path.join(profile, 'DevToolsActivePort');
  /* A cold start on a fresh CI image took longer than the 10 s this used to allow */
  for (let i = 0; i < 300 && exited === null && !fs.existsSync(portFile); i++) await sleep(100);
  if (!fs.existsSync(portFile)) {
    proc.kill();
    throw new Error(`Chrome did not start (${exited === null ? 'timed out after 30 s' : 'exited: ' + exited})\n${stderr}`);
  }
  const port = fs.readFileSync(portFile, 'utf8').split('\n')[0];
  const ver = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  return { proc, profile, ws: ver.webSocketDebuggerUrl };
}

function cdp(url) {
  const ws = new WebSocket(url);
  let seq = 0;
  const pending = new Map();
  const listeners = [];
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) reject(new Error(msg.error.message)); else resolve(msg.result);
    } else if (msg.method) {
      listeners.forEach((l) => l(msg));
    }
  };
  const ready = new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
  return {
    ready,
    send(method, params = {}, sessionId) {
      const id = ++seq;
      ws.send(JSON.stringify({ id, method, params, sessionId }));
      return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
    },
    once(method, sessionId, timeoutMs = 15000) {
      return new Promise((resolve, reject) => {
        const t = setTimeout(() => reject(new Error(`timeout waiting for ${method}`)), timeoutMs);
        const l = (m) => {
          if (m.method === method && m.sessionId === sessionId) {
            clearTimeout(t); listeners.splice(listeners.indexOf(l), 1); resolve(m.params);
          }
        };
        listeners.push(l);
      });
    },
    listen(fn) { listeners.push(fn); },
    unlisten(fn) { const i = listeners.indexOf(fn); if (i !== -1) listeners.splice(i, 1); },
    close() { ws.close(); },
  };
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* Installed before any app script runs: long tasks and paints are only
   observable if the observer exists when they happen. */
const PROBE = `
  window.__zylPerf = { longTasks: [], fcp: null };
  try {
    new PerformanceObserver(function (l) {
      l.getEntries().forEach(function (e) { window.__zylPerf.longTasks.push(e.duration); });
    }).observe({ entryTypes: ['longtask'] });
    new PerformanceObserver(function (l) {
      l.getEntries().forEach(function (e) {
        if (e.name === 'first-contentful-paint') window.__zylPerf.fcp = e.startTime;
      });
    }).observe({ type: 'paint', buffered: true });
  } catch (e) {}
`;

async function measureOnce(browser, origin, app) {
  const { targetId } = await browser.send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await browser.send('Target.attachToTarget', { targetId, flatten: true });
  const S = (m, p) => browser.send(m, p, sessionId);
  /* A script that dies at load leaves an app looking fine to every other
     metric — fast, small DOM — while nothing works. Record uncaught errors. */
  const exceptions = [];
  const onMsg = (m) => {
    if (m.sessionId === sessionId && m.method === 'Runtime.exceptionThrown') {
      const d = m.params.exceptionDetails || {};
      exceptions.push(((d.exception && d.exception.description) || d.text || 'error').split('\n')[0]);
    }
  };
  browser.listen(onMsg);
  try {
    await S('Runtime.enable');
    await S('Page.enable');
    await S('Performance.enable', { timeDomain: 'timeTicks' });
    await S('Emulation.setCPUThrottlingRate', { rate: budgets.throttle });
    await S('Emulation.setDeviceMetricsOverride', {
      width: budgets.viewport.width, height: budgets.viewport.height,
      deviceScaleFactor: 1, mobile: true,
    });
    await S('Page.addScriptToEvaluateOnNewDocument', { source: PROBE });

    const loaded = browser.once('Page.loadEventFired', sessionId);
    await S('Page.navigate', { url: `${origin}/apps/${app}/index.html` });
    await loaded;
    await sleep(budgets.settleMs);     /* let deferred init and first renders finish */

    const { result } = await S('Runtime.evaluate', {
      returnByValue: true,
      expression: `(function () {
        var nav = performance.getEntriesByType('navigation')[0];
        var lt = window.__zylPerf.longTasks;
        return {
          loadMs: nav ? nav.loadEventEnd - nav.startTime : null,
          fcpMs: window.__zylPerf.fcp,
          longTasks: lt.length,
          blockingMs: lt.reduce(function (s, d) { return s + Math.max(0, d - 50); }, 0)
        };
      })()`,
    });
    const { metrics } = await S('Performance.getMetrics');
    const m = Object.fromEntries(metrics.map((x) => [x.name, x.value]));
    return {
      exceptions,
      loadMs: result.value.loadMs,
      fcpMs: result.value.fcpMs,
      blockingMs: result.value.blockingMs,
      heapMB: m.JSHeapUsedSize / 1048576,
      domNodes: m.Nodes,
      layouts: m.LayoutCount,
    };
  } finally {
    browser.unlisten(onMsg);
    await browser.send('Target.closeTarget', { targetId }).catch(() => {});
  }
}

const median = (xs) => { const s = xs.filter((x) => x != null).sort((a, b) => a - b); return s.length ? s[Math.floor(s.length / 2)] : null; };

/* Counts are identical on every machine, so a tight margin is safe and catches
   DOM or layout bloat precisely. Times depend on the runner's CPU, so they get a
   floor: a budget derived from a fast laptop would fail on a slower CI box with
   no regression at all. */
const TIME_KEYS = ['loadMs', 'fcpMs', 'blockingMs'];
function budgetFromMeasurement(k, v) {
  const h = budgets.headroom;
  if (TIME_KEYS.includes(k)) {
    const b = Math.ceil((v * h.time) / 50) * 50;
    return Math.max(b, budgets.timeFloorsMs[k]);
  }
  if (k === 'heapMB') return Math.max(2, Math.ceil((v * h.heap) / 0.5) * 0.5);
  return Math.max(Math.ceil(v * h.count / 10) * 10, Math.ceil(v) + 20);
}

function budgetFor(app, key) {
  const o = (budgets.apps && budgets.apps[app]) || {};
  return o[key] !== undefined ? o[key] : budgets.defaults[key];
}

async function main() {
  const apps = fs.readdirSync(path.join(ROOT, 'apps'))
    .filter((d) => fs.existsSync(path.join(ROOT, 'apps', d, 'index.html')))
    .filter((d) => !ONLY || d === ONLY)
    .sort();

  const srv = await serve();
  const origin = `http://127.0.0.1:${srv.address().port}`;
  const chrome = await launchChrome();
  const browser = cdp(chrome.ws);
  await browser.ready;

  const KEYS = ['loadMs', 'fcpMs', 'blockingMs', 'heapMB', 'domNodes', 'layouts'];
  const results = {};
  let failures = 0;
  try {
    for (const app of apps) {
      const runs = [];
      for (let i = 0; i < RUNS; i++) runs.push(await measureOnce(browser, origin, app));
      results[app] = Object.fromEntries(KEYS.map((k) => [k, median(runs.map((r) => r[k]))]));
      results[app].exceptions = [...new Set(runs.flatMap((r) => r.exceptions))];
    }
  } finally {
    browser.close();
    /* Chrome keeps writing to its profile while shutting down, and so do its
       helper processes after the browser itself has gone: end the whole
       process group (detached above), then wait for the browser */
    const exited = new Promise((r) => chrome.proc.once('exit', r));
    try { process.kill(-chrome.proc.pid, 'SIGTERM'); } catch (e) { chrome.proc.kill(); }
    await Promise.race([exited, sleep(5000)]);
    srv.close();
    /* A temp directory left behind is not a measurement failure */
    try {
      fs.rmSync(chrome.profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
    } catch (e) {
      console.warn(`warning: could not remove ${chrome.profile}: ${e.code}`);
    }
  }

  if (UPDATE) {
    /* Lock in today's level with headroom, so the gate catches regressions
       without flaking on run-to-run noise. */
    budgets.apps = {};
    for (const app of apps) {
      budgets.apps[app] = {};
      for (const k of KEYS) {
        const v = results[app][k];
        if (v == null) continue;
        budgets.apps[app][k] = budgetFromMeasurement(k, v);
      }
    }
    fs.writeFileSync(BUDGETS_FILE, JSON.stringify(budgets, null, 2) + '\n');
    console.log(`budgets.json updated for ${apps.length} apps`);
  }

  const fmt = (v, k) => v == null ? '-' : (k === 'heapMB' ? v.toFixed(1) : Math.round(v));
  console.log(`\nApp performance (CPU ×${budgets.throttle}, ${budgets.viewport.width}x${budgets.viewport.height}, median of ${RUNS})`);
  console.log(['app'.padEnd(11), ...KEYS.map((k) => k.padStart(13))].join(''));
  for (const app of apps) {
    const row = [app.padEnd(11)];
    for (const k of KEYS) {
      const v = results[app][k];
      const b = budgetFor(app, k);
      const over = v != null && b !== undefined && v > b;
      if (over) failures++;
      row.push(`${fmt(v, k)}${over ? '!' : ''}/${b === undefined ? '-' : b}`.padStart(13));
    }
    console.log(row.join(''));
  }
  for (const app of apps) {
    if (results[app].exceptions.length) {
      failures++;
      console.log(`\n✗ ${app}: uncaught exception at startup — ${results[app].exceptions.join(' | ')}`);
    }
  }
  fs.writeFileSync(path.join(os.tmpdir(), 'zyl-perf-results.json'), JSON.stringify(results, null, 2));
  console.log(failures ? `\n✗ ${failures} metric(s) over budget (marked !)` : '\n✓ all apps within budget');
  process.exit(failures && !UPDATE ? 1 : 0);
}

main().catch((e) => { console.error(e); process.exit(2); });
