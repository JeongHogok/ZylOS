/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: JS 서비스 모듈 단위 테스트 (ES5 호환, Node.js 직접 실행)
 * 수행범위: services.js 핸들러, permissions.js, security.js, sandbox.js 핵심 경로
 * 의존방향: Node.js assert
 * SOLID: SRP — 서비스 로직 단위 테스트만 담당
 * ────────────────────────────────────────────────────────── */

'use strict';

var assert = require('assert');
var passed = 0;
var failed = 0;
var total = 0;

function test(name, fn) {
  total++;
  try {
    fn();
    passed++;
    console.log('  ✓ ' + name);
  } catch (e) {
    failed++;
    console.log('  ✗ ' + name + ': ' + e.message);
  }
}

function asyncTest(name, fn) {
  total++;
  return fn().then(function () {
    passed++;
    console.log('  ✓ ' + name);
  }).catch(function (e) {
    failed++;
    console.log('  ✗ ' + name + ': ' + (e.message || e));
  });
}

/* ─── Minimal browser globals for Node.js ─── */
global.window = global;
Object.defineProperty(global, 'navigator', {
  value: { vibrate: function () {}, getBattery: null, geolocation: null },
  writable: true, configurable: true
});
global.localStorage = (function () {
  var store = {};
  return {
    getItem: function (k) { return store[k] || null; },
    setItem: function (k, v) { store[k] = String(v); },
    removeItem: function (k) { delete store[k]; }
  };
})();
global.Promise = global.Promise || require('es6-promise').Promise;
global.Notification = function () {};
global.Notification.permission = 'granted';
global.AudioContext = undefined;
global.webkitAudioContext = undefined;

/* ─── ZylServiceModules namespace ─── */
global.ZylServiceModules = {};
global.ZylSecurity = { isHiddenFromListing: function () { return false; }, isProtectedPath: function () { return false; } };
global.ZylSandbox = { isAllowedDomain: function () { return true; } };
global.ZylPermissions = { SYSTEM_APPS: ['com.zylos.home', 'com.zylos.settings'], registerFromAppList: function () {}, setAppOverride: function () {}, checkPermission: function () { return true; } };
global.ZylAppRegistry = { register: function () {} };

/* ─── Load service modules ─── */
var path = require('path');
var servicesDir = path.join(__dirname, '..', 'apps', 'system', 'services');
var fs = require('fs');

var moduleFiles = [
  'fs.js', 'device.js', 'storage.js', 'apps.js', 'settings.js',
  'terminal.js', 'wifi.js', 'bluetooth.js', 'network.js', 'browser.js',
  'notification.js', 'power.js', 'display.js', 'input.js', 'sensors.js',
  'location.js', 'telephony.js', 'contacts.js', 'messaging.js', 'usb.js',
  'user.js', 'credential.js', 'appstore.js', 'updater.js', 'sandbox.js',
  'logger.js', 'accessibility.js', 'audio.js'
];

/* Provide window.addEventListener stub before loading modules */
global.window.addEventListener = function () {};
global.window.innerWidth = 1080;
global.window.innerHeight = 2400;

moduleFiles.forEach(function (f) {
  var code = fs.readFileSync(path.join(servicesDir, f), 'utf8');
  try {
    eval(code);
  } catch (e) {
    console.error('Failed to load ' + f + ': ' + e.message);
    process.exit(1);
  }
});

var ns = global.ZylServiceModules;

/* ─── Mock invoker ─── */
function createMockInvoker(responses) {
  return function mockInvoke(cmd, args) {
    if (responses && responses[cmd]) {
      var result = responses[cmd];
      if (typeof result === 'function') return Promise.resolve(result(args));
      return Promise.resolve(result);
    }
    return Promise.resolve(null);
  };
}

