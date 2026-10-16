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
  'user.js', 'credential.js', 'app-seal.js', 'appstore.js', 'updater.js', 'sandbox.js',
  'logger.js', 'accessibility.js', 'audio.js', 'telemetry.js', 'netstats.js', 'bootctl.js', 'lock.js',
  'alarm.js', 'push.js'
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

/* Same answers, and every host command it was asked for in invoke.calls */
function recordingInvoker(responses) {
  var answer = createMockInvoker(responses);
  function invoke(cmd, args) {
    invoke.calls.push({ cmd: cmd, args: args });
    return answer(cmd, args);
  }
  invoke.calls = [];
  return invoke;
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
  }).then(function () {
    /* The OS applies a permission the user turned off — on load and on update —
       not the emulator watching settings traffic */
    return asyncTest('revoked app permissions reach the permission gate', function () {
      var applied = {};
      var saved = ZylPermissions.setAppOverride;
      ZylPermissions.setAppOverride = function (appId, revoked) { applied[appId] = revoked; };
      var inv = createMockInvoker({ 'load_settings': { app_permissions: { 'com.example.a': 'camera,location' } }, 'save_settings': true });
      var s2 = ns.settings({ invoke: inv });
      return s2._loadFromBackend().then(function () {
        assert.deepStrictEqual(applied['com.example.a'], ['camera', 'location']);
        s2.update({ category: 'app_permissions', key: 'com.example.b', value: 'contacts' });
        assert.deepStrictEqual(applied['com.example.b'], ['contacts']);
        s2.update({ category: 'display', key: 'brightness', value: 10 });
        assert.strictEqual(Object.keys(applied).length, 2);
      }).then(function () { ZylPermissions.setAppOverride = saved; },
               function (e) { ZylPermissions.setAppOverride = saved; throw e; });
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
  }).then(function () {
    return asyncTest('handoff: the OS names both roots — the user\'s files for a system app', function () {
      var asked = null;
      var svc = ns.fs({ SYSTEM_APPS: ['com.zylos.files'], invoke: function (cmd, a) {
        asked = [cmd, a];
        return Promise.resolve(['Inbox/a.jpg', '']);
      } });
      return svc._handoff('com.zylos.files', ['Pictures/a.jpg', 'x'], 'com.example.chat').then(function (got) {
        assert.deepStrictEqual(asked, ['fs_handoff', { from: null, paths: ['Pictures/a.jpg', 'x'], to: 'com.example.chat' }]);
        assert.deepStrictEqual(got, ['Inbox/a.jpg', '']);
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
    for (var i = 0; i < n; i++) p = p.then(function () { return svc.verifyPin({ pin: '000000' }); });
    return p;
  }

  return Promise.all([
    asyncTest('no PIN: verify passes, the PIN is never stored in plain text', function () {
      var t = make();
      return t.svc.verifyPin({ pin: '' }).then(function (r) {
        assert.strictEqual(r.ok, true);
        assert.strictEqual(r.hasPin, false);
        /* A specific PIN: the stored hash is random hex, and this exact
           6-digit sequence turns up inside it by chance about 1 in 180,000 runs */
        return t.svc.setPin({ current: '', pin: '482109' });
      }).then(function (r) {
        assert.strictEqual(r.ok, true);
        assert.ok(JSON.stringify(t.store).indexOf('482109') === -1);
        assert.ok(/^pbkdf2-sha256\$/.test(t.store.lock.hash));
        return t.svc.verifyPin({ pin: '482109' });
      }).then(function (r) { assert.strictEqual(r.ok, true); });
    }),
    asyncTest('five failures make the user wait, even for the right PIN', function () {
      var t = make();
      return t.svc.setPin({ current: '', pin: '482109' }).then(function () {
        return fail(t.svc, 5);
      }).then(function () {
        return t.svc.verifyPin({ pin: '482109' });
      }).then(function (r) {
        assert.strictEqual(r.ok, false);
        assert.strictEqual(r.reason, 'locked_out');
        assert.ok(r.retryAfterMs > 25000 && r.retryAfterMs <= 30000);
      });
    }),
    asyncTest('changing or removing needs the current PIN; format is checked', function () {
      var t = make();
      return t.svc.setPin({ current: '', pin: '12a456' }).then(function (r) {
        assert.strictEqual(r.reason, 'format');
        return t.svc.setPin({ current: '', pin: '1234' });     /* 4 digits: the old length is rejected */
      }).then(function (r) {
        assert.strictEqual(r.reason, 'format');
        return t.svc.setPin({ current: '', pin: '482109' });
      }).then(function () {
        return t.svc.setPin({ current: '111111', pin: '' });
      }).then(function (r) {
        assert.strictEqual(r.reason, 'wrong_pin');
        return t.svc.setPin({ current: '482109', pin: '' });
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
  /* A page as the WAM builds it: the WAM's bridge script (its tokens filled
     in as bridge.c does) in the top document, the message handler WebKit
     gives every frame, and window.postMessage delivering to listeners */
  var TOKEN = '0123456789abcdef0123456789abcdef';
  function devicePage(href) {
    var sent = [], raw = [], listeners = [];
    var win = {
      webkit: { messageHandlers: { bridge: { postMessage: function (j) {
        raw.push(j);
        if (j.indexOf(TOKEN) === 0) sent.push(JSON.parse(j.slice(TOKEN.length)));   /* zyl_bridge_untoken */
      } } } },
      navigator: {},
      addEventListener: function (t, fn) { if (t === 'message') listeners.push(fn); },
      postMessage: function (data) {
        setTimeout(function () { listeners.forEach(function (fn) { fn({ data: data, source: win }); }); }, 0);
      },
      setTimeout: setTimeout, clearTimeout: clearTimeout
    };
    win.window = win; win.parent = win;
    var ctx = { window: win, navigator: win.navigator, document: { addEventListener: function () {} },
                location: { href: href || 'zyl-app://com.zylos.settings/index.html' },
                console: console, setTimeout: setTimeout, clearTimeout: clearTimeout, JSON: JSON, Promise: Promise,
                Object: Object };
    vm.createContext(ctx);
    var wamJs = fs.readFileSync(path.join(__dirname, '..', 'runtime', 'wam', 'src', 'bridge', 'bridge.js'), 'utf8')
      .split('{{APP_ID}}').join(JSON.stringify('com.zylos.settings'))   /* JS literals, as bridge.c */
      .split('{{APP_NAME}}').join(JSON.stringify('Settings'))
      .split('{{APP_VERSION}}').join(JSON.stringify('1.0.0')).split('{{BRIDGE_TOKEN}}').join(JSON.stringify(TOKEN));
    vm.runInContext(wamJs, ctx);
    vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'shared', 'bridge.js'), 'utf8'), ctx);
    return { win: win, sent: sent, raw: raw, B: win.ZylBridge || ctx.ZylBridge, listeners: listeners };
  }

  return Promise.all([
    asyncTest('device messages go to the WAM, not the injected navigator.system', function () {
      var p = devicePage();
      assert.strictEqual(p.B.getIpcMode(), 'webkit');
      p.B.sendToSystem({ type: 'navigation.handled' });
      assert.strictEqual(p.sent.length, 1);
      assert.strictEqual(p.raw[0].indexOf(TOKEN), 0, 'carries the view\'s token');
      return Promise.resolve();
    }),
    asyncTest('a page that is not the app\'s own sends the WAM nothing', function () {
      var p = devicePage('https://example.com/');                /* the app's view went elsewhere */
      p.B.sendToSystem({ type: 'navigation.handled' });
      p.win.navigator.system.app.close();
      assert.strictEqual(p.raw.length, 0);
      return Promise.resolve();
    }),
    asyncTest('a frame the app shows is not the OS speaking', function () {
      var p = devicePage();
      var heard = false;
      p.B.onServiceEvent('telephony', 'CallStateChanged', function () { heard = true; });
      var frame = {};                                            /* a child frame's window */
      p.listeners.forEach(function (fn) {
        fn({ source: frame, data: JSON.stringify({ type: 'service.event', service: 'telephony',
          event: 'CallStateChanged', data: ['IDLE', 'RINGING', 'x'] }) });
      });
      assert.strictEqual(heard, false);
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

var intentTests = (function () {
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

  /* Same scenarios as test_intent.c: who picks, what files may be */
  var PICK = 'zyl.intent.action.PICK';
  ctx.ZylAppRegistry.register([
    { id: 'com.zylos.picker', name: 'Picker', system: true, intents: [{ action: PICK }] },
    { id: 'com.example.fakepicker', name: 'Fake', intents: [{ action: PICK }] },
    { id: 'com.example.chat', name: 'Chat' }
  ]);

  test('only a system app picks', function () {
    assert.strictEqual(I.start('com.example.chat', { action: PICK, mimeType: 'image/png' }).launch, 'com.zylos.picker');
    assert.deepStrictEqual(plain(I.start('com.example.chat', { action: PICK, targetApp: 'com.example.fakepicker' })),
                           { none: true });
  });

  test('files are strings, ten at most', function () {
    var d = I.start('com.example.chat', { targetApp: 'com.zylos.picker',
      files: ['a', 1, '', { x: 1 }, 'b', '3', '4', '5', '6', '7', '8', '9', '10', '11'] });
    assert.strictEqual(d.intent.files.length, 10);
    assert.deepStrictEqual(plain(d.intent.files.slice(0, 2)), ['a', 'b']);
    assert.deepStrictEqual(plain(I.start('com.example.chat', { targetApp: 'com.zylos.picker', files: 'x' }).intent.files), []);
  });

  test('a pick is answered once, by its handler, and goes to the app that asked', function () {
    var d = I.start('com.example.chat', { action: PICK, requestCode: 'attach' });
    var token = d.intent.resultToken;
    assert.ok(token);
    assert.strictEqual(I.result('com.example.chat', token, ['x']), null);           /* not the handler */
    assert.strictEqual(I.result('com.zylos.picker', 'r-other', ['x']), null);       /* not its token */
    var r = I.result('com.zylos.picker', token, ['Pictures/cat.jpg', 7]);
    assert.strictEqual(r.launch, 'com.example.chat');
    assert.strictEqual(r.from, 'com.zylos.picker');
    assert.deepStrictEqual(plain(r.result), { requestCode: 'attach', files: ['Pictures/cat.jpg'] });
    assert.strictEqual(I.result('com.zylos.picker', token, ['x']), null);           /* used up */
  });

  return asyncTest('delivery hands the files over first: the receiver gets its own paths', function () {
    var asked = [];
    I.useHandoff(function (from, paths, to) {
      asked.push([from, plain(paths), to]);
      return Promise.resolve(paths.map(function (p) { return p === 'gone' ? '' : 'Inbox/' + p.split('/').pop(); }));
    });
    var d = I.start('com.zylos.picker', { targetApp: 'com.example.chat', files: ['Pictures/cat.jpg', 'gone'] });
    return I.deliver(d).then(function (p) {
      assert.deepStrictEqual(asked, [['com.zylos.picker', ['Pictures/cat.jpg', 'gone'], 'com.example.chat']]);
      assert.deepStrictEqual(plain(p.intent.files), ['Inbox/cat.jpg']);
      I.useHandoff(function () { return Promise.reject(new Error('down')); });
      return I.deliver(I.start('com.zylos.picker', { targetApp: 'com.example.chat', files: ['a'] }));
    }).then(function (p) {
      assert.deepStrictEqual(plain(p.intent.files), []);                            /* never the sender's path */
      I.useHandoff(null);
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   Shell policy (real shell.js, isolated) — same scenarios as
   runtime/wam/src/shell (tests/test_shell.c)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylShell');

(function () {
  var vm = require('vm');
  var HOME = 'com.zylos.home', LOCK = 'com.zylos.lockscreen', SETUP = 'com.zylos.oobe';
  function shell() {
    var ctx = { window: {} };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system', 'shell.js'), 'utf8'), ctx);
    return ctx.window.ZylShell;
  }
  /* Show what the policy said, as the container does */
  function show(S, app) { if (app) S.shown(app); }

  test('setup holds the device', function () {
    var S = shell();
    assert.strictEqual(S.boot(false), SETUP);
    show(S, SETUP);
    assert.ok(S.mayLaunch(SETUP));
    assert.ok(S.mayLaunch('com.zylos.phone'));                     /* an emergency call */
    assert.ok(!S.mayLaunch(HOME));
    assert.ok(!S.mayLaunch(LOCK));
    assert.ok(!S.mayLaunch('com.zylos.settings'));
    assert.ok(!S.mayLaunch(undefined));
    assert.strictEqual(S.home(), null);
    assert.ok(!S.navigable());
    assert.ok(!S.mayClose(SETUP));
    assert.strictEqual(S.lock(), null);                              /* setup never locks */
    assert.strictEqual(S.unlock(LOCK), null);
    show(S, HOME);
    assert.strictEqual(S.screenOn(), SETUP);                         /* back to setup */
  });

  test('only setup finishes setup', function () {
    var S = shell();
    show(S, S.boot(false));
    assert.strictEqual(S.setupDone('com.example.app'), null);
    assert.strictEqual(S.setupDone(LOCK), null);
    assert.strictEqual(S.setupDone(SETUP), HOME);
    show(S, HOME);
    assert.ok(S.navigable());                                        /* just set up: not locked */
    assert.ok(S.mayLaunch('com.zylos.settings'));
    assert.ok(!S.mayLaunch(SETUP));                                  /* not again */
    assert.strictEqual(S.setupDone(SETUP), null);
  });

  test('locked shows only the lock screen', function () {
    var S = shell();
    assert.strictEqual(S.boot(true), LOCK);
    show(S, LOCK);
    assert.ok(S.mayLaunch(LOCK));
    assert.ok(S.mayLaunch('com.zylos.camera'));                     /* the shortcut */
    assert.ok(S.mayLaunch('com.zylos.alerts'));
    assert.ok(!S.mayLaunch(HOME));
    assert.ok(!S.mayLaunch('com.zylos.gallery'));                   /* not from the camera either */
    assert.ok(!S.mayLaunch(SETUP));
    assert.ok(!S.navigable());
    assert.ok(!S.mayClose(LOCK));
    assert.strictEqual(S.home(), null);                              /* already there */
    show(S, 'com.zylos.camera');
    assert.strictEqual(S.home(), LOCK);                              /* leaving it: the lock screen */
    assert.strictEqual(S.screenOn(), null);                          /* a call/alert/camera stays */
  });

  test('only the lock screen unlocks', function () {
    var S = shell();
    show(S, S.boot(true));
    assert.strictEqual(S.unlock('com.zylos.camera'), null);
    assert.strictEqual(S.unlock('com.example.app'), null);
    assert.strictEqual(S.unlock(undefined), null);
    assert.ok(!S.navigable());
    assert.strictEqual(S.unlock(LOCK, 'com.zylos.messages'), 'com.zylos.messages');
    assert.ok(S.navigable());
    assert.strictEqual(S.unlock(LOCK), null);                        /* not locked */
  });

  test('unlock opens home unless an app may open', function () {
    var S = shell();
    show(S, S.boot(true));
    assert.strictEqual(S.unlock(LOCK), HOME);
    show(S, S.lock());
    assert.strictEqual(S.unlock(LOCK, ''), HOME);
    show(S, S.lock());
    assert.strictEqual(S.unlock(LOCK, SETUP), HOME);                 /* never setup again */
    show(S, S.lock());
    assert.strictEqual(S.unlock(LOCK, LOCK), HOME);
  });

  test('unlocked: home, close and lock', function () {
    var S = shell();
    show(S, S.boot(true));
    show(S, S.unlock(LOCK));
    assert.strictEqual(S.home(), null);                              /* already home */
    show(S, 'com.zylos.notes');
    assert.strictEqual(S.home(), HOME);
    assert.ok(!S.mayLaunch(LOCK));                                   /* only by locking */
    assert.ok(!S.mayClose(HOME));
    assert.ok(S.mayClose(LOCK));
    assert.ok(S.mayClose(SETUP));
    assert.ok(S.mayClose('com.zylos.notes'));
    assert.strictEqual(S.screenOn(), null);                          /* unlocked: nothing to cover */
    assert.strictEqual(S.lock(), LOCK);
    assert.ok(!S.navigable());
    assert.strictEqual(S.screenOn(), LOCK);                          /* not shown yet: cover it */
    show(S, LOCK);
    assert.strictEqual(S.lock(), null);                              /* already in front */
    assert.strictEqual(S.screenOn(), null);
  });

  test('the panel follows the device state', function () {
    var S = shell();
    var UI = S.systemUi();
    show(S, S.boot(false));
    assert.strictEqual(S.panelMode('com.example.app', 'full'), null);
    assert.strictEqual(S.panelMode(SETUP, 'bar'), null);
    assert.strictEqual(S.panelMode(UI, 'full'), 'bar');                 /* setup */
    assert.strictEqual(S.panelMode(UI, 'banner'), 'bar');
    show(S, S.setupDone(SETUP));
    assert.strictEqual(S.panelMode(UI, 'full'), 'full');
    assert.strictEqual(S.panelMode(UI, 'banner'), 'banner');
    assert.strictEqual(S.panelMode(UI, 'bar'), 'bar');
    assert.strictEqual(S.panelMode(UI, 'anything'), 'bar');
    show(S, S.lock());
    assert.strictEqual(S.panelMode(UI, 'full'), 'bar');                 /* locked */
    assert.strictEqual(S.panelMode(UI, 'banner'), 'bar');
    assert.ok(!S.mayLaunch(UI) && !S.mayClose(UI));                     /* a surface */
    show(S, S.unlock(LOCK));
    assert.ok(!S.mayLaunch(UI) && !S.mayClose(UI));
  });

  test('the same apps as the device policy (shell.c)', function () {
    var js = fs.readFileSync(path.join(__dirname, '..', 'apps', 'system', 'shell.js'), 'utf8');
    var c = fs.readFileSync(path.join(__dirname, '..', 'runtime', 'wam', 'src', 'shell', 'shell.c'), 'utf8') +
            fs.readFileSync(path.join(__dirname, '..', 'runtime', 'wam', 'src', 'shell', 'shell.h'), 'utf8');
    function ids(text) { return (text.match(/com\.zylos\.[a-z]+/g) || []).filter(function (v, i, a) { return a.indexOf(v) === i; }).sort(); }
    assert.deepStrictEqual(ids(js), ids(c));
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
                                        'webcontent', 'camera', 'network'] },
    { id: 'com.zylos.settings', permissions: ['camera'] }   /* ID spoof */
  ]);

  test('a third-party app cannot declare itself system permissions', function () {
    var got = P.getEffectivePermissions('com.evil.app');
    ['system', 'credential', 'app.manage', 'notification.read', 'webcontent'].forEach(function (r) {
      assert.strictEqual(got.indexOf(r), -1, r + ' must not be grantable by declaration');
    });
  });

  test('the device\'s power: any app reads it, only system apps act on it (as bridge.c)', function () {
    assert.ok(P.checkPermission('com.evil.app', 'power', 'getState'));
    ['reboot', 'shutdown', 'suspend', 'screenOff', 'screenOn', 'userActivity',
     'setBrightness', 'acquireWakelock', 'releaseWakelock'].forEach(function (m) {
      assert.strictEqual(P.checkPermission('com.evil.app', 'power', m), false, m);
      assert.ok(P.checkPermission('com.zylos.settings', 'power', m), m);
    });
  });

  test('the device\'s settings and the shell are the OS\'s; the app list is app.list (as bridge.c)', function () {
    ['usb.setMode', 'display.setMode', 'display.setScale', 'display.setRotation', 'display.setAutoRotate',
     'accessibility.setHighContrast', 'accessibility.setFontScale', 'accessibility.setScreenReader',
     'input.showKeyboard', 'input.hideKeyboard', 'input.switchLayout', 'logger.getLogs',
     'logger.getCrashReports', 'logger.setFilterLevel', 'apps.suspend', 'apps.resume', 'apps.goHome',
     'apps.goBack', 'apps.openPanel', 'apps.showRecents', 'apps.switchApp',
     'apps.getInstalled', 'apps.launch'].forEach(function (k) {
      var sm = k.split('.');
      assert.strictEqual(P.checkPermission('com.evil.app', sm[0], sm[1]), false, k);
      assert.ok(P.checkPermission('com.zylos.settings', sm[0], sm[1]), k);
    });
    assert.ok(P.checkPermission('com.evil.app', 'display', 'getCurrentMode'));
    assert.ok(P.checkPermission('com.evil.app', 'clipboard', 'copy'));
    assert.ok(P.checkPermission('com.zylos.home', 'apps', 'getInstalled'));   /* the launcher's app.list */
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
/* ═══════════════════════════════════════════════════════
   The request gate (real services.js + permissions.js + notification.js,
   isolated) — the device bridge's rules (bridge.c): a third-party app's
   appId is its own, other apps' notifications are notification.read's,
   internals are not methods, an unlisted service is closed
   ═══════════════════════════════════════════════════════ */
console.log('\n■ request gate');

var gateTests = (function () {
  var vm = require('vm');
  var events = [];
  var ctx = { console: console, setTimeout: function () { return 0; }, clearTimeout: function () {},
              Promise: Promise, CustomEvent: function (type, init) { this.type = type; this.detail = init.detail; } };
  ctx.window = ctx;
  ctx.dispatchEvent = function (e) { events.push(e); return true; };
  ['permissions.js', 'services/notification.js', 'services.js'].forEach(function (f) {
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system', f), 'utf8'), ctx);
  });
  var S = ctx.ZylSystemServices;
  ctx.ZylPermissions.registerFromAppList([
    { id: 'com.example.chat', permissions: ['notification'] },
    { id: 'com.example.spy', permissions: ['notification', 'notification.read'] },
    { id: 'com.zylos.statusbar', permissions: [] }
  ]);
  S.init(function () { return Promise.resolve(null); });
  /* A request's answer — refusals come back as a promise */
  function ask(service, method, params, app) { return Promise.resolve(S.handleRequest(service, method, params, app)); }
  function active() {
    return ask('notification', 'getActive', {}, 'com.zylos.statusbar').then(function (a) { return JSON.parse(JSON.stringify(a)); });
  }

  /* One after another: they share the service's state */
  return asyncTest('a third-party app posts only as itself; a system app may name another', function () {
    return ask('notification', 'post', { appId: 'com.zylos.messages', title: 'fake' }, 'com.example.chat')
      .then(function () { return ask('notification', 'post', { appId: 'com.zylos.settings', title: 'data' }, 'com.zylos.statusbar'); })
      .then(active).then(function (a) {
        assert.strictEqual(a[0].appId, 'com.example.chat');
        assert.strictEqual(a[1].appId, 'com.zylos.settings');
      });
  }).then(function () {
    return asyncTest("other apps' notifications are not a third-party app's to read, dismiss or silence", function () {
      var refused = [];
      ['getActive', 'getHistory', 'clearAll', 'clearHistory', 'setDndMode'].forEach(function (m) {
        ['com.example.chat', 'com.example.spy'].forEach(function (app) {
          refused.push(ask('notification', m, { id: 1 }, app).then(function (r) {
            assert.ok(r && r.error === 'Permission denied', app + ' ' + m);
          }));
          /* Cancel is open, but only for its own: naming the owner changes nothing */
          refused.push(ask('notification', 'cancel', { id: 2, appId: 'com.zylos.settings' }, app));
        });
      });
      return Promise.all(refused).then(active).then(function (a) {
        assert.strictEqual(a.length, 2);                                  /* nothing dismissed */
      });
    });
  }).then(function () {
    return asyncTest("a third-party app cancels its own; a system app's '' is any app's", function () {
      return ask('notification', 'post', { title: 'mine' }, 'com.example.chat').then(function (r) {
        return ask('notification', 'cancel', { id: r.id }, 'com.example.chat');
      }).then(function (r) {
        assert.strictEqual(r.success, true);
        return ask('notification', 'cancel', { id: 1, appId: '' }, 'com.zylos.statusbar');
      }).then(active).then(function (a) {
        assert.deepStrictEqual(a.map(function (n) { return n.appId; }), ['com.zylos.settings']);
      });
    });
  }).then(function () {
    return asyncTest('posted and dismissed are announced (what draws notifications follows them)', function () {
      assert.deepStrictEqual(events.map(function (e) { return e.type; }),
        ['zyl:notification-posted', 'zyl:notification-posted', 'zyl:notification-posted',
         'zyl:notification-dismissed', 'zyl:notification-dismissed']);
      assert.strictEqual(events[0].detail.appId, 'com.example.chat');
      return Promise.resolve();
    });
  }).then(function () {
    /* Same rule as test_notification.c: only an offered action, by the system UI; then it is done */
    return asyncTest('an action press goes to the posting app, by the system UI only, once', function () {
      var before = events.length;
      return ask('notification', 'postWithActions',
                 { title: 'Mom', actions: [{ label: 'Reply', actionId: 'reply' }] }, 'com.example.chat')
        .then(function (r) {
          var id = r.id;
          return Promise.all([
            ask('notification', 'invokeAction', { id: id, actionId: 'reply' }, 'com.example.chat'),
            ask('notification', 'invokeAction', { id: id, actionId: 'nope' }, 'com.zylos.statusbar')
          ]).then(function (refused) {
            assert.strictEqual(refused[0].error, 'Permission denied');           /* not an app's own call */
            assert.strictEqual(refused[1], false);                                 /* not offered */
            return ask('notification', 'invokeAction', { id: id, actionId: 'reply' }, 'com.zylos.statusbar');
          }).then(function (ok) {
            assert.strictEqual(ok, true);
            var pressed = events.slice(before).filter(function (e) { return e.type === 'zyl:notification-action'; });
            assert.deepStrictEqual(JSON.parse(JSON.stringify(pressed.map(function (e) { return e.detail; }))),
                                   [{ id: id, appId: 'com.example.chat', actionId: 'reply' }]);
            return ask('notification', 'invokeAction', { id: id, actionId: 'reply' }, 'com.zylos.statusbar');
          }).then(function (again) { assert.strictEqual(again, false); });           /* done: gone */
        });
    });
  }).then(function () {
    return asyncTest('internals and what every object has are not methods', function () {
      ['_forgetApp', 'constructor', 'toString', '__proto__', 'hasOwnProperty'].forEach(function (m) {
        assert.strictEqual(S.handleRequest('notification', m, {}, 'com.zylos.statusbar'), null, m);
      });
      return active().then(function (a) { assert.strictEqual(a.length, 1); });
    });
  }).then(function () {
    return asyncTest('users are system-only; an unlisted service is closed', function () {
      return Promise.all([ask('user', 'listUsers', {}, 'com.example.chat'), ask('nosuch', 'x', {}, 'com.example.chat')])
        .then(function (r) {
          assert.strictEqual(r[0].error, 'Permission denied');
          assert.strictEqual(r[1].error, 'Permission denied');
        });
    });
  }).then(function () {
    return asyncTest('no whitelist loaded is no network', function () {
      var c2 = { window: {} };
      vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system', 'services', 'network.js'), 'utf8'), c2);
      var called = false;
      var net = c2.window.ZylServiceModules.network({ invoke: function () { called = true; return Promise.resolve('x'); } });
      return net.fetch({ url: 'https://example.com' }).then(function (r) {
        assert.ok(r && r.error && !called);
      });
    });
  });
})();

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
   the way tools/build-store.py signs — RSA-SHA256 over the SHA-256 — and
   packages sealed the way it seals them (.zyl-seal/)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ App store');

var storeTests = (function () {
  var nodeCrypto = require('crypto');
  var keys = nodeCrypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
  var devKeys = nodeCrypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
  var pub = keys.publicKey.export({ type: 'spki', format: 'pem' });
  var devPub = devKeys.publicKey.export({ type: 'spki', format: 'pem' });
  function sha(buf) { return nodeCrypto.createHash('sha256').update(buf).digest(); }
  function signWith(key, buf) { return nodeCrypto.sign('sha256', sha(buf), key).toString('base64'); }
  function sign(buf) { return signWith(keys.privateKey, buf); }
  function hexOf(text) { return sha(Buffer.from(text)).toString('hex'); }
  var pkg = Buffer.from('a zip, as far as the OS is concerned');
  /* One clock for every certificate this suite makes: the same certificate,
     and so the same fingerprint, however many times it is built */
  var NOW = Math.floor(Date.now() / 1000);

  /* What the container reports for an unpacked package (store_unpack, store_inspect):
     every file's hash and the seal's files, as the tool writes them */
  function sealed(o) {
    o = o || {};
    var id = o.id || 'com.example.hello', version = o.version || '2.0.0';
    var files = {}, hashes = {}, seal = {};
    ['app.json', 'index.html'].forEach(function (n) { files[n] = hashes[n] = hexOf(n + ' of ' + id); });
    function put(name, obj, key) {
      var json = JSON.stringify(obj);
      seal[name + '.json'] = json;
      seal[name + '.sig'] = signWith(key, Buffer.from(json)) + '\n';
      hashes['.zyl-seal/' + name + '.json'] = hexOf(json);
      hashes['.zyl-seal/' + name + '.sig'] = hexOf(seal[name + '.sig']);
    }
    if (o.dev) {
      put('cert', { developer: 'acme', name: 'ACME', publicKey: devPub, notAfter: NOW + (o.expired ? -60 : 86400) },
          keys.privateKey);
      put('profile', { cert: hashes['.zyl-seal/cert.json'], apps: ['com.example.*'], devices: ['DEV-1'],
                       notAfter: NOW + 86400 }, keys.privateKey);
    }
    put('seal', { version: 1, id: id, appVersion: version, files: files }, o.dev ? devKeys.privateKey : keys.privateKey);
    if (o.added) hashes['js/extra.js'] = hexOf('evil()');
    if (o.unsealed) seal = {};
    return { manifest: JSON.stringify({ id: id, version: version }), hashes: hashes, seal: seal };
  }

  function store(opts) {
    opts = opts || {};
    var entry = { id: 'com.example.hello', name: 'Hello', version: '2.0.0', package: 'hello.zpkg',
                  size: pkg.length, sha256: sha(pkg).toString('hex'), signature: sign(pkg), permissions: ['network'] };
    var cat = Buffer.from(JSON.stringify({ apps: [entry,
      { id: 'com.zylos.settings', name: 'S', version: '9', package: 's', size: 1, sha256: entry.sha256, signature: 'c2ln' },
      { id: '../evil', name: 'E', version: '1', package: 'e', size: 1, sha256: entry.sha256, signature: 'c2ln' }],
      revoked: opts.revokeInCatalog || [] }));
    var calls = [], files = {
      'https://store.example/v1/catalog.json': opts.catalog || cat,
      'https://store.example/v1/catalog.json.sig': Buffer.from(sign(cat) + '\n'),
      'https://store.example/v1/hello.zpkg': opts.pkg || pkg
    };
    var installedApps = [{ id: 'com.zylos.settings', source: 'image', version: '1' }].concat(opts.installed || []);
    var forgotten = [], revoked = opts.revoked || [], committed = [];
    function invoke(cmd, a) {
      calls.push(cmd);
      if (cmd === 'store_config') {
        return Promise.resolve(JSON.stringify(opts.unconfigured ? { url: '', publicKey: '' }
          : { url: 'https://store.example/v1/catalog.json', publicKey: pub }));
      }
      if (cmd === 'store_download') {
        return files[a.url] ? Promise.resolve(files[a.url].toString('base64')) : Promise.reject(new Error('404'));
      }
      if (cmd === 'fs_read_binary') return Promise.resolve(pkg.toString('base64'));
      if (cmd === 'store_unpack') return Promise.resolve(opts.unpacked || sealed());
      if (cmd === 'store_inspect') return Promise.resolve(opts.inspect[a.appId]);
      if (cmd === 'store_revoked') {
        if (a.list) revoked = a.list;
        return Promise.resolve(revoked);
      }
      if (cmd === 'store_commit') committed.push([a.appId, a.stage]);
      return Promise.resolve(true);
    }
    var seal = ns.appSeal({ invoke: invoke, deviceRef: { deviceId: opts.device === undefined ? 'DEV-1' : opts.device } });
    var svc = ns.appstore({
      SYSTEM_APPS: ['com.zylos.settings'],
      seal: seal,
      forgetApp: function (id) { forgotten.push(id); },
      appsSvc: { getInstalled: function () { return Promise.resolve(installedApps); }, _invalidate: function () {} },
      invoke: invoke
    });
    return { svc: svc, seal: seal, calls: calls, forgotten: forgotten, committed: committed,
             revoked: function () { return revoked; } };
  }

  function devFp() { return sealed({ dev: true }).hashes['.zyl-seal/cert.json']; }

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
    asyncTest('catalog: its revoked certificates are kept', function () {
      var s = store({ revokeInCatalog: [devFp().toUpperCase(), 'short'] });
      return s.svc.getCatalog().then(function () {
        assert.deepStrictEqual(s.revoked(), [devFp()]);
      });
    }),
    asyncTest('install: a verified, store-sealed package is committed; a tampered one never unpacked', function () {
      var good = store(), bad = store({ pkg: Buffer.from('changed after signing') });
      return Promise.all([good.svc.install({ appId: 'com.example.hello' }),
                          bad.svc.install({ appId: 'com.example.hello' })]).then(function (r) {
        assert.deepStrictEqual(r[0], { ok: true, reason: '', appId: 'com.example.hello' });
        assert.deepStrictEqual(good.committed, [['com.example.hello', 'com.example.hello']]);
        assert.deepStrictEqual(r[1], { ok: false, reason: 'bad_package' });
        assert.strictEqual(bad.calls.indexOf('store_unpack'), -1);
      });
    }),
    asyncTest('install: from the catalog, no seal, a changed file or a development build is refused', function () {
      var cases = [store({ unpacked: sealed({ unsealed: true }) }), store({ unpacked: sealed({ added: true }) }),
                   store({ unpacked: sealed({ dev: true }) })];
      return Promise.all(cases.map(function (s) { return s.svc.install({ appId: 'com.example.hello' }); })).then(function (r) {
        r.forEach(function (x) { assert.deepStrictEqual(x, { ok: false, reason: 'bad_package' }); });
        cases.forEach(function (s) {
          assert.ok(s.calls.indexOf('store_discard') !== -1 && s.committed.length === 0);
        });
      });
    }),
    asyncTest('install: not the catalog\'s version, a system app, already current', function () {
      var wrong = store({ unpacked: sealed({ version: '1.0.0' }) });
      var current = store({ installed: [{ id: 'com.example.hello', source: 'store', version: '2.0.0' }] });
      return Promise.all([wrong.svc.install({ appId: 'com.example.hello' }),
                          store().svc.install({ appId: 'com.zylos.settings' }),
                          current.svc.install({ appId: 'com.example.hello' })]).then(function (r) {
        assert.strictEqual(r[0].reason, 'invalid_package');
        assert.ok(wrong.calls.indexOf('store_discard') !== -1 && wrong.committed.length === 0);
        assert.strictEqual(r[1].reason, 'system_app');        /* listed by the store, still the image's */
        assert.strictEqual(r[2].reason, 'up_to_date');
      });
    }),
    asyncTest('installFile: a development build only where provisioned, unexpired, unrevoked', function () {
      var here = store({ unpacked: sealed({ dev: true }) });
      var elsewhere = store({ unpacked: sealed({ dev: true }), device: 'DEV-2' });
      var noDevice = store({ unpacked: sealed({ dev: true }), device: '' });
      var other = store({ unpacked: sealed({ dev: true, id: 'com.other.app' }) });
      var expired = store({ unpacked: sealed({ dev: true, expired: true }) });
      var revoked = store({ unpacked: sealed({ dev: true }), revoked: [devFp()] });
      var all = [here, elsewhere, noDevice, other, expired, revoked];
      return Promise.all(all.map(function (s) {
        return s.svc.installFile({ path: 'Downloads/app.zpkg' });
      })).then(function (r) {
        assert.deepStrictEqual(r[0], { ok: true, reason: '', appId: 'com.example.hello' });
        assert.deepStrictEqual(here.committed, [['com.example.hello', 'local.package']]);
        assert.deepStrictEqual(r.slice(1).map(function (x) { return x.reason; }),
          ['not_provisioned', 'not_provisioned', 'not_provisioned', 'expired', 'revoked']);
      });
    }),
    asyncTest('installFile: the store\'s own package; a path out of the user\'s files; a downgrade', function () {
      var down = store({ installed: [{ id: 'com.example.hello', source: 'store', version: '3.0.0' }] });
      var same = store({ installed: [{ id: 'com.example.hello', source: 'store', version: '2.0.0' }] });
      var s = store();
      return Promise.all([s.svc.installFile({ path: 'Downloads/hello.zpkg' }),
                          store().svc.installFile({ path: '../../etc/passwd' }),
                          down.svc.installFile({ path: 'a.zpkg' }),
                          same.svc.installFile({ path: 'a.zpkg' })]).then(function (r) {
        assert.strictEqual(r[0].ok, true);
        assert.strictEqual(r[1].reason, 'invalid_package');
        assert.strictEqual(r[2].reason, 'up_to_date');
        assert.strictEqual(r[3].ok, true);                    /* a developer's next build, same version */
      });
    }),
    asyncTest('apps list: a store app whose seal does not hold is not listed', function () {
      var s = store({ inspect: { 'com.example.hello': sealed(), 'com.example.bad': sealed({ id: 'com.example.bad', added: true }) } });
      var list = [{ id: 'com.zylos.settings', source: 'image' },
                  { id: 'com.example.hello', source: 'store', version: '2.0.0' },
                  { id: 'com.example.bad', source: 'store', version: '2.0.0' }];
      return s.seal.sealedOnly(list).then(function (kept) {
        assert.deepStrictEqual(kept.map(function (a) { return a.id; }), ['com.zylos.settings', 'com.example.hello']);
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
         timeout, so a reply to the newer request was dropped and lost.
         Ordering, not wall-clock margins: the first request's timeout fires
         before the second's, and the reply goes out in the microtask right
         after it — so a starved event loop cannot flip the sequence. */
      var st = make(['a', 'b']);
      var first = saveThen(st, 'a');
      var second = saveThen(st, 'b');          /* newer request, first still pending */
      return first.then(function () {          /* the older request just timed out */
        st.onState({ who: 'b' });              /* the reply to the newer request */
        return second;
      }).then(function () {
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
   Alarm service (contract of runtime/services/alarm): a trigger that fires
   with the app closed, so the notification is the OS's job, not the app's
   ═══════════════════════════════════════════════════════ */
console.log('\n■ alarm service');

var alarmTests = (function () {
  function make(store) {
    store = store || {};
    var posted = [];
    var waiting = [];
    var settingsSvc = {
      _getSetting: function (cat) { return Promise.resolve(store[cat] || null); },
      _updateSetting: function (cat, k, v) { store[cat] = store[cat] || {}; store[cat][k] = v; }
    };
    /* Posting is the alarm going off: tests wait for that, never for a clock —
       the whole suite shares one event loop and wall-clock waits go flaky */
    var notificationSvc = {
      post: function (p) {
        posted.push(p);
        waiting.splice(0).forEach(function (resolve) { resolve(); });
        return { id: posted.length };
      }
    };
    return {
      svc: ns.alarm({ settingsSvc: settingsSvc, notificationSvc: notificationSvc }),
      store: store, posted: posted,
      fired: function () {
        if (posted.length) return Promise.resolve();
        return new Promise(function (resolve) { waiting.push(resolve); });
      }
    };
  }
  function soon(ms) { return Date.now() + (ms || 5); }
  function after(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  /* A still-pending alarm timer would keep the test runner alive */
  function clear(t) {
    return t.svc.list().then(function (list) {
      return Promise.all(list.map(function (a) { return t.svc.cancel({ tag: a.tag, appId: a.appId }); }));
    });
  }

  return Promise.all([
    asyncTest('a one-shot alarm notifies at its time and then is gone', function () {
      var t = make();
      return t.svc.set({ tag: 'wake', triggerAt: soon(5), appId: 'com.zylos.clock', title: '07:00' })
        .then(function (ok) {
          assert.strictEqual(ok, true);
          return t.fired();
        }).then(function () {
          assert.strictEqual(t.posted.length, 1);
          assert.strictEqual(t.posted[0].title, '07:00');
          assert.strictEqual(t.posted[0].appId, 'com.zylos.clock');
          /* Do Not Disturb must not swallow an alarm */
          assert.strictEqual(t.posted[0].priority, 3);
          return t.svc.list();
        }).then(function (r) { assert.strictEqual(r.length, 0); });
    }),
    asyncTest('a repeating alarm stays registered and keeps its next time', function () {
      var t = make();
      return t.svc.set({ tag: 'mon', triggerAt: soon(5), intervalMs: 604800000,
                         appId: 'com.zylos.clock', repeating: true, title: 'Mon 07:00' })
        .then(function () { return t.fired(); })
        .then(function () { return t.svc.list(); })
        .then(function (r) {
          assert.strictEqual(r.length, 1);
          assert.ok(r[0].triggerAt > Date.now() + 604000000);
          return clear(t);
        });
    }),
    asyncTest('limits as the device: a repeat at most each minute, no app holds every slot', function () {
      var t = make();
      var later = Date.now() + 3600000;
      var bad = [0, 1, 59999, 31622400001, -1].map(function (ms) {
        return t.svc.set({ tag: 'r' + ms, triggerAt: later, intervalMs: ms, appId: 'a', repeating: true });
      });
      return Promise.all(bad).then(function (r) {
        assert.deepStrictEqual(r, [false, false, false, false, false]);
        var mine = [];
        for (var i = 0; i < 128; i++) mine.push(t.svc.set({ tag: 't' + i, triggerAt: later, appId: 'a' }));
        return Promise.all(mine);
      }).then(function () {
        return Promise.all([
          t.svc.set({ tag: 'one-more', triggerAt: later, appId: 'a' }),
          t.svc.set({ tag: 't0', triggerAt: later, appId: 'a', title: 'replaced' }),
          t.svc.set({ tag: 'wake', triggerAt: later, appId: 'com.zylos.clock' })
        ]);
      }).then(function (r) {
        assert.deepStrictEqual(r, [false, true, true]);
        return clear(t);
      });
    }),
    asyncTest('a cancelled alarm never fires; the same tag replaces, not adds', function () {
      var t = make();
      return t.svc.set({ tag: 'x', triggerAt: soon(5), appId: 'a', title: 'first' })
        .then(function () { return t.svc.set({ tag: 'x', triggerAt: soon(5), appId: 'a', title: 'second' }); })
        .then(function () { return t.svc.list(); })
        .then(function (r) {
          assert.strictEqual(r.length, 1);
          assert.strictEqual(r[0].title, 'second');
          return t.svc.cancel({ tag: 'x', appId: 'a' });
        }).then(function (ok) {
          assert.strictEqual(ok, true);
          return after(40);
        }).then(function () {
          assert.strictEqual(t.posted.length, 0);
          return t.svc.cancel({ tag: 'x', appId: 'a' });
        }).then(function (ok) { assert.strictEqual(ok, false); });
    }),
    /* Same rule as test_alarm.c: an alarm is its app's tag */
    asyncTest('another app\'s same tag is not replaced, cancelled or listed', function () {
      var t = make();
      return t.svc.set({ tag: 'wake', triggerAt: soon(60000), appId: 'com.zylos.clock', title: 'c' })
        .then(function () { return t.svc.set({ tag: 'wake', triggerAt: soon(60000), appId: 'com.example.evil', title: 'e' }); })
        .then(function () { return t.svc.cancel({ tag: 'wake', appId: 'com.example.evil' }); })
        .then(function () { return t.svc.cancel({ tag: 'wake', appId: 'com.example.evil' }); })
        .then(function (again) {
          assert.strictEqual(again, false);
          return Promise.all([t.svc.list({ appId: 'com.zylos.clock' }), t.svc.list({ appId: 'com.example.evil' })]);
        }).then(function (r) {
          assert.deepStrictEqual(r[0].map(function (a) { return a.title; }), ['c']);
          assert.strictEqual(r[1].length, 0);
          return clear(t);
        });
    }),
    asyncTest('a removed app leaves no alarm behind, in memory or stored', function () {
      var t = make();
      return t.svc.set({ tag: 'keep', triggerAt: soon(60000), appId: 'com.zylos.clock', title: 'k' })
        .then(function () { return t.svc.set({ tag: 'gone', triggerAt: soon(60000), appId: 'com.example.gone', title: 'g' }); })
        .then(function () {
          t.svc._forgetApp('com.example.gone');
          return t.svc.list();
        }).then(function (r) {
          assert.strictEqual(r.length, 1);
          assert.strictEqual(r[0].tag, 'keep');
          assert.ok(JSON.stringify(t.store).indexOf('com.example.gone') === -1);
          return clear(t);
        });
    }),
    asyncTest('alarms registered before a reload are armed again from storage', function () {
      var first = make();
      return first.svc.set({ tag: 'wake', triggerAt: soon(60000), appId: 'com.zylos.clock', title: '07:00' })
        .then(function () {
          /* Same store, fresh module: what the daemon does after a reboot */
          var second = make(first.store);
          return second.svc._ready
            .then(function () { return second.svc.list(); }).then(function (r) {
            assert.strictEqual(r.length, 1);
            assert.strictEqual(r[0].title, '07:00');
            return Promise.all([clear(first), clear(second)]);
          });
        });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Notification service: state kept in the OS, per app
   ═══════════════════════════════════════════════════════ */
console.log('\n■ notification service');

(function () {
  /* A notification lasts until it is dismissed or cancelled, as on the device:
     every test also checks the module armed no timer (it would arm one
     synchronously, so the swap cannot leak) */
  function untilDismissed(fn) {
    var realSet = global.setTimeout, armed = 0;
    global.setTimeout = function () { armed++; return 0; };
    try { fn(ns.notification({})); } finally { global.setTimeout = realSet; }
    assert.strictEqual(armed, 0, 'a notification armed a timer');
  }
  function ids(list) { return list.map(function (n) { return n.id; }); }
  var A = 'com.example.a', B = 'com.example.b';

  test('post: the next id each time, fields defaulted, no actions on a simple post', function () {
    untilDismissed(function (n) {
      var r = [n.post({ appId: A, title: 'Timer', body: 'done', actions: [{ label: 'x', actionId: 'x' }] }),
               n.post({ appId: A, title: 'Again' })];
      assert.deepStrictEqual(r, [{ id: 1 }, { id: 2 }]);
      var got = JSON.parse(JSON.stringify(n.getActive()[0]));
      assert.ok(got.timestamp > 0);
      delete got.timestamp;
      assert.deepStrictEqual(got, { id: 1, appId: A, channelId: '', title: 'Timer', body: 'done', icon: '',
                                    priority: 1, group: '', persistent: false, actions: [] });
    });
  });

  test('postWithActions keeps its buttons; none given is none', function () {
    untilDismissed(function (n) {
      n.postWithActions({ appId: A, title: 'Alarm', actions: [{ label: 'Snooze', actionId: 'snooze' }] });
      n.postWithActions({ appId: A, title: 'Plain' });
      assert.deepStrictEqual(n.getActive().map(function (x) { return x.actions; }),
                             [[{ label: 'Snooze', actionId: 'snooze' }], []]);
    });
  });

  test('cancel: gone from active, badge down once, still in history', function () {
    untilDismissed(function (n) {
      n.post({ appId: A, title: '1' });
      n.post({ appId: A, title: '2' });
      n.post({ appId: B, title: '3' });
      assert.deepStrictEqual(n.cancel({ id: 1 }), { success: true });
      assert.deepStrictEqual(n.cancel({ id: 1 }), { success: true });
      assert.deepStrictEqual(n.cancel({ id: 99 }), { success: true });
      assert.deepStrictEqual(ids(n.getActive()), [2, 3]);
      assert.deepStrictEqual(n.getAllBadgeCounts(), { 'com.example.a': 1, 'com.example.b': 1 });
      assert.strictEqual(n.getHistory().length, 3);
    });
  });

  test("cancel with an appId takes only that app's; without one (the OS) any app's", function () {
    untilDismissed(function (n) {
      var a = n.post({ appId: A, title: 'a' }), b = n.post({ appId: B, title: 'b' });
      assert.deepStrictEqual(n.cancel({ id: b.id, appId: A }), { success: true });
      assert.deepStrictEqual(ids(n.getActive()), [a.id, b.id]);
      n.cancel({ id: a.id, appId: A });
      n.cancel({ id: b.id, appId: '' });
      assert.deepStrictEqual(n.getActive(), []);
    });
  });

  test('badges count active notifications per app; apps at zero and no app are not listed', function () {
    untilDismissed(function (n) {
      n.post({ appId: A });
      n.post({ appId: A });
      var b = n.post({ appId: B });
      n.post({ title: 'from nobody' });
      assert.deepStrictEqual(n.getBadgeCount({ appId: A }), { appId: A, count: 2 });
      assert.deepStrictEqual(n.getBadgeCount({ appId: 'com.example.none' }), { appId: 'com.example.none', count: 0 });
      n.cancel({ id: b.id });
      assert.deepStrictEqual(n.getAllBadgeCounts(), { 'com.example.a': 2 });
    });
  });

  test('Do Not Disturb drops all but urgent, and a dropped one leaves no trace', function () {
    untilDismissed(function (n) {
      assert.deepStrictEqual(n.setDndMode({ enabled: true }), { success: true, enabled: true });
      assert.deepStrictEqual(n.getDndMode(), { enabled: true });
      assert.deepStrictEqual(n.post({ appId: A, priority: 2 }), { suppressed: true, reason: 'dnd' });
      assert.deepStrictEqual(n.postWithActions({ appId: A, actions: [] }), { suppressed: true, reason: 'dnd' });
      assert.deepStrictEqual(n.post({ appId: A, priority: 3, title: 'call' }), { id: 1 });
      assert.deepStrictEqual(ids(n.getActive()), [1]);
      assert.strictEqual(n.getHistory().length, 1);
      assert.deepStrictEqual(n.getAllBadgeCounts(), { 'com.example.a': 1 });
      assert.deepStrictEqual(n.setDndMode(), { success: true, enabled: false });
      assert.deepStrictEqual(n.post({ appId: A }), { id: 2 });
    });
  });

  test('every post stays until dismissed or cancelled: normal, urgent and persistent alike', function () {
    untilDismissed(function (n) {
      var normal = n.post({ appId: A, group: 'chat' });
      var urgent = n.post({ appId: A, priority: 3 });
      var sticky = n.post({ appId: A, persistent: true });
      assert.deepStrictEqual(ids(n.getActive()), [normal.id, urgent.id, sticky.id]);
      assert.deepStrictEqual(n.getAllBadgeCounts(), { 'com.example.a': 3 });
      assert.deepStrictEqual(n.getGroupSummary(), { 'com.example.a:chat': 1 });
    });
  });

  test('clearAll keeps persistent notifications', function () {
    untilDismissed(function (n) {
      n.post({ appId: A });
      var sticky = n.post({ appId: A, persistent: true });
      n.post({ appId: B, priority: 3 });
      assert.deepStrictEqual(n.clearAll(), { success: true });
      assert.deepStrictEqual(ids(n.getActive()), [sticky.id]);
      assert.deepStrictEqual(n.getAllBadgeCounts(), { 'com.example.a': 1 });
    });
  });

  test('history keeps the newest 100, by app and newest-first limit; clearing it keeps the active', function () {
    untilDismissed(function (n) {
      for (var i = 1; i <= 105; i++) n.post({ appId: i % 2 ? A : B, title: 't' + i });
      function titles(list) { return list.map(function (x) { return x.title; }); }
      var all = n.getHistory();
      assert.strictEqual(all.length, 100);
      assert.deepStrictEqual([all[0].title, all[99].title], ['t6', 't105']);
      var onlyB = n.getHistory({ appId: B });
      assert.strictEqual(onlyB.length, 50);
      assert.ok(onlyB.every(function (x) { return x.appId === B; }));
      assert.deepStrictEqual(titles(n.getHistory({ limit: 2 })), ['t104', 't105']);
      assert.deepStrictEqual(titles(n.getHistory({ appId: B, limit: 1 })), ['t104']);
      assert.deepStrictEqual(n.clearHistory(), { success: true });
      assert.deepStrictEqual(n.getHistory(), []);
      assert.strictEqual(n.getActive().length, 105);
    });
  });

  test('groups are per app and name; the summary counts the active; an emptied group is gone', function () {
    untilDismissed(function (n) {
      n.post({ appId: A, group: 'chat' });
      n.post({ appId: A, group: 'chat' });
      n.post({ appId: B, group: 'chat' });
      n.post({ appId: A });
      assert.deepStrictEqual(n.getGroupSummary(), { 'com.example.a:chat': 2, 'com.example.b:chat': 1 });
      assert.deepStrictEqual(ids(n.getGroup({ appId: A, group: 'chat' })), [1, 2]);
      assert.deepStrictEqual(ids(n.getGroup({ appId: B, group: 'chat' })), [3]);
      assert.deepStrictEqual(n.getGroup({ appId: A }), []);
      n.cancel({ id: 1 });
      n.cancel({ id: 2 });
      assert.deepStrictEqual(n.getGroup({ appId: A, group: 'chat' }), []);
      assert.deepStrictEqual(n.getGroupSummary(), { 'com.example.b:chat': 1 });
    });
  });

  test('a removed app leaves nothing: active (persistent too), history, badge, group', function () {
    untilDismissed(function (n) {
      var gone = 'com.example.gone';
      n.post({ appId: gone, group: 'chat' });
      n.post({ appId: gone, persistent: true });
      var kept = n.post({ appId: B, group: 'chat' });
      n._forgetApp(gone);
      assert.deepStrictEqual(ids(n.getActive()), [kept.id]);
      assert.deepStrictEqual(ids(n.getHistory()), [kept.id]);
      assert.deepStrictEqual(n.getAllBadgeCounts(), { 'com.example.b': 1 });
      assert.deepStrictEqual(n.getGroupSummary(), { 'com.example.b:chat': 1 });
      assert.strictEqual(JSON.stringify([n.getActive(), n.getHistory(), n.getAllBadgeCounts(),
                                         n.getGroupSummary()]).indexOf(gone), -1);
    });
  });
})();

/* ═══════════════════════════════════════════════════════
   App registry: the OS image's (and sealed store) apps
   ═══════════════════════════════════════════════════════ */
console.log('\n■ apps service');

var appsTests = (function () {
  var SYSTEM = ['com.zylos.settings', 'com.zylos.lockscreen'];
  var MANIFESTS = [
    { id: 'com.zylos.settings', name: 'Settings', iconKey: 'gear', color: 'icon-gray', version: '2.1.0',
      description: 'd', permissions: ['system'], formFactors: ['*'], iconSvg: '<svg/>',
      intents: [{ action: 'VIEW', mimeType: 'text/*' }], entry: 'main.html' },
    { id: 'com.zylos.lockscreen', launcher: false, system: false },
    { id: 'com.example.hello', name: 'Hello', system: true, source: 'store', intents: 'VIEW', launcher: 0 },
    { id: 'com.example.side', source: 'sideload' }
  ];
  /* The stubs stay as they were; this only remembers what they were given */
  var toPermissions = [], toRegistry = [];
  var realPerm = ZylPermissions.registerFromAppList, realReg = ZylAppRegistry.register;
  ZylPermissions.registerFromAppList = function (list) { toPermissions.push(list); return realPerm(list); };
  ZylAppRegistry.register = function (list) { toRegistry.push(list); return realReg(list); };

  function make(answer, seal) {
    var invoke = recordingInvoker({ list_installed_apps: answer });
    return { svc: ns.apps({ invoke: invoke, SYSTEM_APPS: SYSTEM, seal: seal }), invoke: invoke };
  }
  function byId(list) {
    var m = {};
    list.forEach(function (a) { m[a.id] = a; });
    return m;
  }

  return Promise.all([
    asyncTest('one entry per manifest: what it declares is kept, the rest defaulted', function () {
      return make(MANIFESTS).svc.getInstalled().then(function (list) {
        var a = byId(list);
        assert.strictEqual(list.length, 4);
        assert.deepStrictEqual(a['com.zylos.lockscreen'], {
          id: 'com.zylos.lockscreen', name: 'lockscreen', nameKey: 'app.lockscreen', icon: 'lockscreen',
          color: 'icon-blue', version: '1.0.0', description: '', system: true, permissions: [],
          formFactors: [], iconSvg: '', launcher: false, intents: [], entry: 'index.html', source: 'image' });
        var s = a['com.zylos.settings'];
        assert.deepStrictEqual([s.name, s.nameKey, s.icon, s.color, s.version, s.description, s.iconSvg, s.entry],
                               ['Settings', 'app.settings', 'gear', 'icon-gray', '2.1.0', 'd', '<svg/>', 'main.html']);
        assert.deepStrictEqual([s.permissions, s.formFactors, s.intents],
                               [['system'], ['*'], [{ action: 'VIEW', mimeType: 'text/*' }]]);
        assert.deepStrictEqual(a['com.example.hello'].intents, [], 'intents that are not a list are none');
      });
    }),
    asyncTest('the system flag is the OS whitelist\'s, never the manifest\'s', function () {
      return make(MANIFESTS).svc.getInstalled().then(function (list) {
        var a = byId(list);
        assert.strictEqual(a['com.zylos.settings'].system, true);
        assert.strictEqual(a['com.zylos.lockscreen'].system, true, 'manifest said false');
        assert.strictEqual(a['com.example.hello'].system, false, 'manifest said true');
        assert.strictEqual(a['com.example.side'].system, false);
      });
    }),
    asyncTest('only an explicit launcher:false hides an app; only source "store" is a store app', function () {
      return make(MANIFESTS).svc.getInstalled().then(function (list) {
        var a = byId(list);
        assert.deepStrictEqual([a['com.zylos.settings'].launcher, a['com.zylos.lockscreen'].launcher,
                                a['com.example.hello'].launcher], [true, false, true]);
        assert.deepStrictEqual([a['com.zylos.settings'].source, a['com.example.hello'].source,
                                a['com.example.side'].source], ['image', 'store', 'image']);
      });
    }),
    asyncTest('the list is asked for once and cached; getById reads it; invalidate asks again', function () {
      var host = MANIFESTS.slice(0, 2);
      var t = make(function () { return host; });
      assert.strictEqual(t.svc.getById({ id: 'com.zylos.settings' }), null, 'nothing before the first load');
      return t.svc.getInstalled().then(function (first) {
        return t.svc.getInstalled().then(function (again) {
          assert.strictEqual(again, first);
          assert.deepStrictEqual(t.invoke.calls, [{ cmd: 'list_installed_apps', args: undefined }]);
          assert.strictEqual(t.svc.getById({ id: 'com.zylos.settings' }).name, 'Settings');
          assert.strictEqual(t.svc.getById({ id: 'com.example.hello' }), null);
          host = MANIFESTS;
          t.svc._invalidate();
          return t.svc.getInstalled();
        });
      }).then(function (list) {
        assert.strictEqual(t.invoke.calls.length, 2);
        assert.strictEqual(list.length, 4);
        assert.strictEqual(t.svc.getById({ id: 'com.example.hello' }).source, 'store');
      });
    }),
    asyncTest('the mapped list is what the permission tables and the app registry get', function () {
      return make(MANIFESTS).svc.getInstalled().then(function (list) {
        assert.ok(toPermissions.indexOf(list) !== -1);
        assert.ok(toRegistry.indexOf(list) !== -1);
      });
    }),
    asyncTest('a store app is listed only while its seal holds', function () {
      var given = [];
      var seal = { sealedOnly: function (list) {
        given.push(list);
        return Promise.resolve(list.filter(function (m) { return m.id !== 'com.example.hello'; }));
      } };
      return make(MANIFESTS, seal).svc.getInstalled().then(function (list) {
        assert.deepStrictEqual(given, [MANIFESTS]);
        assert.deepStrictEqual(list.map(function (a) { return a.id; }),
                               ['com.zylos.settings', 'com.zylos.lockscreen', 'com.example.side']);
      });
    }),
    asyncTest('no apps, a malformed answer or a failure is an empty list, and nothing is cached', function () {
      var none = make(null);
      var failing = make(function () { return Promise.reject(new Error('io')); });
      var badSeal = make(MANIFESTS, { sealedOnly: function () { return Promise.reject(new Error('seal')); } });
      return Promise.all([none.svc.getInstalled(), make({ apps: MANIFESTS }).svc.getInstalled(),
                          make([]).svc.getInstalled(), failing.svc.getInstalled(),
                          badSeal.svc.getInstalled()]).then(function (r) {
        assert.deepStrictEqual(r, [[], [], [], [], []]);
        assert.strictEqual(none.svc.getById({ id: 'com.zylos.settings' }), null);
        return none.svc.getInstalled();
      }).then(function () {
        assert.strictEqual(none.invoke.calls.length, 2, 'an empty answer is asked again');
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Sandbox policy: system-ness is the whitelist's alone
   ═══════════════════════════════════════════════════════ */
console.log('\n■ sandbox service');

var sandboxTests = (function () {
  function make(installed, logs) {
    return ns.sandbox({
      appsSvc: { getInstalled: function () {
        return installed instanceof Error ? Promise.reject(installed) : Promise.resolve(installed);
      } },
      SYSTEM_APPS: ['com.zylos.settings'],
      logFn: logs ? function (level, tag, msg) { logs.push([level, tag, msg]); } : null
    });
  }
  /* What an installed-list entry claims is not what the policy says */
  var INSTALLED = [{ id: 'com.zylos.settings', system: false }, { id: 'com.example.hello', system: true }];

  return Promise.all([
    asyncTest('policy: system from the whitelist, not from the installed list\'s claim', function () {
      var svc = make(INSTALLED);
      return Promise.all([svc.getPolicy({ appId: 'com.zylos.settings' }),
                          svc.getPolicy({ appId: 'com.example.hello' })]).then(function (r) {
        assert.deepStrictEqual(r[0], { appId: 'com.zylos.settings', system: true, permissions: 0, seccompProfile: 'DEFAULT' });
        assert.deepStrictEqual(r[1], { appId: 'com.example.hello', system: false, permissions: 0, seccompProfile: 'DEFAULT' });
      });
    }),
    asyncTest('policy: an app that is not installed is not system, even under a system id', function () {
      var svc = make([{ id: 'com.example.hello' }]);
      return Promise.all([svc.getPolicy({ appId: 'com.zylos.settings' }), svc.getPolicy({})]).then(function (r) {
        assert.strictEqual(r[0].system, false);
        assert.deepStrictEqual([r[1].appId, r[1].system], ['', false]);
      });
    }),
    asyncTest('policy: without the app list it is null, not a default policy', function () {
      return make(new Error('down')).getPolicy({ appId: 'com.zylos.settings' }).then(function (r) {
        assert.strictEqual(r, null);
      });
    }),
    asyncTest('apply is logged with the app id, and works without a logger', function () {
      var logs = [];
      var svc = make(INSTALLED, logs);
      return Promise.all([svc.apply({ appId: 'com.example.hello' }), svc.apply({}),
                          make(INSTALLED).apply({ appId: 'com.example.hello' })]).then(function (r) {
        assert.deepStrictEqual(r, [true, true, true]);
        assert.deepStrictEqual(logs, [['INFO', 'sandbox', 'Policy applied for com.example.hello'],
                                      ['INFO', 'sandbox', 'Policy applied for unknown']]);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Accessibility: kept in settings, coerced on the way in and out
   ═══════════════════════════════════════════════════════ */
console.log('\n■ accessibility service');

var accessibilityTests = (function () {
  function make(stored, failing) {
    var writes = [];
    var settingsSvc = {
      _getSetting: function (cat) {
        if (failing) return Promise.reject(new Error('settings down'));
        return Promise.resolve(cat === 'accessibility' ? stored || null : null);
      },
      _updateSetting: function (cat, k, v) { writes.push([cat, k, v]); }
    };
    return { svc: ns.accessibility({ settingsSvc: settingsSvc }), writes: writes };
  }

  return Promise.all([
    asyncTest('nothing stored: contrast off, scale 1, no screen reader', function () {
      return make(null).svc.getState().then(function (s) {
        assert.deepStrictEqual(s, { highContrast: false, fontScale: 1, screenReader: false });
      });
    }),
    asyncTest('stored values read back as booleans and a number; a bad scale is 1', function () {
      return Promise.all([make({ highContrast: 1, fontScale: '1.25', screenReader: 'on' }).svc.getState(),
                          make({ fontScale: 'huge' }).svc.getState()]).then(function (r) {
        assert.deepStrictEqual(r[0], { highContrast: true, fontScale: 1.25, screenReader: true });
        assert.deepStrictEqual(r[1], { highContrast: false, fontScale: 1, screenReader: false });
      });
    }),
    asyncTest('each switch is stored in the accessibility settings, coerced, and answered', function () {
      var t = make(null);
      return Promise.all([t.svc.setHighContrast({ enabled: 'yes' }), t.svc.setFontScale({ scale: '1.5' }),
                          t.svc.setScreenReader({}), t.svc.setFontScale({ scale: 'x' })]).then(function (r) {
        assert.deepStrictEqual(r, [true, 1.5, false, 1]);
        assert.deepStrictEqual(t.writes, [['accessibility', 'highContrast', true], ['accessibility', 'fontScale', 1.5],
                                          ['accessibility', 'screenReader', false], ['accessibility', 'fontScale', 1]]);
        return t.svc.getState();
      }).then(function (s) {
        assert.deepStrictEqual(s, { highContrast: true, fontScale: 1, screenReader: false });
      });
    }),
    asyncTest('a settings failure answers null, not a guess', function () {
      return make(null, true).svc.getState().then(function (s) { assert.strictEqual(s, null); });
    }),
    asyncTest('speak: nothing to say or no speech engine is false; otherwise the text is spoken', function () {
      var svc = make(null).svc;
      var spoken = [];
      var noEngine = svc.speak({ text: 'hello' });
      /* The engine is used before the first await, so the swap cannot leak */
      window.speechSynthesis = { speak: function (u) { spoken.push(u.text); } };
      window.SpeechSynthesisUtterance = function (text) { this.text = text; };
      var r;
      try {
        r = [noEngine, svc.speak({ text: 'hello' }), svc.speak({ text: '' }), svc.speak()];
      } finally {
        delete window.speechSynthesis;
        delete window.SpeechSynthesisUtterance;
      }
      return Promise.all(r).then(function (v) {
        assert.deepStrictEqual(v, [false, true, false, false]);
        assert.deepStrictEqual(spoken, ['hello']);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Storage usage: host reading behind a 30-second cache
   ═══════════════════════════════════════════════════════ */
console.log('\n■ storage service');

var storageTests = (function () {
  var USAGE = { total: 64e9, used: 16e9, available: 48e9, percent: 25 };
  /* Each call reads one more byte used, so a fresh reading is told from a cached one */
  function counting() {
    var n = 0;
    return function () { n++; return { total: 100, used: n, available: 100 - n, percent: n }; };
  }

  return Promise.all([
    asyncTest('usage comes from the host once, then from the cache', function () {
      var invoke = recordingInvoker({ fs_get_usage: USAGE });
      var svc = ns.storage({ invoke: invoke });
      return svc.getUsage().then(function (u) {
        assert.strictEqual(u, USAGE);
        return svc.getUsage();
      }).then(function (u) {
        assert.strictEqual(u, USAGE);
        assert.deepStrictEqual(invoke.calls, [{ cmd: 'fs_get_usage', args: undefined }]);
      });
    }),
    asyncTest('a reading older than 30 seconds is asked for again', function () {
      var invoke = recordingInvoker({ fs_get_usage: counting() });
      var svc = ns.storage({ invoke: invoke });
      return svc.getUsage().then(function () {
        /* The age is checked before the first await, so the clock swap cannot leak */
        var realNow = Date.now;
        Date.now = function () { return realNow() + 30001; };
        try { return svc.getUsage(); } finally { Date.now = realNow; }
      }).then(function (u) {
        assert.strictEqual(u.used, 2);
        assert.strictEqual(invoke.calls.length, 2);
      });
    }),
    asyncTest('prefetch always asks the host, and getUsage then serves that reading', function () {
      var invoke = recordingInvoker({ fs_get_usage: counting() });
      var svc = ns.storage({ invoke: invoke });
      return svc.getUsage().then(function () { return svc.prefetch(); }).then(function (u) {
        assert.strictEqual(u.used, 2);
        return svc.getUsage();
      }).then(function (u) {
        assert.strictEqual(u.used, 2);
        assert.strictEqual(invoke.calls.length, 2);
      });
    }),
    asyncTest('a host failure answers the last reading, or zeros before any', function () {
      var fail = true;
      var svc = ns.storage({ invoke: recordingInvoker({ fs_get_usage: function () {
        return fail ? Promise.reject(new Error('io')) : USAGE;
      } }) });
      return svc.getUsage().then(function (u) {
        assert.deepStrictEqual(u, { total: 0, used: 0, available: 0, percent: 0 });
        fail = false;
        return svc.prefetch();
      }).then(function () {
        fail = true;
        return svc.prefetch();
      }).then(function (u) {
        assert.strictEqual(u, USAGE);
      });
    }),
    asyncTest('no answer is passed on as none and not cached', function () {
      var invoke = recordingInvoker({});
      var svc = ns.storage({ invoke: invoke });
      return svc.getUsage().then(function (u) {
        assert.strictEqual(u, null);
        return svc.getUsage();
      }).then(function () {
        assert.strictEqual(invoke.calls.length, 2);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Network: the sandbox whitelist before the host fetch
   ═══════════════════════════════════════════════════════ */
console.log('\n■ network service');

var networkTests = (function () {
  /* The whitelist is checked before the first await, so the swap cannot leak */
  function withSandbox(allowed, checked, fn) {
    var real = global.ZylSandbox;
    global.ZylSandbox = { isAllowedDomain: function (url) { checked.push(url); return allowed; } };
    try { return fn(); } finally { global.ZylSandbox = real; }
  }

  return Promise.all([
    asyncTest('an allowed URL is fetched by the host with the URL alone', function () {
      var checked = [];
      var invoke = recordingInvoker({ http_fetch: { status: 200, body: 'ok' } });
      var svc = ns.network({ invoke: invoke });
      return withSandbox(true, checked, function () {
        return svc.fetch({ url: 'https://api.example/x', method: 'POST', headers: { a: '1' } });
      }).then(function (r) {
        assert.deepStrictEqual(r, { status: 200, body: 'ok' });
        assert.deepStrictEqual(checked, ['https://api.example/x']);
        assert.deepStrictEqual(invoke.calls, [{ cmd: 'http_fetch', args: { url: 'https://api.example/x' } }]);
      });
    }),
    asyncTest('a domain outside the whitelist is refused and never reaches the host', function () {
      var checked = [];
      var invoke = recordingInvoker({ http_fetch: { status: 200 } });
      var svc = ns.network({ invoke: invoke });
      return withSandbox(false, checked, function () {
        return Promise.all([svc.fetch({ url: 'https://tracker.example/' }), svc.fetch({})]);
      }).then(function (r) {
        assert.deepStrictEqual(r, [{ error: 'Domain not in whitelist' }, { error: 'Domain not in whitelist' }]);
        assert.deepStrictEqual(checked, ['https://tracker.example/', ''], 'a missing URL is checked as empty');
        assert.strictEqual(invoke.calls.length, 0);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Credential store: exactly the keychain fields, nothing more
   ═══════════════════════════════════════════════════════ */
console.log('\n■ credential service');

var credentialTests = (function () {
  return Promise.all([
    asyncTest('store sends only service, account and secret', function () {
      var invoke = recordingInvoker({ credential_store: true });
      return ns.credential({ invoke: invoke })
        .store({ service: 'mail', account: 'bo', secret: 's3', appId: 'com.example.other' }).then(function (r) {
          assert.strictEqual(r, true);
          assert.deepStrictEqual(invoke.calls, [{ cmd: 'credential_store',
                                                  args: { service: 'mail', account: 'bo', secret: 's3' } }]);
        });
    }),
    asyncTest('lookup and delete never send a secret; lookup answers what the host found', function () {
      var invoke = recordingInvoker({ credential_lookup: 's3', credential_delete: true });
      var svc = ns.credential({ invoke: invoke });
      return Promise.all([svc.lookup({ service: 'mail', account: 'bo', secret: 'x' }),
                          svc['delete']({ service: 'mail', account: 'bo', secret: 'x' })]).then(function (r) {
        assert.deepStrictEqual(r, ['s3', true]);
        assert.deepStrictEqual(invoke.calls, [{ cmd: 'credential_lookup', args: { service: 'mail', account: 'bo' } },
                                              { cmd: 'credential_delete', args: { service: 'mail', account: 'bo' } }]);
      });
    }),
    asyncTest('a missing entry is null and a keychain failure reaches the caller', function () {
      var svc = ns.credential({ invoke: recordingInvoker({
        credential_store: function () { return Promise.reject(new Error('locked')); } }) });
      return svc.lookup({ service: 'mail', account: 'nobody' }).then(function (v) {
        assert.strictEqual(v, null);
        return svc.store({ service: 'mail', account: 'bo', secret: 's' });
      }).then(function () {
        throw new Error('a failed store answered success');
      }, function (e) {
        assert.strictEqual(e.message, 'locked');
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Location and user: host answers passed through, none invented
   ═══════════════════════════════════════════════════════ */
console.log('\n■ location service');

var locationTests = (function () {
  return Promise.all([
    asyncTest('getLastKnown asks the host, with nothing from the request, and passes its fix on', function () {
      var fix = { latitude: 37.5, longitude: 127.0, accuracy: 20 };
      var invoke = recordingInvoker({ location_get_last_known: fix });
      return ns.location({ invoke: invoke }).getLastKnown({ latitude: 0 }).then(function (r) {
        assert.strictEqual(r, fix);
        assert.deepStrictEqual(invoke.calls, [{ cmd: 'location_get_last_known', args: undefined }]);
      });
    }),
    asyncTest('no fix is null — no position invented', function () {
      return ns.location({ invoke: recordingInvoker({}) }).getLastKnown().then(function (r) {
        assert.strictEqual(r, null);
      });
    })
  ]);
})();

console.log('\n■ user service');

var userTests = (function () {
  return Promise.all([
    asyncTest("the device's owner, as runtime/services/user starts — nothing from the host or the request", function () {
      var svc = ns.user({ invoke: function () { throw new Error('the host is not asked'); } });
      return Promise.all([svc.getCurrent({ uid: 0 }), svc.listUsers({ all: true })]).then(function (r) {
        /* The facts are runtime/lib/users.h's (the device's user, fs, appstore read it) */
        var h = fs.readFileSync(path.join(__dirname, '..', 'runtime', 'lib', 'users.h'), 'utf8');
        var uid = parseInt(/#define ZYL_OWNER_UID\s+(\d+)/.exec(h)[1], 10);
        var root = /#define ZYL_USERS_ROOT\s+"([^"]+)"/.exec(h)[1];
        assert.strictEqual(r[0].uid, uid);
        assert.strictEqual(r[0].type, 'OWNER');
        assert.strictEqual(r[0].dataDir, root + '/' + uid);
        assert.deepStrictEqual(r[1], [r[0]]);
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   Push — same rules as runtime/services/push (tests/test_push.c, push_e2e.sh)
   ═══════════════════════════════════════════════════════ */
console.log('\n■ push service');

var pushTests = (function () {
  if (!global.crypto) global.crypto = require('crypto').webcrypto;
  function make(store, url) {
    store = store || {};
    var asked = [], posted = [], announced = [], replies = [];
    var svc = ns.push({
      invoke: function (cmd, a) {
        if (cmd === 'push_config') return Promise.resolve(JSON.stringify({ url: url === undefined ? 'https://push.example/' : url }));
        if (cmd === 'push_poll') { asked.push(a); return Promise.resolve(replies.shift() || []); }
        return Promise.resolve(null);
      },
      settingsSvc: {
        _getSetting: function (c) { return Promise.resolve(store[c] || null); },
        _updateSetting: function (c, k, v) { store[c] = store[c] || {}; store[c][k] = v; }
      },
      notificationSvc: { post: function (p) { posted.push(p); return { id: posted.length }; } }
    });
    return { svc: svc, store: store, asked: asked, posted: posted, announced: announced, replies: replies };
  }
  var realShell = global.ZylShell, realDispatch = global.window.dispatchEvent;

  return Promise.all([
    asyncTest('register: the app\'s own endpoint, the same each time; none without a server', function () {
      var t = make();
      return Promise.all([t.svc.register({ title: 'Chat', body: 'New' }, 'com.example.chat'),
                          t.svc.register({ title: 'Chat', body: 'New' }, 'com.example.chat'),
                          t.svc.register({ title: 'Mail', body: 'New' }, 'com.example.mail')]).then(function (e) {
        assert.ok(/^https:\/\/push\.example\/up[A-Za-z0-9]{12}\?up=1$/.test(e[0]), e[0]);
        assert.strictEqual(e[0], e[1]);
        assert.notStrictEqual(e[0], e[2]);
        return make({}, '').svc.register({ title: 'x' }, 'com.example.chat').then(function () {
          throw new Error('registered without a server');
        }, function (err) { assert.strictEqual(err.message, 'push unavailable'); });
      });
    }),
    asyncTest('a message: to the app in front as PushReceived, else the notification it registered', function () {
      var t = make();
      var events = [];
      global.ZylShell = { front: function () { return 'com.example.mail'; } };
      global.window.dispatchEvent = function (e) { events.push(e); return true; };
      return Promise.all([t.svc.register({ title: 'Chat', body: 'New message' }, 'com.example.chat'),
                          t.svc.register({ title: 'Mail', body: 'New mail' }, 'com.example.mail')]).then(function (e) {
        var chat = /\/(up[A-Za-z0-9]{12})\?/.exec(e[0])[1], mail = /\/(up[A-Za-z0-9]{12})\?/.exec(e[1])[1];
        t.replies.push([{ id: 'k1', event: 'message', topic: chat, message: 'version=1' },
                        { id: 'k2', event: 'message', topic: mail, message: 'hi' },
                        { id: 'k3', event: 'keepalive', topic: chat },
                        { id: 'k4', event: 'message', topic: 'upstranger00', message: 'x' }]);
        return t.svc._poll().then(function (n) {
          assert.strictEqual(n, 3);
          assert.deepStrictEqual(t.asked[0], { topics: [chat, mail].sort().join(','), since: '' });
          assert.deepStrictEqual(t.posted.map(function (p) { return [p.appId, p.title, p.body]; }),
                                 [['com.example.chat', 'Chat', 'New message']]);
          assert.deepStrictEqual(events.map(function (ev) { return [ev.type, ev.detail.appId, ev.detail.message]; }),
                                 [['zyl:push', 'com.example.mail', 'hi']]);
          return t.svc._poll();
        }).then(function () {
          assert.strictEqual(t.asked[1].since, 'k4');                 /* nothing asked for twice */
        });
      }).then(function () {
        global.ZylShell = realShell; global.window.dispatchEvent = realDispatch;
      }, function (err) {
        global.ZylShell = realShell; global.window.dispatchEvent = realDispatch;
        throw err;
      });
    }),
    asyncTest('registrations survive a reload; unregister and a removed app leave none', function () {
      var t = make();
      return t.svc.register({ title: 'Chat' }, 'com.example.chat').then(function (first) {
        var again = make(t.store);
        return again.svc.register({ title: 'Chat' }, 'com.example.chat').then(function (e) {
          assert.strictEqual(e, first);
          return again.svc.unregister({}, 'com.example.chat');
        }).then(function (ok) {
          assert.strictEqual(ok, true);
          return again.svc.unregister({}, 'com.example.chat');
        }).then(function (ok) {
          assert.strictEqual(ok, false);
          return again.svc.register({ title: 'x' }, 'com.example.gone');
        }).then(function () {
          again.svc._forgetApp('com.example.gone');
          assert.ok(t.store.push.state.indexOf('com.example.gone') === -1);
          return again.svc._poll();
        }).then(function (n) { assert.strictEqual(n, 0); });          /* no topics: nothing asked */
      });
    })
  ]);
})();

/* ═══════════════════════════════════════════════════════
   App content policy (real apps/system/sandbox.js, isolated) — the same
   strings tests/test_app_scheme.c expects of the device's zyl_app_scheme_csp
   ═══════════════════════════════════════════════════════ */
console.log('\n■ ZylSandbox content policy');

(function () {
  var vm = require('vm');
  var ctx = { window: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'apps', 'system',
                     'sandbox.js'), 'utf8'), ctx);
  var S = ctx.window.ZylSandbox;

  test('frame policy: camera and microphone by permission, geolocation never (as wam.c)', function () {
    assert.strictEqual(S.getPolicy('com.example.cam', ['camera', 'location']),
                       "camera 'self'; microphone 'none'; geolocation 'none'");
  });

  test('a user app without the network: its own files, nothing out', function () {
    assert.strictEqual(S.getContentPolicy(['camera']),
      "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; " +
      "img-src 'self' data: blob:; media-src 'self' blob:; connect-src 'self'; " +
      "font-src 'self'; frame-src 'self'; object-src 'none'");
  });

  test('with the network: https/wss out, never plain http, never remote or inline code', function () {
    assert.strictEqual(S.getContentPolicy(['network']),
      "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; " +
      "img-src 'self' data: blob: https:; media-src 'self' blob: https:; connect-src 'self' https: wss:; " +
      "font-src 'self' https:; frame-src 'self'; object-src 'none'");
  });

  test('network and webview (as a system app holds): https frames, still no inline code', function () {
    assert.strictEqual(S.getContentPolicy(['network', 'webview']),
      "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; " +
      "img-src 'self' data: blob: https:; media-src 'self' blob: https:; connect-src 'self' https: wss:; " +
      "font-src 'self' https:; frame-src 'self' https:; object-src 'none'");
  });
})();

/* ═══════════════════════════════════════════════════════
   Wait for all async tests and report
   ═══════════════════════════════════════════════════════ */
Promise.all([settingsTests, displayTests, inputTests, sensorsTests,
             usbTests, powerTests, audioTests, fsTests, updaterTests, telemetryTests, callTests, storeTests,
             netstatsTests, alertTests, lockTests, deviceBridgeTests, appStateTests,
             alarmTests, appsTests, sandboxTests, accessibilityTests, storageTests, networkTests,
             credentialTests, locationTests, userTests, gateTests, intentTests, pushTests])
  .then(function () {
    console.log('\n════════════════════════');
    console.log('Total: ' + total + '  Passed: ' + passed + '  Failed: ' + failed);
    console.log('════════════════════════');
    if (failed > 0) process.exit(1);
  }).catch(function (e) {
    console.error('Test runner error:', e);
    process.exit(1);
  });
