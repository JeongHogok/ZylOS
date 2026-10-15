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
  'wifi.js', 'bluetooth.js', 'network.js', 'appdata.js', 'clipboard.js',
  'notification.js', 'power.js', 'display.js', 'input.js', 'sensors.js',
  'location.js', 'telephony.js', 'contacts.js', 'messaging.js', 'usb.js',
  'user.js', 'credential.js', 'appstore.js', 'updater.js', 'sandbox.js',
  'logger.js', 'accessibility.js', 'audio.js', 'telemetry.js', 'netstats.js', 'bootctl.js', 'lock.js'
];

/* Provide window.addEventListener stub before loading modules */
global.window.addEventListener = function () {};
global.window.innerWidth = 1080;
global.window.innerHeight = 2400;

/* OS image identity, read by the device service */
eval(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system', 'release.js'), 'utf8'));

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
  test('the SoC is the virtual device profile\'s — nothing invented without one', function () {
    assert.strictEqual(info.soc, '');
    assert.strictEqual(info.kernel, '');
    dev._applyProfile({ soc: 'SpacemiT K1', ram: '4GB' });
    assert.strictEqual(dev.getInfo().soc, 'SpacemiT K1 (RISC-V)');
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

  return asyncTest('setBrightness clamps to 0-100 and shows in getState', function () {
    return pwr.setBrightness({ percent: 150 }).then(function (r) {
      assert.strictEqual(r, true);
      return pwr.getState();
    }).then(function (s) {
      assert.strictEqual(s.brightness, 100);
    });
  }).then(function () {
    return asyncTest('getState includes batteryLevel', function () {
      return pwr.getState().then(function (s) {
        assert.strictEqual(s.batteryLevel, 85);
        assert.strictEqual(s.batteryPresent, true);
      });
    });
  }).then(function () {
    /* A host without a battery: the device has none — no invented level */
    var none = ns.power({ invoke: createMockInvoker({ 'power_get_state': { state: 'ACTIVE', battery: { present: false } } }) });
    return asyncTest('no battery is reported as none, not as a level', function () {
      return Promise.all([none.getState(), none.getCapabilities()]).then(function (r) {
        assert.strictEqual(r[0].batteryPresent, false);
        assert.strictEqual(r[0].batteryLevel, undefined);
        assert.strictEqual(r[0].plugged, true);
        assert.deepStrictEqual(r[1], { backlight: true, battery: false });
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
  }).then(function () {
    return asyncTest('a third-party app works in its own container, system apps in the shared files', function () {
      var calls = [];
      var scoped = ns.fs({ SYSTEM_APPS: ['com.zylos.files'], invoke: function (cmd, a) {
        calls.push([cmd, a.container]);
        return Promise.resolve([]);
      } });
      return Promise.all([
        scoped.writeFile({ path: 'Documents/a.txt', content: 'x', container: 'com.zylos.files' }, 'com.example.app'),
        scoped.getDirectory({ path: '/' }, 'com.zylos.files'),
        scoped.rename({ oldPath: 'a', newPath: 'b' }, 'com.example.app')
      ]).then(function () {
        assert.deepStrictEqual(calls, [['fs_write_file', 'com.example.app'], ['fs_read_dir', undefined],
                                       ['fs_rename', 'com.example.app']]);   /* the request cannot pick the root */
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

  /* The device daemon's answer shape; the emulator cannot install updates */
  return asyncTest('updater reports the running version and that it cannot install here', function () {
    return Promise.all([upd.getState(), upd.check(), upd.install()]).then(function (rs) {
      rs.forEach(function (r) {
        assert.strictEqual(r.state, 'unsupported');
        assert.strictEqual(r.version, '0.1.0');
        assert.strictEqual(r.available, '');
      });
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Form-factor policy + directional focus navigation
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylFormFactor / ZylFocusNav');

(function () {
  /* DOM stubs — only what apply() touches */
  var htmlClasses = {};
  var htmlAttrs = {};
  var htmlVars = {};
  var htmlZoom = null;
  global.document = {
    documentElement: {
      classList: {
        add: function (c) { htmlClasses[c] = true; },
        remove: function (c) { delete htmlClasses[c]; }
      },
      setAttribute: function (k, v) { htmlAttrs[k] = v; },
      style: {
        setProperty: function (k, v) { htmlVars[k] = v; },
        set zoom(v) { htmlZoom = v; },
        get zoom() { return htmlZoom; }
      }
    },
    addEventListener: function () {},
    querySelectorAll: function () { return []; }
  };

  var pointerMode = 'coarse';
  global.window.location = { search: '' };
  global.window.matchMedia = function (q) {
    return {
      matches: (q.indexOf('pointer: fine') !== -1 && pointerMode === 'fine') ||
               (q.indexOf('pointer: none') !== -1 && pointerMode === 'none')
    };
  };

  var ffCode = fs.readFileSync(path.join(__dirname, '..', 'apps', 'shared', 'form-factor.js'), 'utf8');
  eval(ffCode);
  var fnCode = fs.readFileSync(path.join(__dirname, '..', 'apps', 'shared', 'focus-nav.js'), 'utf8');
  eval(fnCode);

  var FF = global.ZylFormFactor;
  var FN = global.ZylFocusNav;

  function geom(w, h, pointer) {
    global.window.innerWidth = w;
    global.window.innerHeight = h;
    pointerMode = pointer || 'coarse';
  }

  /* ─── detect() ─── */
  test('detect: 1080x2400 touch → phone', function () {
    geom(1080, 2400);
    assert.strictEqual(FF.detect(), 'phone');
  });

  test('detect: 454x454 → watch', function () {
    geom(454, 454);
    assert.strictEqual(FF.detect(), 'watch');
  });

  test('detect: 1920x1080 no pointer → tv', function () {
    geom(1920, 1080, 'none');
    assert.strictEqual(FF.detect(), 'tv');
  });

  test('detect: 1920x1080 fine pointer → desktop', function () {
    geom(1920, 1080, 'fine');
    assert.strictEqual(FF.detect(), 'desktop');
  });

  test('detect: 800x1280 touch → tablet', function () {
    geom(800, 1280);
    assert.strictEqual(FF.detect(), 'tablet');
  });

  /* ─── container-reported input hardware ─── */
  function withInput(q, fn) {
    var prev = global.window.location.search;
    global.window.location.search = q;
    try { fn(); } finally { global.window.location.search = prev; }
  }

  test('hardwareInput reads the container hint from the app URL', function () {
    withInput('?v=123&zylInput=dpad', function () {
      assert.strictEqual(FF.hardwareInput(), 'dpad');
    });
    withInput('?v=123', function () {
      assert.strictEqual(FF.hardwareInput(), null);
    });
  });

  test('input hardware outranks pointer media queries', function () {
    /* A host mouse answers "pointer: fine", so geometry alone would say
       desktop; the container says the device only has a remote. */
    geom(1920, 1080, 'fine');
    assert.strictEqual(FF.detect(), 'desktop');
    withInput('?zylInput=dpad', function () {
      assert.strictEqual(FF.detect(), 'tv');
    });
  });

  test('input hardware distinguishes the geometry-blind form factors', function () {
    geom(1080, 2400);
    withInput('?zylInput=crown', function () {
      assert.strictEqual(FF.detect(), 'watch');
    });
    withInput('?zylInput=touch-rotary', function () {
      assert.strictEqual(FF.detect(), 'auto');
    });
    withInput('?zylInput=touch-keypad', function () {
      assert.strictEqual(FF.detect(), 'pda');
    });
    /* A D-pad on a small panel is an appliance, not a TV. */
    geom(1024, 600, 'none');
    withInput('?zylInput=dpad', function () {
      assert.strictEqual(FF.detect(), 'appliance');
    });
  });

  test('plain touch hint falls through to geometry', function () {
    geom(1920, 1080, 'none');
    withInput('?zylInput=touch', function () {
      assert.strictEqual(FF.detect(), 'tv');
    });
  });

  /* ─── the device's declared class (the WAM: the image's build profile) ─── */
  function declare(id) {
    global.window.__zylFormFactor = id || undefined;
    return FF.reload();
  }

  test('the declared class wins over detection', function () {
    geom(1080, 2400);                   /* would detect as phone */
    assert.strictEqual(declare('auto').id, 'auto');
  });

  test('a declared class that is not a profile is ignored', function () {
    geom(1080, 2400);
    assert.strictEqual(declare('toaster').id, 'phone');
    assert.strictEqual(FF.declared(), null);
  });

  test('the device class is not a user setting: no override, nothing stored', function () {
    ['setOverride', 'getOverride', 'clearOverride'].forEach(function (f) {
      assert.strictEqual(typeof FF[f], 'undefined', f);
    });
    assert.ok(ffCode.indexOf('localStorage') === -1);
  });

  /* ─── apply() ─── */
  test('apply writes form-factor class and nav model onto <html>', function () {
    declare('tv');
    assert.strictEqual(htmlAttrs['data-form-factor'], 'tv');
    assert.strictEqual(htmlAttrs['data-nav-model'], 'focus');
    assert.strictEqual(htmlClasses['ff-tv'], true);
    assert.strictEqual(htmlClasses['ff-auto'], undefined, 'previous class must be removed');
    assert.strictEqual(htmlVars['--ff-touch-target'], '64px');
  });

  test('apply sets root zoom from the profile (zoom ignores var())', function () {
    declare('tv');
    assert.strictEqual(htmlZoom, '1.8', 'tv must scale the root');
    declare('phone');
    assert.strictEqual(htmlZoom, '', 'phone must not scale the root');
    declare('tv');
  });

  /* ─── supportsApp() ─── */
  test('supportsApp: "*" is universal', function () {
    assert.strictEqual(FF.supportsApp(['*']), true);
  });

  test('supportsApp: phone-only app is unavailable on tv', function () {
    assert.strictEqual(FF.getId(), 'tv');
    assert.strictEqual(FF.supportsApp(['phone', 'tablet']), false);
    assert.strictEqual(FF.supportsApp(['tv', 'desktop']), true);
  });

  test('supportsApp: missing declaration stays available (legacy manifests)', function () {
    assert.strictEqual(FF.supportsApp([]), true);
    assert.strictEqual(FF.supportsApp(null), true);
  });

  declare(null);

  test('apply(target) scopes the form factor and leaves <html> alone', function () {
    /* The compositor hosts OS UI (the keyboard) in its own document; scaling
       <html> there would scale the emulator chrome with it. */
    var scopedClasses = {};
    var scopedAttrs = {};
    var scopedZoom = null;
    var container = {
      classList: {
        add: function (c) { scopedClasses[c] = true; },
        remove: function (c) { delete scopedClasses[c]; }
      },
      setAttribute: function (k, v) { scopedAttrs[k] = v; },
      style: {
        setProperty: function () {},
        set zoom(v) { scopedZoom = v; },
        get zoom() { return scopedZoom; }
      }
    };

    /* Nothing declared, so the hardware hint decides — as in the emulator.
       A declared class would outrank it; that precedence is tested separately. */
    declare(null);
    geom(1080, 2400);
    FF.reload();
    assert.strictEqual(htmlAttrs['data-form-factor'], 'phone',
      'baseline: <html> is a phone');
    var htmlZoomBefore = htmlZoom;

    FF.setHardwareInput('dpad');
    geom(1920, 1080, 'fine');
    FF.reload(container);

    assert.strictEqual(scopedAttrs['data-form-factor'], 'tv');
    assert.strictEqual(scopedClasses['ff-tv'], true);
    assert.strictEqual(scopedZoom, '1.8');
    assert.strictEqual(htmlZoom, htmlZoomBefore, '<html> zoom must not change');
    assert.strictEqual(htmlAttrs['data-form-factor'], 'phone',
      '<html> form factor must not change');

    FF.setHardwareInput(null);
    declare(null);
    geom(1080, 2400);
    FF.reload();
  });

  test('focus-nav init is idempotent (no duplicate keydown listener)', function () {
    declare('tv');
    var added = 0;
    var realAdd = global.document.addEventListener;
    global.document.addEventListener = function (type) {
      if (type === 'keydown') added++;
      return realAdd.apply(this, arguments);
    };
    var alreadyInstalled = FN.isInstalled();
    FN.init();
    FN.init();
    FN.init();
    global.document.addEventListener = realAdd;
    assert.strictEqual(added, alreadyInstalled ? 0 : 1,
      'three init() calls must register the keydown listener at most once');
    assert.strictEqual(FN.isInstalled(), true);
  });

  /* ─── launcher filter: the predicate home.js applies to the app list ─── */
  test('launcher filter hides apps the device class does not declare', function () {
    declare('tv');
    var appList = [
      { id: 'com.zylos.home',     formFactors: ['*'] },
      { id: 'com.example.desk',   formFactors: ['phone', 'tablet', 'desktop', 'pda'] },
      { id: 'com.zylos.phone',    formFactors: ['phone', 'tablet', 'auto', 'watch', 'pda'] },
      { id: 'com.zylos.browser',  formFactors: ['phone', 'tablet', 'tv', 'desktop', 'pda'] },
      { id: 'com.zylos.legacy' }   /* no declaration → stays visible */
    ];
    var visible = appList.filter(function (a) {
      return FF.supportsApp(a.formFactors);
    }).map(function (a) { return a.id; });
    assert.deepStrictEqual(visible,
      ['com.zylos.home', 'com.zylos.browser', 'com.zylos.legacy']);
  });

  /* ─── focus-nav spatial scoring ─── */
  var origin = { left: 100, top: 100, width: 100, height: 50 };

  test('_score: rejects candidates opposite to the direction', function () {
    var leftOf = { left: 0, top: 100, width: 100, height: 50 };
    assert.strictEqual(FN._score(origin, leftOf, 'right'), -1);
    assert.ok(FN._score(origin, leftOf, 'left') > 0);
  });

  test('_score: prefers the aligned target over a nearer misaligned one', function () {
    var aligned  = { left: 400, top: 100, width: 100, height: 50 };  /* far, same row */
    var offAxis  = { left: 250, top: 600, width: 100, height: 50 };  /* near, far row */
    assert.ok(FN._score(origin, aligned, 'right') < FN._score(origin, offAxis, 'right'),
      'aligned target must win');
  });

  test('_score: nearer wins when both are aligned', function () {
    var near = { left: 250, top: 100, width: 100, height: 50 };
    var far  = { left: 600, top: 100, width: 100, height: 50 };
    assert.ok(FN._score(origin, near, 'right') < FN._score(origin, far, 'right'));
  });

  test('_score: up and down are symmetric', function () {
    var above = { left: 100, top: 0,   width: 100, height: 50 };
    var below = { left: 100, top: 200, width: 100, height: 50 };
    assert.strictEqual(FN._score(origin, above, 'up'), FN._score(origin, below, 'down'));
    assert.strictEqual(FN._score(origin, above, 'down'), -1);
  });

  test('_score: unknown direction is rejected', function () {
    assert.strictEqual(FN._score(origin, origin, 'sideways'), -1);
  });
})();

/* ═══════════════════════════════════════════════════════
   Japanese / Chinese conversion (apps/keyboard/js/ime.js + dict/)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylIme');

(function () {
  var M = require(path.join(__dirname, '..', 'apps', 'keyboard', 'js', 'ime.js'));
  var I = M.ZylIme;
  /* The dictionary files call the global ZylImeDict.register, as in a page */
  global.ZylImeDict = M.ZylImeDict;
  require(path.join(__dirname, '..', 'apps', 'keyboard', 'dict', 'ja.js'));
  require(path.join(__dirname, '..', 'apps', 'keyboard', 'dict', 'zh.js'));

  /* Applies edits the way apps/shared/bridge.js applies input.text */
  function apply(field, e) {
    assert.strictEqual(field.slice(field.length - e.expect.length), e.expect, 'expect is before the caret');
    return field.slice(0, field.length - e.expect.length) + e.text;
  }
  function type(c, keys, field) {
    field = field || '';
    keys.split('').forEach(function (k) { field = apply(field, c.feed(k)); });
    return field;
  }
  function kana(keys) { return type(I.create('ja'), keys); }

  test('romaji becomes kana as it is typed', function () {
    assert.strictEqual(kana('nihongo'), 'にほんご');
    assert.strictEqual(kana('shinbun'), 'しんぶn');          /* last n still undecided */
    assert.strictEqual(kana('sinbunn'), 'しんぶん');
    assert.strictEqual(kana('kyouha'), 'きょうは');
    assert.strictEqual(kana('chotto'), 'ちょっと');
    assert.strictEqual(kana('matcha'), 'まっちゃ');
    assert.strictEqual(kana('tsukue'), 'つくえ');
    assert.strictEqual(kana('fairu'), 'ふぁいる');
    assert.strictEqual(kana('ko-hi-'), 'こーひー');
    assert.strictEqual(kana('xtsu'), 'っ');
    assert.strictEqual(kana('kan\'i'), 'かんい');
    assert.strictEqual(kana('k'), 'k');
  });

  test('katakana mirrors hiragana', function () {
    assert.strictEqual(I.toKatakana('こーひー'), 'コーヒー');
  });

  test('space-first candidate converts the longest match; the rest keeps composing', function () {
    var c = I.create('ja');
    var field = type(c, 'watashiha', '> ');
    assert.strictEqual(field, '> わたしは');
    var cands = c.candidates();
    assert.strictEqual(cands[0].text, '私');
    assert.ok(cands.some(function (x) { return x.text === 'わたしは'; }), 'as typed');
    assert.ok(cands.some(function (x) { return x.text === 'ワタシハ'; }), 'katakana');
    field = apply(field, c.choose(cands[0]));
    assert.strictEqual(field, '> 私は');
    assert.strictEqual(c.reading(), 'は', 'は still composing');
    field = apply(field, c.finish());
    assert.strictEqual(field, '> 私は');
    assert.strictEqual(c.isComposing(), false);
  });

  test('a whole-word match outranks its parts', function () {
    var c = I.create('ja');
    type(c, 'henkan');
    assert.strictEqual(c.candidates()[0].text, '変換');   /* trailing n read as ん */
  });

  test('Enter keeps the reading, a trailing n becomes ん', function () {
    var c = I.create('ja');
    var field = type(c, 'hon');
    assert.strictEqual(apply(field, c.finish()), 'ほん');
  });

  test('backspace removes one character of the reading', function () {
    var c = I.create('ja');
    var field = type(c, 'kana');
    field = apply(field, c.backspace());
    assert.strictEqual(field, 'か');
  });

  test('pinyin gives hanzi words, longest first', function () {
    var c = I.create('zh');
    var field = type(c, 'nihao');
    assert.strictEqual(field, 'nihao');
    var cands = c.candidates();
    assert.strictEqual(cands[0].text, '你好');
    assert.strictEqual(cands[cands.length - 1].text, 'nihao', 'latin kept as last resort');
    assert.strictEqual(apply(field, c.choose(cands[0])), '你好');
  });

  test('partial pinyin converts the known part', function () {
    var c = I.create('zh');
    var field = type(c, 'womenzou');
    var first = c.candidates()[0];
    assert.strictEqual(first.text, '我们');
    field = apply(field, c.choose(first));
    assert.strictEqual(field, '我们zou');
    assert.strictEqual(c.candidates()[0].text, '走');
  });

  test('ü is typed as v', function () {
    var c = I.create('zh');
    type(c, 'lvse');
    assert.strictEqual(c.candidates()[0].text, '绿色');
  });

  test('only lowercase letters (and ー for Japanese) feed the reading', function () {
    assert.ok(I.accepts('ja', 'a') && I.accepts('ja', '-') && I.accepts('zh', 'v'));
    assert.ok(!I.accepts('zh', '-') && !I.accepts('ja', 'A') && !I.accepts('ja', '1'));
  });
})();

/* ═══════════════════════════════════════════════════════
   Screen lock credential (JS lock service, contract of runtime/services/lock)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ lock service');

var lockTests = (function () {
  function make(store) {
    store = store || {};
    var settingsSvc = {
      _getSetting: function (cat) { return Promise.resolve(store[cat] || null); },
      _updateSetting: function (cat, k, v) { store[cat] = store[cat] || {}; store[cat][k] = v; }
    };
    return { svc: ns.lock({ settingsSvc: settingsSvc }), store: store };
  }
  function fail(svc, n) {
    var p = Promise.resolve();
    for (var i = 0; i < n; i++) p = p.then(function () { return svc.verifyPin({ pin: '0000' }); });
    return p;
  }

  return Promise.all([
    asyncTest('no PIN: verify passes, the PIN is never stored in plain text', function () {
      var t = make();
      return t.svc.verifyPin({ pin: '' }).then(function (r) {
        assert.strictEqual(r.ok, true);
        assert.strictEqual(r.hasPin, false);
        return t.svc.setPin({ current: '', pin: '4821' });
      }).then(function (r) {
        assert.strictEqual(r.ok, true);
        assert.ok(JSON.stringify(t.store).indexOf('4821') === -1);
        assert.ok(/^pbkdf2-sha256\$/.test(t.store.lock.hash));
        return t.svc.verifyPin({ pin: '4821' });
      }).then(function (r) { assert.strictEqual(r.ok, true); });
    }),
    asyncTest('five failures make the user wait, even for the right PIN', function () {
      var t = make();
      return t.svc.setPin({ current: '', pin: '4821' }).then(function () {
        return fail(t.svc, 5);
      }).then(function () {
        return t.svc.verifyPin({ pin: '4821' });
      }).then(function (r) {
        assert.strictEqual(r.ok, false);
        assert.strictEqual(r.reason, 'locked_out');
        assert.ok(r.retryAfterMs > 25000 && r.retryAfterMs <= 30000);
      });
    }),
    asyncTest('changing or removing needs the current PIN; format is checked', function () {
      var t = make();
      return t.svc.setPin({ current: '', pin: '12a4' }).then(function (r) {
        assert.strictEqual(r.reason, 'format');
        return t.svc.setPin({ current: '', pin: '4821' });
      }).then(function () {
        return t.svc.setPin({ current: '1111', pin: '' });
      }).then(function (r) {
        assert.strictEqual(r.reason, 'wrong_pin');
        return t.svc.setPin({ current: '4821', pin: '' });
      }).then(function (r) {
        assert.strictEqual(r.ok, true);
        assert.strictEqual(r.hasPin, false);
      });
    }),
    asyncTest('a plain-text PIN left by older builds is hashed and erased', function () {
      var t = make({ security: { pin: '2580' } });
      return t.svc.getState().then(function (r) {
        assert.strictEqual(r.hasPin, true);
        assert.strictEqual(t.store.security.pin, '');
        return t.svc.verifyPin({ pin: '2580' });
      }).then(function (r) { assert.strictEqual(r.ok, true); });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   apps/shared/bridge.js on the device (WAM / WebKitGTK)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylBridge on the device');

var deviceBridgeTests = (function () {
  var vm = require('vm');
  /* A page as the WAM builds it: messageHandlers.bridge plus the injected
     navigator.system, and window.postMessage delivering to listeners */
  function devicePage() {
    var sent = [], listeners = [];
    var win = {
      webkit: { messageHandlers: { bridge: { postMessage: function (j) { sent.push(JSON.parse(j)); } } } },
      navigator: { system: { app: { id: 'com.zylos.settings' } } },
      addEventListener: function (t, fn) { if (t === 'message') listeners.push(fn); },
      postMessage: function (data) {
        setTimeout(function () { listeners.forEach(function (fn) { fn({ data: data, source: win }); }); }, 0);
      },
      setTimeout: setTimeout, clearTimeout: clearTimeout
    };
    win.window = win; win.parent = win;
    var ctx = { window: win, navigator: win.navigator, document: { addEventListener: function () {} },
                console: console, setTimeout: setTimeout, clearTimeout: clearTimeout, JSON: JSON, Promise: Promise };
    vm.createContext(ctx);
    vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'shared', 'bridge.js'), 'utf8'), ctx);
    return { win: win, sent: sent, B: win.ZylBridge || ctx.ZylBridge, listeners: listeners };
  }

  return Promise.all([
    asyncTest('device messages go to the WAM, not the injected navigator.system', function () {
      var p = devicePage();
      assert.strictEqual(p.B.getIpcMode(), 'webkit');
      p.B.sendToSystem({ type: 'navigation.handled' });
      assert.strictEqual(p.sent.length, 1);
      return Promise.resolve();
    }),
    asyncTest('a service request gets its answer through the native callback', function () {
      var p = devicePage();
      var pr = p.B.requestService('settings', 'get', { category: 'display' });
      var msg = p.sent[0];
      assert.ok(msg.callbackId >= 1000000, 'callbackId set, clear of navigator.system ids');
      p.win['_zylCb_' + msg.callbackId]({ brightness: 35 });   /* what bridge.c evaluates */
      return pr.then(function (d) { assert.deepStrictEqual(d, { brightness: 35 }); });
    }),
    asyncTest('a native error answer rejects', function () {
      var p = devicePage();
      var pr = p.B.requestService('settings', 'get', { category: 'x' });
      p.win['_zylCb_' + p.sent[0].callbackId]({ error: true, message: 'Permission denied' });
      return pr.then(function () { throw new Error('resolved'); },
                     function (e) { assert.strictEqual(e.message, 'Permission denied'); });
    }),
    asyncTest('service events reach their listeners with the signal arguments', function () {
      var p = devicePage();
      var got = new Promise(function (resolve) {
        p.B.onServiceEvent('telephony', 'CallStateChanged', resolve);
      });
      p.B.onServiceEvent('telephony', 'SmsReceived', function () { throw new Error('wrong listener'); });
      p.win.postMessage(JSON.stringify({ type: 'service.event', service: 'telephony',
        event: 'CallStateChanged', data: ['IDLE', 'RINGING', '0101234'] }), '*');
      return got.then(function (d) { assert.deepStrictEqual(d, ['IDLE', 'RINGING', '0101234']); });
    }),
    asyncTest('fire-and-forget callers get the service.response the emulator sends', function () {
      var p = devicePage();
      var got = new Promise(function (resolve) {
        p.listeners.push(function (e) {
          var m = JSON.parse(e.data);
          if (m.type === 'service.response') resolve(m);
        });
      });
      p.B.requestServiceFire('settings', 'get', { category: 'wifi' });
      p.win['_zylCb_' + p.sent[0].callbackId]({ enabled: true });
      return got.then(function (m) {
        assert.strictEqual(m.service, 'settings');
        assert.deepStrictEqual(m.params, { category: 'wifi' });
        assert.deepStrictEqual(m.data, { enabled: true });
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Hangul 2-beolsik composition automaton
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylHangul');

(function () {
  var H = require(path.join(__dirname, '..', 'apps', 'keyboard', 'js', 'hangul.js'));

  /* Replays edits the way apps/shared/bridge.js applies input.text */
  function type(keys) {
    var c = H.create(), out = '';
    keys.split('').forEach(function (k) {
      var r = c.feed(k);
      if (!r) throw new Error('not jamo: ' + k);
      out = out.slice(0, out.length - r.replace) + r.text;
    });
    return out;
  }

  function typeThenBack(keys, n) {
    var c = H.create(), out = '';
    keys.split('').forEach(function (k) {
      var r = c.feed(k);
      out = out.slice(0, out.length - r.replace) + r.text;
    });
    var steps = [out];
    for (var i = 0; i < n; i++) {
      var b = c.backspace();
      if (!b) { steps.push('(not composing)'); break; }
      out = out.slice(0, out.length - b.replace) + b.text;
      steps.push(out);
    }
    return steps;
  }

  test('composes words', function () {
    assert.strictEqual(type('ㅎㅏㄴㄱㅡㄹ'), '한글');
    assert.strictEqual(type('ㅇㅏㄴㄴㅕㅇㅎㅏㅅㅔㅇㅛ'), '안녕하세요');
    assert.strictEqual(type('ㄷㅐㅎㅏㄴㅁㅣㄴㄱㅜㄱ'), '대한민국');
    assert.strictEqual(type('ㄱㅏㅁㅅㅏㅎㅏㅂㄴㅣㄷㅏ'), '감사합니다');
  });

  test('a final consonant moves to the next syllable before a vowel', function () {
    assert.strictEqual(type('ㅎㅏㄴㅏ'), '하나');
    assert.strictEqual(type('ㅅㅏㄹㅏㅇㅎㅐ'), '사랑해');
    assert.strictEqual(type('ㅇㅣㄹㄱㅇㅓㅇㅛ'), '읽어요');
  });

  test('all seven compound vowels', function () {
    assert.strictEqual(type('ㄱㅗㅏ'), '과');
    assert.strictEqual(type('ㄱㅗㅐ'), '괘');
    assert.strictEqual(type('ㄱㅗㅣ'), '괴');
    assert.strictEqual(type('ㄱㅜㅓ'), '궈');
    assert.strictEqual(type('ㄱㅜㅔ'), '궤');
    assert.strictEqual(type('ㄱㅜㅣ'), '귀');
    assert.strictEqual(type('ㅇㅡㅣ'), '의');
  });

  test('all eleven compound finals', function () {
    assert.strictEqual(type('ㄴㅓㄱㅅ'), '넋');
    assert.strictEqual(type('ㅇㅏㄴㅈ'), '앉');
    assert.strictEqual(type('ㅇㅏㄴㅎ'), '않');
    assert.strictEqual(type('ㄷㅏㄹㄱ'), '닭');
    assert.strictEqual(type('ㅅㅏㄹㅁ'), '삶');
    assert.strictEqual(type('ㄴㅓㄹㅂ'), '넓');
    assert.strictEqual(type('ㄱㅗㄹㅅ'), '곬');
    assert.strictEqual(type('ㅎㅏㄹㅌ'), '핥');
    assert.strictEqual(type('ㅇㅡㄹㅍ'), '읊');
    assert.strictEqual(type('ㅅㅣㄹㅎ'), '싫');
    assert.strictEqual(type('ㄱㅏㅂㅅ'), '값');
  });

  test('a compound final splits before a vowel, keeping its first half', function () {
    assert.strictEqual(type('ㄷㅏㄹㄱㅏ'), '달가');
    assert.strictEqual(type('ㄱㅏㅂㅅㅣ'), '갑시');
    assert.strictEqual(type('ㅇㅏㄴㅈㅏ'), '안자');
  });

  test('ㄸ ㅃ ㅉ never become finals', function () {
    assert.strictEqual(type('ㄱㅏㄸ'), '가ㄸ');
    assert.strictEqual(type('ㄱㅏㄸㅏ'), '가따');
    assert.strictEqual(type('ㅇㅏㅃㅏ'), '아빠');
    assert.strictEqual(type('ㄱㅏㅉㅏ'), '가짜');
  });

  test('ㄲ and ㅆ are valid finals', function () {
    assert.strictEqual(type('ㅇㅣㅆ'), '있');
    assert.strictEqual(type('ㅇㅣㅆㅇㅓ'), '있어');
    assert.strictEqual(type('ㅂㅏㄲ'), '밖');
  });

  test('lone jamo and non-combining sequences', function () {
    assert.strictEqual(type('ㄱ'), 'ㄱ');
    assert.strictEqual(type('ㅏ'), 'ㅏ');
    assert.strictEqual(type('ㄱㄴ'), 'ㄱㄴ');
    assert.strictEqual(type('ㅏㅓ'), 'ㅏㅓ');
    assert.strictEqual(type('ㅗㅏ'), 'ㅘ', 'a lone compound vowel still merges');
    assert.strictEqual(type('ㅎㅏㅓ'), '하ㅓ');
  });

  test('a compound cannot absorb a third key', function () {
    assert.strictEqual(type('ㄷㅏㄹㄱㅅ'), '닭ㅅ');
    assert.strictEqual(type('ㄱㅗㅏㅣ'), '과ㅣ');
  });

  test('backspace removes one jamo at a time', function () {
    assert.deepStrictEqual(typeThenBack('ㅎㅏㄴ', 4),
      ['한', '하', 'ㅎ', '', '(not composing)']);
  });

  test('backspace undoes a compound one key at a time', function () {
    assert.deepStrictEqual(typeThenBack('ㄱㅗㅏ', 3), ['과', '고', 'ㄱ', '']);
    assert.deepStrictEqual(typeThenBack('ㄷㅏㄹㄱ', 3), ['닭', '달', '다', 'ㄷ']);
  });

  test('backspace only touches the composing syllable', function () {
    /* 한 is committed when ㄱ starts a new syllable; backspace walks ㄱ only */
    assert.deepStrictEqual(typeThenBack('ㅎㅏㄴㄱ', 2),
      ['한ㄱ', '한', '(not composing)']);
  });

  test('replace is 0 for the first jamo and 1 while composing', function () {
    var c = H.create();
    assert.strictEqual(c.feed('ㅎ').replace, 0);
    assert.strictEqual(c.feed('ㅏ').replace, 1);
    assert.strictEqual(c.feed('ㄴ').replace, 1);
    c.commit();
    assert.strictEqual(c.isComposing(), false);
    assert.strictEqual(c.feed('ㄱ').replace, 0, 'commit starts a fresh syllable');
  });

  test('each edit names the text it expects before the caret', function () {
    /* The receiver checks expect before deleting: a field cleared behind the
       keyboard (app empties it after "send") must not get a stale syllable
       written back into it. */
    var c = H.create();
    var a = c.feed('ㅎ');
    assert.strictEqual(a.expect, '');
    assert.strictEqual(a.compose, 'ㅎ');
    var b = c.feed('ㅏ');
    assert.strictEqual(b.expect, 'ㅎ');
    assert.strictEqual(b.text, '하');
    var n = c.feed('ㄴ');
    assert.strictEqual(n.expect, '하');
    assert.strictEqual(n.compose, '한');
    var carry = c.feed('ㅏ');                 /* 한 + ㅏ → 하 + 나 */
    assert.strictEqual(carry.expect, '한');
    assert.strictEqual(carry.text, '하나');
    assert.strictEqual(carry.compose, '나', 'only 나 is still composing');
    var back = c.backspace();
    assert.strictEqual(back.expect, '나');
    assert.strictEqual(back.text, 'ㄴ');
  });

  test('non-jamo input is rejected', function () {
    var c = H.create();
    assert.strictEqual(c.feed('a'), null);
    assert.strictEqual(c.feed(' '), null);
    assert.strictEqual(c.feed('가'), null, 'precomposed syllables are not keystrokes');
    assert.strictEqual(H.isJamo('ㄱ'), true);
    assert.strictEqual(H.isJamo('1'), false);
  });

  test('every key on the Korean layout is understood by the automaton', function () {
    /* Ties the engine to the real layout: a layout key with a different
       codepoint would otherwise be inserted raw and never compose. */
    var src = fs.readFileSync(path.join(__dirname, '..', 'apps', 'keyboard', 'js',
                              'keyboard.js'), 'utf8');
    var ko = src.split('    ko: {')[1].split('    symbols:')[0];
    var keys = (ko.match(/\\u31[3-6][0-9A-F]/g) || []).map(function (e) {
      return String.fromCharCode(parseInt(e.slice(2), 16));
    });
    assert.ok(keys.length >= 30, 'found the Korean letter keys');
    keys.forEach(function (k) {
      assert.ok(H.isJamo(k), 'layout key U+' + k.charCodeAt(0).toString(16) + ' is jamo');
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Intents (real app-registry.js + intent.js, isolated) — same rules as
   runtime/wam/src/intent (tests/test_intent.c)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylIntent');

(function () {
  var vm = require('vm');
  var ctx = { window: {}, console: console };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system', 'app-registry.js'), 'utf8'), ctx);
  ctx.ZylAppRegistry = ctx.window.ZylAppRegistry;
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system', 'intent.js'), 'utf8'), ctx);
  var I = ctx.window.ZylIntent;
  function plain(v) { return JSON.parse(JSON.stringify(v)); }   /* objects from the other context */
  var VIEW = 'zyl.intent.action.VIEW';
  ctx.ZylAppRegistry.register([
    { id: 'com.zylos.gallery', name: 'Gallery', intents: [{ action: VIEW, mimeType: 'image/*' }] },
    { id: 'com.example.photos', name: 'Photos', intents: [{ action: VIEW, mimeType: 'image/png' }] },
    { id: 'com.zylos.files', name: 'Files' }
  ]);

  test('one handler launches; the starting app is not offered itself', function () {
    var d = I.start('com.zylos.files', { action: VIEW, data: 'Pictures/a.gif', mimeType: 'image/*',
                                         sourceApp: 'com.zylos.settings' });
    assert.strictEqual(d.launch, 'com.zylos.gallery');
    assert.strictEqual(d.intent.sourceApp, 'com.zylos.files');           /* the OS's, not the request's */
    assert.deepStrictEqual(plain(I.start('com.zylos.gallery', { action: VIEW, mimeType: 'image/*' })), { none: true });
    assert.deepStrictEqual(plain(I.start('com.zylos.files', { action: VIEW, mimeType: 'text/plain' })), { none: true });
  });

  test('several handlers: a choice, and only an offered pick from the same app counts', function () {
    var d = I.start('com.zylos.files', { action: VIEW, data: 'x.png', mimeType: 'image/png' });
    assert.deepStrictEqual(plain(d.choose.choices.map(function (c) { return c.appId; })),
                           ['com.example.photos', 'com.zylos.gallery']);
    var t = d.choose.token;
    assert.strictEqual(I.choose('com.zylos.files', t, 'com.zylos.files'), null);        /* not offered */
    assert.strictEqual(I.choose('com.example.photos', t, 'com.zylos.gallery'), null);   /* not its choice */
    var picked = I.choose('com.zylos.files', t, 'com.zylos.gallery');
    assert.strictEqual(picked.launch, 'com.zylos.gallery');
    assert.strictEqual(picked.intent.data, 'x.png');
    assert.strictEqual(I.choose('com.zylos.files', t, 'com.zylos.gallery'), null);     /* used up */
  });

  test('explicit intents name an installed app', function () {
    assert.strictEqual(I.start('com.zylos.files', { targetApp: 'com.zylos.gallery' }).launch, 'com.zylos.gallery');
    assert.deepStrictEqual(plain(I.start('com.zylos.files', { targetApp: 'com.nope' })), { none: true });
  });
})();

/* ═══════════════════════════════════════════════════════
   Permission policy — reserved permissions (real permissions.js, isolated)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylPermissions');

(function () {
  /* Load the real module in its own context: the rest of this file uses a
     permissive stub, which must stay untouched. */
  var vm = require('vm');
  var ctx = { window: {}, console: console };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system',
                     'permissions.js'), 'utf8'), ctx);
  var P = ctx.window.ZylPermissions;

  P.registerFromAppList([
    { id: 'com.zylos.settings', permissions: [] },
    { id: 'com.evil.app', permissions: ['system', 'credential',
                                        'app.manage', 'notification.read',
                                        'camera', 'network'] },
    { id: 'com.zylos.settings', permissions: ['camera'] }   /* ID spoof */
  ]);

  test('a third-party app cannot declare itself system permissions', function () {
    var got = P.getEffectivePermissions('com.evil.app');
    ['system', 'credential', 'app.manage', 'notification.read'].forEach(function (r) {
      assert.strictEqual(got.indexOf(r), -1, r + ' must not be grantable by declaration');
    });
  });

  test('ordinary declared permissions are still granted', function () {
    var got = P.getEffectivePermissions('com.evil.app');
    assert.ok(got.indexOf('camera') !== -1);
    assert.ok(got.indexOf('network') !== -1);
  });

  test('settings and telemetry stay closed to third-party apps', function () {
    assert.strictEqual(P.checkPermission('com.evil.app', 'settings', 'set'), false);
    assert.strictEqual(P.checkPermission('com.evil.app', 'telemetry', 'setEnabled'), false);
  });

  test('system apps keep reserved permissions', function () {
    assert.strictEqual(P.checkPermission('com.zylos.settings', 'settings', 'set'), true);
    assert.strictEqual(P.checkPermission('com.zylos.settings', 'telemetry', 'setEnabled'), true);
  });

  test('a spoofed system app ID is not re-registered', function () {
    var got = P.getEffectivePermissions('com.zylos.settings');
    assert.ok(got.indexOf('system') !== -1, 'still the locked full set');
  });

  test('registerApp applies the same filter', function () {
    P.registerApp('com.other.app', ['system', 'location']);
    var got = P.getEffectivePermissions('com.other.app');
    assert.strictEqual(got.indexOf('system'), -1);
    assert.ok(got.indexOf('location') !== -1);
  });
})();

/* ═══════════════════════════════════════════════════════
   Telemetry consent (JS service, emulator contract)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ telemetry service');

var telemetryTests = (function () {
  /* Each test gets its own store: they run concurrently under Promise.all */
  function make() {
    var store = {};
    var settingsSvc = {
      _getSetting: function (cat) { return Promise.resolve(store[cat] || null); },
      _updateSetting: function (cat, k, v) { store[cat] = store[cat] || {}; store[cat][k] = v; }
    };
    return { tel: ns.telemetry({ settingsSvc: settingsSvc }), store: store };
  }

  return Promise.all([
    asyncTest('telemetry is off when never asked (opt-in)', function () {
      var t = make();
      return t.tel.getEnabled().then(function (on) { assert.strictEqual(on, false); });
    }),
    asyncTest('consent is stored and read back', function () {
      var t = make();
      return t.tel.setEnabled({ enabled: true }).then(function () {
        return t.tel.getEnabled();
      }).then(function (on) {
        assert.strictEqual(on, true);
        assert.strictEqual(t.store.privacy.telemetry, true);
      });
    }),
    asyncTest('withdrawal turns it back off', function () {
      var t = make();
      return t.tel.setEnabled({ enabled: true }).then(function () {
        return t.tel.setEnabled({ enabled: false });
      }).then(function () {
        return t.tel.getEnabled();
      }).then(function (on) { assert.strictEqual(on, false); });
    }),
    asyncTest('only an explicit true counts as consent', function () {
      var t = make();
      t.store.privacy = { telemetry: 'yes' };
      return t.tel.getEnabled().then(function (on) {
        assert.strictEqual(on, false);
        return t.tel.setEnabled({ enabled: 'true' });
      }).then(function (on) {
        assert.strictEqual(on, false, 'a truthy non-boolean is not consent');
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Data usage policy (JS service, emulator contract)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ netstats service');

var netstatsTests = (function () {
  function make() {
    var store = {};
    var settingsSvc = {
      _getSetting: function (cat) { return Promise.resolve(store[cat] || null); },
      _updateSetting: function (cat, k, v) { store[cat] = store[cat] || {}; store[cat][k] = v; }
    };
    return { svc: ns.netstats({ settingsSvc: settingsSvc }), store: store };
  }

  return Promise.all([
    asyncTest('the emulator reports usage as unsupported, never invented', function () {
      return make().svc.getUsage().then(function (u) {
        assert.deepStrictEqual(u, { supported: false });
      });
    }),
    asyncTest('policy defaults to cycle day 1 with no warning or limit', function () {
      return make().svc.getPolicy().then(function (p) {
        assert.deepStrictEqual(p, { cycleDay: 1, warningBytes: 0, limitBytes: 0 });
      });
    }),
    asyncTest('policy is stored and read back', function () {
      var t = make();
      return t.svc.setPolicy({ cycleDay: 15, warningBytes: 2e9, limitBytes: 3e9 }).then(function () {
        return t.svc.getPolicy();
      }).then(function (p) {
        assert.deepStrictEqual(p, { cycleDay: 15, warningBytes: 2e9, limitBytes: 3e9 });
      });
    }),
    asyncTest('the emulator has no bootloader to unlock', function () {
      var b = ns.bootctl({});
      return Promise.all([b.getState(), b.setUnlockAllowed({ allowed: true }),
                          b.requestLockState({ unlocked: true })]).then(function (r) {
        assert.deepStrictEqual(r, [{ supported: false }, false, false]);
      });
    }),
    asyncTest('a cycle day missing from some months is rejected (daemon contract)', function () {
      var t = make();
      return t.svc.setPolicy({ cycleDay: 31, warningBytes: 0, limitBytes: 0 }).then(function () {
        throw new Error('accepted day 31');
      }, function () {
        assert.strictEqual(t.store.datausage, undefined, 'nothing stored');
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Emergency alerts (JS telephony, emulator contract of cbs.c)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ emergency alerts');

var alertTests = (function () {
  function make() {
    var store = {};
    var settingsSvc = {
      _getSetting: function (cat) { return Promise.resolve(store[cat] || null); },
      _updateSetting: function (cat, k, v) { store[cat] = store[cat] || {}; store[cat][k] = v; }
    };
    return ns.telephony({ settingsSvc: settingsSvc });
  }
  return Promise.all([
    asyncTest('no modem, no alerts — never invented', function () {
      return make().getEmergencyAlerts().then(function (l) { assert.deepStrictEqual(l, []); });
    }),
    asyncTest('defaults: everything but tests, presidential always', function () {
      return make().getEmergencyAlertSettings().then(function (e) {
        assert.strictEqual(e.presidential, true);
        assert.strictEqual(e.extreme, true);
        assert.strictEqual(e.test, false);
      });
    }),
    asyncTest('a choice is kept; presidential cannot be turned off', function () {
      var tel = make();
      return tel.setEmergencyAlertEnabled({ category: 'presidential', enabled: false }).then(function (ok) {
        assert.strictEqual(ok, false);
        return tel.setEmergencyAlertEnabled({ category: 'severe', enabled: false });
      }).then(function (ok) {
        assert.strictEqual(ok, true);
        return tel.setEmergencyAlertEnabled({ category: 'bogus', enabled: true });
      }).then(function (ok) {
        assert.strictEqual(ok, false);
        return tel.getEmergencyAlertSettings();
      }).then(function (e) {
        assert.strictEqual(e.severe, false);
        assert.strictEqual(e.presidential, true);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Call log and favorites (JS telephony/contacts, emulator contract of
   telephony.c GetCallLog/DeleteCallLog and contacts.starred)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ call log / favorites');

var callTests = (function () {
  var store = {};
  var settingsSvc = {
    _getSetting: function (cat) { return Promise.resolve(store[cat] || null); },
    _updateSetting: function (cat, k, v) { store[cat] = store[cat] || {}; store[cat][k] = v; }
  };
  var tel = ns.telephony({ settingsSvc: settingsSvc });
  /* What older builds left in the user's Documents folder */
  var files = {
    'Documents/Contacts/c_1.json': JSON.stringify({ id: 'c_1', name: 'Ana', phone: '010', email: '' }),
    'Documents/Messages/thread_010.json': JSON.stringify({ id: 'thread_010', number: '010', name: 'Ana', lastTime: 5,
      messages: [{ id: 'm_4', text: 'hi', sent: false, time: 4 }, { id: 'm_5', text: 'yo', sent: true, time: 5 }] })
  };
  var fsInvoke = function (cmd, a) {
    if (cmd === 'fs_read_dir') {
      return Promise.resolve(Object.keys(files).filter(function (f) { return f.indexOf(a.path + '/') === 0; })
        .map(function (f) { return { name: f.slice(a.path.length + 1) }; }));
    }
    if (cmd === 'fs_read_file') return files[a.path] ? Promise.resolve(files[a.path]) : Promise.reject(new Error('missing'));
    return Promise.reject(new Error('no file writes expected: ' + cmd));
  };
  var contacts = ns.contacts({ invoke: fsInvoke, settingsSvc: settingsSvc });
  var messaging = ns.messaging({ invoke: fsInvoke, settingsSvc: settingsSvc });
  return Promise.all([
    asyncTest('a call is logged by the service, newest first, and can be deleted', function () {
      return tel.dial({ number: '0212345678' }).then(function () { return tel.hangup(); })
        .then(function () { return tel.dial({ number: '01011112222' }); })
        .then(function () { return tel.hangup(); })
        .then(function () { return tel.getCallLog(); })
        .then(function (log) {
          assert.strictEqual(log.length, 2);
          assert.strictEqual(log[0].number, '01011112222');
          assert.strictEqual(log[0].type, 'outgoing');
          assert.ok(log[0].id > log[1].id && typeof log[0].time === 'number');
          return tel.deleteCallLog({ id: log[1].id }).then(function (ok) {
            assert.strictEqual(ok, true);
            return tel.deleteCallLog({ id: log[1].id });
          });
        })
        .then(function (again) {
          assert.strictEqual(again, false);
          return tel.getCallLog();
        })
        .then(function (log) { assert.strictEqual(log.length, 1); });
    }),
    asyncTest('no modem: nothing invented, no tones sent', function () {
      return Promise.all([tel.getState(), tel.sendDTMF({ digit: '5' })]).then(function (r) {
        assert.strictEqual(r[0].simPresent, false);
        assert.strictEqual(r[0].operator, '');
        assert.strictEqual(r[1], false);
        assert.strictEqual(typeof tel.addCallLog, 'undefined');   /* apps cannot write the log */
      });
    }),
    asyncTest('audio says what this device has: no call audio here, vibration if the page can', function () {
      var audio = ns.audio({ settingsSvc: { _getSetting: function () { return Promise.resolve({}); },
                                            _updateSetting: function () {} } });
      return Promise.all([audio.getCapabilities(), audio.setSpeakerphone({ enabled: true })]).then(function (r) {
        assert.deepStrictEqual(r[0], { speakerphone: false, vibrator: true });   /* stub has navigator.vibrate */
        assert.strictEqual(r[1], false);
      });
    }),
    asyncTest('vibration follows the sound setting at the moment it is asked', function () {
      var sound = { vibration: false };
      var buzzed = 0;
      var nav = global.navigator.vibrate;
      global.navigator.vibrate = function () { buzzed++; };
      var audio = ns.audio({ settingsSvc: { _getSetting: function () { return Promise.resolve(sound); },
                                            _updateSetting: function () {} } });
      return audio.vibrate({}).then(function (r) {
        assert.strictEqual(r, false);
        sound.vibration = true;                 /* toggled in Settings */
        return audio.vibrate({ pattern: [50] });
      }).then(function (r) {
        global.navigator.vibrate = nav;
        assert.strictEqual(r, true);
        assert.strictEqual(buzzed, 1);
      });
    }),
    asyncTest('radios are switched through their services; the emulator has no hotspot', function () {
      var calls = [];
      var invoke = function (cmd, a) {
        calls.push(cmd + (a ? ' ' + JSON.stringify(a) : ''));
        if (cmd === 'wifi_set_enabled' || cmd === 'bt_set_enabled') return Promise.resolve({ enabled: a.enabled });
        if (cmd === 'wifi_get_state') return Promise.resolve({ enabled: true, connected: true, currentSsid: 'Home' });
        return Promise.resolve(null);
      };
      var wifi = ns.wifi({ invoke: invoke });
      var bt = ns.bluetooth({ invoke: invoke });
      return Promise.all([wifi.setEnabled({ enabled: false }), bt.setEnabled({ enabled: true }), wifi.getState(),
                          wifi.getCapabilities(), wifi.setHotspot({ enabled: true, ssid: 'x', passphrase: 'yyyyyyyy' })])
        .then(function (r) {
          assert.deepStrictEqual(r.slice(0, 3), [true, true, { enabled: true, connected: true, ssid: 'Home' }]);
          assert.deepStrictEqual(r[3], { present: true, hotspot: false });
          assert.strictEqual(r[4].ok, false);
          assert.strictEqual(r[4].reason, 'unsupported');
          assert.ok(calls.indexOf('wifi_set_enabled {"enabled":false}') !== -1);
        });
    }),
    asyncTest('wifi.connect answers true or false, like the device', function () {
      var wifi = ns.wifi({ invoke: function (cmd, a) {
        return Promise.resolve(cmd === 'wifi_connect' ? { success: a.password === 'secret88' } : null);
      } });
      return Promise.all([wifi.connect({ ssid: 'Home', passphrase: 'secret88' }),
                          wifi.connect({ ssid: 'Home', passphrase: 'wrong' })]).then(function (r) {
        assert.deepStrictEqual(r, [true, false]);
      });
    }),
    asyncTest('starring a contact keeps its details; editing keeps the star', function () {
      return contacts.update({ id: 'c_1', starred: true }).then(function (c) {
        assert.strictEqual(c.name, 'Ana');                       /* taken in from Documents */
        assert.strictEqual(c.starred, true);
        return contacts.update({ id: 'c_1', name: 'Ana Kim', phone: '011', email: 'a@k' });
      }).then(function (c) {
        assert.strictEqual(c.name, 'Ana Kim');
        assert.strictEqual(c.starred, true);
        assert.strictEqual(JSON.parse(store.contacts.entries)[0].name, 'Ana Kim');   /* private store */
        return Promise.all([contacts.create({ name: 'bo', phone: '012' }), contacts.create({ name: 'Cy' })]);
      }).then(function () {
        return Promise.all([contacts.getAll(), contacts.search({ query: 'B' }), contacts.update({ id: 'nobody' }).catch(function () { return 'refused'; })]);
      }).then(function (r) {
        assert.deepStrictEqual(r[0].map(function (c) { return c.name; }), ['Ana Kim', 'bo', 'Cy']);
        assert.deepStrictEqual(r[1].map(function (c) { return c.name; }), ['bo']);
        assert.strictEqual(r[2], 'refused');
      });
    }),
    asyncTest('appdata: each app only its own, quota keeps the old value, no caller no data', function () {
      var disk = {};
      var appdata = ns.appdata({ invoke: function (cmd, a) {
        if (cmd === 'appdata_load') return Promise.resolve(disk[a.appId] || '');
        if (cmd === 'appdata_save') { disk[a.appId] = a.data; return Promise.resolve(true); }
        return Promise.reject(new Error(cmd));
      } });
      return Promise.all([appdata.setItem({ key: 'k', value: 'a' }, 'com.a'), appdata.setItem({ key: 'k', value: 'b' }, 'com.b')])
        .then(function (r) {
          assert.deepStrictEqual(r, [true, true]);
          return Promise.all([appdata.getItem({ key: 'k' }, 'com.a'), appdata.getItem({ key: 'k' }, 'com.c'),
                              appdata.setItem({ key: 'big', value: new Array(5 * 1024 * 1024).join('x') }, 'com.a'),
                              appdata.getItem({ key: 'k' }).catch(function () { return 'refused'; })]);
        }).then(function (r) {
          assert.deepStrictEqual(r, ['a', '', false, 'refused']);
          assert.deepStrictEqual(JSON.parse(disk['com.a']), { k: 'a' });
          return Promise.all([appdata.keys({}, 'com.b'), appdata.removeItem({ key: 'k' }, 'com.b'), appdata.removeItem({ key: 'k' }, 'com.b')]);
        }).then(function (r) {
          assert.deepStrictEqual(r, [['k'], true, false]);
        });
    }),
    asyncTest('clipboard: the OS names the source app, not the request; size is capped', function () {
      var clip = ns.clipboard({});
      assert.deepStrictEqual(clip.paste(), {});
      assert.strictEqual(clip.copy({ text: 'hi', sourceApp: 'com.zylos.settings' }, 'com.example.app'), true);
      assert.strictEqual(clip.paste().sourceApp, 'com.example.app');
      assert.strictEqual(clip.copy({ text: new Array(1024 * 1024 + 2).join('a') }, 'x'), false);
      assert.strictEqual(clip.paste().text, 'hi');
      return Promise.resolve();
    }),
    asyncTest('messages: threads newest first, a send without a modem is not sent, read and delete', function () {
      return messaging.getThreads().then(function (t) {
        assert.strictEqual(t.length, 1);                           /* taken in from Documents */
        assert.deepStrictEqual([t[0].id, t[0].lastMessage, t[0].unread], ['thread_010', 'yo', 0]);
        return Promise.all([messaging.send({ number: '+82 10-9', text: 'a' }), messaging.send({ number: '+82 10-9', text: 'b' })]);
      }).then(function (sent) {
        assert.strictEqual(sent[0].threadId, 'thread_82109');
        assert.strictEqual(sent[1].status, 'failed');
        return Promise.all([messaging.getThreads(), messaging.getMessages({ threadId: 'thread_82109' }),
                            messaging.send({ number: '', text: 'x' }).catch(function () { return 'refused'; })]);
      }).then(function (r) {
        assert.deepStrictEqual(r[0].map(function (t) { return t.id; }), ['thread_82109', 'thread_010']);
        assert.deepStrictEqual(r[1].map(function (m) { return m.text; }), ['a', 'b']);   /* neither lost */
        assert.strictEqual(r[2], 'refused');
        return Promise.all([messaging.markRead({ threadId: 'thread_010' }), messaging.deleteThread({ threadId: 'thread_82109' }),
                            messaging.deleteThread({ threadId: 'thread_82109' })]);
      }).then(function (r) {
        assert.deepStrictEqual(r, [true, true, false]);
        return messaging.getThreads();
      }).then(function (t) {
        assert.deepStrictEqual(t.map(function (x) { return x.id; }), ['thread_010']);
        assert.strictEqual(Object.keys(JSON.parse(store.messaging.threads)).length, 1);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   App store (emulator contract of runtime/services/appstore): a store signed
   the way tools/build-store.py signs — RSA-SHA256 over the SHA-256
   ═══════════════════════════════════════════════════════ */
console.log('\n■ App store');

var storeTests = (function () {
  var nodeCrypto = require('crypto');
  var keys = nodeCrypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
  var pub = keys.publicKey.export({ type: 'spki', format: 'pem' });
  function sha(buf) { return nodeCrypto.createHash('sha256').update(buf).digest(); }
  function sign(buf) { return nodeCrypto.sign('sha256', sha(buf), keys.privateKey).toString('base64'); }
  var pkg = Buffer.from('a zip, as far as the OS is concerned');

  function store(opts) {
    opts = opts || {};
    var entry = { id: 'com.example.hello', name: 'Hello', version: '2.0.0', package: 'hello.zpkg',
                  size: pkg.length, sha256: sha(pkg).toString('hex'), signature: sign(pkg), permissions: ['network'] };
    var cat = Buffer.from(JSON.stringify({ apps: [entry,
      { id: 'com.zylos.settings', name: 'S', version: '9', package: 's', size: 1, sha256: entry.sha256, signature: 'c2ln' },
      { id: '../evil', name: 'E', version: '1', package: 'e', size: 1, sha256: entry.sha256, signature: 'c2ln' }] }));
    var calls = [], files = {
      'https://store.example/v1/catalog.json': opts.catalog || cat,
      'https://store.example/v1/catalog.json.sig': Buffer.from(sign(cat) + '\n'),
      'https://store.example/v1/hello.zpkg': opts.pkg || pkg
    };
    var installedApps = [{ id: 'com.zylos.settings', source: 'image', version: '1' }].concat(opts.installed || []);
    var forgotten = [];
    var svc = ns.appstore({
      SYSTEM_APPS: ['com.zylos.settings'],
      forgetApp: function (id) { forgotten.push(id); },
      appsSvc: { getInstalled: function () { return Promise.resolve(installedApps); }, _invalidate: function () {} },
      invoke: function (cmd, a) {
        calls.push(cmd);
        if (cmd === 'store_config') {
          return Promise.resolve(JSON.stringify(opts.unconfigured ? { url: '', publicKey: '' }
            : { url: 'https://store.example/v1/catalog.json', publicKey: pub }));
        }
        if (cmd === 'store_download') {
          return files[a.url] ? Promise.resolve(files[a.url].toString('base64')) : Promise.reject(new Error('404'));
        }
        if (cmd === 'store_unpack') {
          return Promise.resolve({ manifest: JSON.stringify(opts.manifest || { id: 'com.example.hello', version: '2.0.0' }),
                                   files: ['app.json', 'index.html'] });
        }
        return Promise.resolve(true);
      }
    });
    return { svc: svc, calls: calls, forgotten: forgotten };
  }

  return Promise.all([
    asyncTest('catalog: verified, invalid and image apps left out, installed state shown', function () {
      var s = store({ installed: [{ id: 'com.example.hello', source: 'store', version: '1.0.0' }] });
      return s.svc.getCatalog().then(function (c) {
        assert.strictEqual(c.state, 'ok');
        assert.deepStrictEqual(c.apps.map(function (a) { return a.id; }), ['com.example.hello']);
        assert.deepStrictEqual([c.apps[0].installed, c.apps[0].installedVersion, c.apps[0].update], [true, '1.0.0', true]);
        assert.deepStrictEqual(c.apps[0].permissions, ['network']);
      });
    }),
    asyncTest('catalog: none configured, or not signed by the store', function () {
      return Promise.all([store({ unconfigured: true }).svc.getCatalog(),
                          store({ catalog: Buffer.from('{"apps":[]}') }).svc.getCatalog()]).then(function (r) {
        assert.strictEqual(r[0].state, 'unconfigured');
        assert.strictEqual(r[1].state, 'untrusted');
      });
    }),
    asyncTest('install: a verified package is committed; a tampered one never unpacked', function () {
      var good = store(), bad = store({ pkg: Buffer.from('changed after signing') });
      return Promise.all([good.svc.install({ appId: 'com.example.hello' }),
                          bad.svc.install({ appId: 'com.example.hello' })]).then(function (r) {
        assert.deepStrictEqual(r[0], { ok: true, reason: '' });
        assert.ok(good.calls.indexOf('store_commit') !== -1);
        assert.deepStrictEqual(r[1], { ok: false, reason: 'bad_package' });
        assert.strictEqual(bad.calls.indexOf('store_unpack'), -1);
      });
    }),
    asyncTest('install: not the catalog\'s version, a system app, already current', function () {
      var wrong = store({ manifest: { id: 'com.example.hello', version: '1.0.0' } });
      var current = store({ installed: [{ id: 'com.example.hello', source: 'store', version: '2.0.0' }] });
      return Promise.all([wrong.svc.install({ appId: 'com.example.hello' }),
                          store().svc.install({ appId: 'com.zylos.settings' }),
                          current.svc.install({ appId: 'com.example.hello' })]).then(function (r) {
        assert.strictEqual(r[0].reason, 'invalid_package');
        assert.ok(wrong.calls.indexOf('store_discard') !== -1 && wrong.calls.indexOf('store_commit') === -1);
        assert.strictEqual(r[1].reason, 'system_app');        /* listed by the store, still the image's */
        assert.strictEqual(r[2].reason, 'up_to_date');
      });
    }),
    asyncTest('uninstall: the container goes and the OS forgets the app; system apps stay', function () {
      var s = store({ installed: [{ id: 'com.example.hello', source: 'store', version: '2.0.0' }] });
      return Promise.all([s.svc.uninstall({ appId: 'com.example.hello' }),
                          s.svc.uninstall({ appId: 'com.zylos.settings' })]).then(function (r) {
        assert.deepStrictEqual(r[0], { ok: true, reason: '' });
        assert.ok(s.calls.indexOf('store_remove') !== -1);
        assert.deepStrictEqual(s.forgotten, ['com.example.hello']);
        assert.ok(r[1].reason === 'system_app' || r[1].reason === 'busy');
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Compositor instance-state store (emulator-app/ui/js/app-state.js)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylAppStateStore');

var appStateTests = (function () {
  var Store = require(path.join(__dirname, '..', 'emulator-app', 'ui', 'js', 'app-state.js'));
  function make(running, onAsk) {
    return Store.create({
      ask: onAsk || function () {}, timeoutMs: 40, maxBytes: 100,
      isRunning: function (id) { return running.indexOf(id) !== -1; }
    });
  }
  function saveThen(store, id) { return new Promise(function (r) { store.saveThen(id, r); }); }

  return Promise.all([
    asyncTest('a reply is stored and handed back once', function () {
      var st = make(['a'], null);
      var p = saveThen(st, 'a');
      st.onState({ draft: 'x' });
      return p.then(function () {
        assert.deepStrictEqual(st.take('a'), { found: true, state: { draft: 'x' } });
        assert.deepStrictEqual(st.take('a'), { found: false }, 'one-shot');
      });
    }),
    asyncTest('a hung app does not block the switch', function () {
      /* No reply ever comes; the switch must still go ahead via the timeout.
         A tight wall-clock bound flaked on loaded CI runners, so the guard
         only tells "finished" from "never finished". */
      var st = make(['a']);
      var guard;
      return Promise.race([
        saveThen(st, 'a').then(function () { return 'done'; }),
        new Promise(function (r) { guard = setTimeout(function () { r('blocked'); }, 5000); })
      ]).then(function (how) {
        clearTimeout(guard);
        assert.strictEqual(how, 'done', 'switch went ahead without a reply');
        assert.strictEqual(st.take('a').found, false);
      });
    }),
    asyncTest('a closed app has its state discarded', function () {
      var st = make([]);                       /* no longer running */
      var p = saveThen(st, 'a');
      st.onState({ draft: 'x' });
      return p.then(function () { assert.strictEqual(st.take('a').found, false); });
    }),
    asyncTest('oversized state is dropped', function () {
      var st = make(['a']);
      var p = saveThen(st, 'a');
      st.onState({ blob: new Array(200).join('x') });
      return p.then(function () { assert.strictEqual(st.take('a').found, false); });
    }),
    asyncTest('done fires exactly once even if a late reply follows the timeout', function () {
      var st = make(['a']);
      var calls = 0;
      return new Promise(function (resolve) {
        st.saveThen('a', function () { calls++; });
        setTimeout(function () { st.onState({ late: true }); }, 60);
        setTimeout(resolve, 120);
      }).then(function () { assert.strictEqual(calls, 1); });
    }),
    asyncTest('an older timeout does not cancel a newer request (rapid switching)', function () {
      /* The inline version this replaced cleared the shared waiter on any
         timeout, so a reply to the newer request was dropped and lost. */
      var st = make(['a', 'b']);
      var first = saveThen(st, 'a');
      var second;
      return new Promise(function (resolve) {
        setTimeout(function () {
          second = saveThen(st, 'b');          /* newer request, first still pending */
          setTimeout(function () {             /* first has timed out by now */
            st.onState({ who: 'b' });          /* the reply to the newer request */
            resolve();
          }, 30);
        }, 20);
      }).then(function () { return Promise.all([first, second]); }).then(function () {
        assert.deepStrictEqual(st.take('b'), { found: true, state: { who: 'b' } });
        assert.strictEqual(st.take('a').found, false);
      });
    }),
    asyncTest('discard removes saved state', function () {
      var st = make(['a']);
      var p = saveThen(st, 'a');
      st.onState({ x: 1 });
      return p.then(function () {
        st.discard('a');
        assert.strictEqual(st.take('a').found, false);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Time zone, relative time, lunar date (real apps/shared/i18n.js, isolated)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ zylI18n time');

(function () {
  var vm = require('vm');
  var noop = function () {};
  var doc = {
    readyState: 'complete', addEventListener: noop, querySelectorAll: function () { return []; },
    documentElement: { lang: '', dir: '', setAttribute: noop, classList: { add: noop, remove: noop } },
    body: { classList: { add: noop, remove: noop } }
  };
  var ctx = { window: { addEventListener: noop, parent: null }, document: doc, console: console,
              navigator: { language: 'ko-KR' }, Intl: Intl, Date: Date, setTimeout: setTimeout };
  ctx.window.window = ctx.window;
  ctx.window.document = doc;
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'shared', 'i18n.js'), 'utf8'), ctx);
  var I = ctx.window.zylI18n || ctx.zylI18n;
  var instant = new Date('2026-10-11T03:30:00Z');

  test('formatBytes uses decimal units and the locale separator', function () {
    var before = I.getLocale();
    I.setLocale('en');
    assert.strictEqual(I.formatBytes(0), '0 KB');
    assert.strictEqual(I.formatBytes(1500000), '1.5 MB');
    assert.strictEqual(I.formatBytes(2.25e9), '2.3 GB');
    I.setLocale('es');
    assert.strictEqual(I.formatBytes(1.5e9), '1,5 GB');
    I.setLocale(before);
  });

  test('automatic zone reads the system clock', function () {
    I.setTimeZone('auto');
    assert.strictEqual(I.isAutoTimeZone(), true);
    assert.strictEqual(I.parts(instant).hour, instant.getHours());
  });

  test('a chosen zone moves the wall clock, half-hour offsets included', function () {
    assert.strictEqual(I.setTimeZone('Asia/Kolkata'), true);
    var p = I.parts(instant);
    assert.deepStrictEqual([p.year, p.month, p.day, p.hour, p.minute], [2026, 10, 11, 9, 0]);
    assert.strictEqual(I.formatTime(instant), '09:00');
  });

  test('the calendar date follows the zone across midnight', function () {
    I.setTimeZone('America/New_York');
    var p = I.parts(instant);
    assert.deepStrictEqual([p.day, p.hour, p.minute, p.weekday], [10, 23, 30, 6]);
  });

  test('an unknown zone is rejected and the current one kept', function () {
    I.setTimeZone('Asia/Seoul');
    assert.strictEqual(I.setTimeZone('Mars/Olympus'), false);
    assert.strictEqual(I.parts(instant).hour, 12);
  });

  test('modern and legacy ids are the same zone', function () {
    I.setTimeZone('Asia/Kolkata');
    var a = I.getTimeZone();
    I.setTimeZone('Asia/Calcutta');
    assert.strictEqual(I.getTimeZone(), a, 'stored in one canonical form');
  });

  test('zone offsets in minutes and their labels', function () {
    assert.strictEqual(I.zoneOffsetMinutes('Asia/Seoul', instant), 540);
    assert.strictEqual(I.zoneOffsetMinutes('Asia/Kolkata', instant), 330);
    assert.strictEqual(I.zoneOffsetMinutes('Asia/Kathmandu', instant), 345);
    assert.strictEqual(I.zoneOffsetMinutes('UTC', instant), 0);
    assert.strictEqual(I.formatOffset(330), 'GMT+5:30');
    assert.strictEqual(I.formatOffset(-300), 'GMT-5');
    assert.strictEqual(I.formatOffset(0), 'GMT');
  });

  test('relative times are translated, not "5m" / "Yesterday"', function () {
    I.setLocale('ko');
    I.setTimeZone('Asia/Seoul');
    var now = new Date('2026-10-11T03:30:00Z');
    var ago = function (ms) { return I.formatRelative(new Date(now.getTime() - ms), now); };
    assert.strictEqual(ago(20e3), '방금');
    assert.strictEqual(ago(5 * 6e4), '5분');
    assert.strictEqual(ago(3 * 36e5), '3시간');
    assert.strictEqual(ago(26 * 36e5), '어제');
    assert.strictEqual(ago(5 * 864e5), '10/6');
  });

  test('"yesterday" is the yesterday of the configured zone', function () {
    I.setLocale('ko');
    /* 01:00 in Seoul on the 11th; 25h earlier is the 10th in Seoul */
    var now = new Date('2026-10-10T16:00:00Z');
    I.setTimeZone('Asia/Seoul');
    assert.strictEqual(I.formatRelative(new Date(now.getTime() - 25 * 36e5), now), '어제');
  });

  test('lunar date only for Korean and Chinese', function () {
    I.setLocale('ko');
    I.setTimeZone('Asia/Seoul');
    var ko = I.formatLunarDate(instant);
    assert.ok(ko && ko.indexOf('음력') === 0, 'ko gets a lunar date: ' + ko);
    I.setLocale('en');
    assert.strictEqual(I.formatLunarDate(instant), null);
    I.setLocale('ja');
    assert.strictEqual(I.formatLunarDate(instant), null);
  });

  test('clocks are told when the zone changes, and only then', function () {
    var calls = 0;
    var off = I.onLocaleChange(function () { calls++; });
    I.setTimeZone('Asia/Tokyo');
    I.setTimeZone('Asia/Tokyo');
    off();
    assert.strictEqual(calls, 1);
    I.setTimeZone('auto');
  });
})();

/* ═══════════════════════════════════════════════════════
   Wait for all async tests and report
   ═══════════════════════════════════════════════════════ */
Promise.all([settingsTests, displayTests, inputTests, sensorsTests,
             usbTests, powerTests, audioTests, fsTests, updaterTests, telemetryTests, callTests, storeTests,
             netstatsTests, alertTests, lockTests, deviceBridgeTests, appStateTests])
  .then(function () {
    console.log('\n════════════════════════');
    console.log('Total: ' + total + '  Passed: ' + passed + '  Failed: ' + failed);
    console.log('════════════════════════');
    if (failed > 0) process.exit(1);
  }).catch(function (e) {
    console.error('Test runner error:', e);
    process.exit(1);
  });