/* ═══════════════════════════════════════════════════════
   Test Suite: Device Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Device Service');
(function () {
  var dev = ns.device({});
  var info = dev.getInfo();
  test('getInfo returns osVersion', function () {
    assert.strictEqual(info.osVersion, '0.1.0');
  });
  test('getInfo returns SoC', function () {
    assert.ok(info.soc.indexOf('RISC-V') !== -1);
  });
  test('getUptime returns non-negative', function () {
    return dev.getUptime().then(function (v) {
      assert.ok(v >= 0);
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Settings Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Settings Service');
var settingsTests = (function () {
  var invoke = createMockInvoker({ 'load_settings': { sound: { mediaVolume: 50 } }, 'save_settings': true });
  var svc = ns.settings({ invoke: invoke });

  return svc._loadFromBackend().then(function () {
    return asyncTest('getSetting returns loaded category', function () {
      return svc._getSetting('sound').then(function (s) {
        assert.strictEqual(s.mediaVolume, 50);
      });
    });
  }).then(function () {
    return asyncTest('updateSetting persists', function () {
      svc._updateSetting('display', 'brightness', 80);
      return svc._getSetting('display').then(function (d) {
        assert.strictEqual(d.brightness, 80);
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Logger Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Logger Service');
(function () {
  var logger = ns.logger({});

  test('log adds entry', function () {
    logger.log({ level: 'INFO', tag: 'test', message: 'hello' });
  });

  return logger.getRecent({ count: 10 }).then(function (entries) {
    test('getRecent returns logged entry', function () {
      assert.ok(entries.length > 0);
      assert.strictEqual(entries[entries.length - 1].message, 'hello');
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Display Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Display Service');
var displayTests = (function () {
  var disp = ns.display({});

  return asyncTest('getMode returns default resolution', function () {
    return disp.getMode().then(function (m) {
      assert.strictEqual(m.width, 1080);
      assert.strictEqual(m.height, 2400);
    });
  }).then(function () {
    return asyncTest('setRotation changes rotation', function () {
      return disp.setRotation({ rotation: 90 }).then(function (r) {
        assert.strictEqual(r, 90);
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Input Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Input Service');
var inputTests = (function () {
  var inp = ns.input({});

  return asyncTest('showKeyboard sets visible', function () {
    return inp.showKeyboard({ layout: 'ko' }).then(function (s) {
      assert.strictEqual(s.visible, true);
      assert.strictEqual(s.layout, 'ko');
    });
  }).then(function () {
    return asyncTest('hideKeyboard clears visible', function () {
      return inp.hideKeyboard().then(function (s) {
        assert.strictEqual(s.visible, false);
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Sensors Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Sensors Service');
var sensorsTests = (function () {
  var sens = ns.sensors({});

  return asyncTest('accelerometer returns 3 values', function () {
    return sens.getLatest({ type: 'accelerometer' }).then(function (d) {
      assert.strictEqual(d.type, 'accelerometer');
      assert.strictEqual(d.values.length, 3);
    });
  }).then(function () {
    return asyncTest('light returns positive lux', function () {
      return sens.getLatest({ type: 'light' }).then(function (d) {
        assert.ok(d.values[0] >= 0);
      });
    });
  }).then(function () {
    return asyncTest('setScenario changes scenario', function () {
      return sens.setScenario({ scenario: 'walking' }).then(function (r) {
        assert.strictEqual(r.scenario, 'walking');
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: USB Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ USB Service');
var usbTests = (function () {
  var usb = ns.usb({});

  return asyncTest('default mode is charging', function () {
    return usb.getMode().then(function (m) {
      assert.strictEqual(m, 'charging');
    });
  }).then(function () {
    return asyncTest('setMode changes mode', function () {
      return usb.setMode({ mode: 'mtp' }).then(function (m) {
        assert.strictEqual(m, 'mtp');
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Power Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Power Service');
var powerTests = (function () {
  var invoke = createMockInvoker({ 'power_get_state': { state: 'ACTIVE', battery: { level: 85, charging: false } } });
  var pwr = ns.power({ invoke: invoke });

  return asyncTest('setBrightness clamps to 0-100', function () {
    return pwr.setBrightness({ percent: 150 }).then(function (r) {
      assert.strictEqual(r.brightness, 100);
    });
  }).then(function () {
    return asyncTest('getState includes batteryLevel', function () {
      return pwr.getState().then(function (s) {
        assert.strictEqual(s.batteryLevel, 85);
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Audio Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Audio Service');
var audioTests = (function () {
  var invoke = createMockInvoker({ 'save_settings': true });
  var settingsMod = ns.settings({ invoke: invoke });
  var audio = ns.audio({ settingsSvc: settingsMod, invoke: invoke });

  return asyncTest('setVolume clamps to 0-100', function () {
    return audio.setVolume({ stream: 'media', value: 200 }).then(function (r) {
      assert.strictEqual(r.value, 100);
    });
  }).then(function () {
    return asyncTest('silentMode defaults to false', function () {
      return audio.getSilentMode().then(function (m) {
        assert.strictEqual(m, false);
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: FS Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ FS Service');
var fsTests = (function () {
  var invoke = createMockInvoker({
    'fs_read_dir': [{ name: 'test.txt', is_dir: false, size: 100 }],
    'fs_read_file': 'hello world'
  });
  var fsSvc = ns.fs({ invoke: invoke });

  return asyncTest('getDirectory returns entries', function () {
    return fsSvc.getDirectory({ path: '/' }).then(function (entries) {
      assert.strictEqual(entries.length, 1);
      assert.strictEqual(entries[0].name, 'test.txt');
    });
  }).then(function () {
    return asyncTest('getFileContent returns content', function () {
      return fsSvc.getFileContent({ path: '/test.txt' }).then(function (c) {
        assert.strictEqual(c, 'hello world');
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Test Suite: Updater Service
   ═══════════════════════════════════════════════════════ */
console.log('\n■ Updater Service');
var updaterTests = (function () {
  var upd = ns.updater({ deviceRef: { osVersion: '0.1.0' } });

  return asyncTest('checkForUpdate returns current version', function () {
    return upd.checkForUpdate().then(function (r) {
      assert.strictEqual(r.available, false);
      assert.strictEqual(r.currentVersion, '0.1.0');
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Wait for all async tests and report
   ═══════════════════════════════════════════════════════ */
Promise.all([settingsTests, displayTests, inputTests, sensorsTests,
             usbTests, powerTests, audioTests, fsTests, updaterTests])
  .then(function () {
    console.log('\n════════════════════════');
    console.log('Total: ' + total + '  Passed: ' + passed + '  Failed: ' + failed);
    console.log('════════════════════════');
    if (failed > 0) process.exit(1);
  }).catch(function (e) {
    console.error('Test runner error:', e);
    process.exit(1);
  });
