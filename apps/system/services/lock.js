// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Screen lock credential — set and verify the PIN
// Scope: getState, verifyPin, setPin. PBKDF2-SHA256 salted hash only, the
//        wait after repeated failures, current PIN required to change/remove
// Dependency Direction: Domain -> settingsSvc (persistence), WebCrypto
// SOLID: SRP — lock credential only; screens live in lockscreen / settings
//
// On a device these reach runtime/services/lock (org.zylos.Lock) with the
// same contract and policy. No caller ever holds the PIN: the lockscreen and
// Settings ask this service instead of comparing against a stored value.
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.lock = function (deps) {
    var settingsSvc = deps.settingsSvc;
    var ITERATIONS = 50000, FREE_ATTEMPTS = 5, BASE_WAIT_S = 30, MAX_WAIT_S = 3600;

    function hex(bytes) {
      var s = '';
      for (var i = 0; i < bytes.length; i++) s += (bytes[i] < 16 ? '0' : '') + bytes[i].toString(16);
      return s;
    }

    function unhex(s) {
      var out = new Uint8Array(s.length / 2);
      for (var i = 0; i < out.length; i++) out[i] = parseInt(s.substr(i * 2, 2), 16);
      return out;
    }

    function derive(pin, salt, iterations) {
      var subtle = typeof crypto !== 'undefined' && crypto.subtle;
      /* Never fall back to storing or comparing the PIN itself */
      if (!subtle) return Promise.reject(new Error('WebCrypto unavailable'));
      return subtle.importKey('raw', new TextEncoder().encode(pin), 'PBKDF2', false, ['deriveBits'])
        .then(function (key) {
          return subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt: salt, iterations: iterations }, key, 256);
        })
        .then(function (bits) { return hex(new Uint8Array(bits)); });
    }

    function hashPin(pin) {
      var salt = crypto.getRandomValues(new Uint8Array(16));
      return derive(pin, salt, ITERATIONS).then(function (h) {
        return 'pbkdf2-sha256$' + ITERATIONS + '$' + hex(salt) + '$' + h;
      });
    }

    function matches(stored, pin) {
      var f = stored.split('$');
      if (f.length !== 4 || f[0] !== 'pbkdf2-sha256') return Promise.resolve(false);
      return derive(pin, unhex(f[2]), parseInt(f[1], 10)).then(function (h) { return h === f[3]; });
    }

    function nowS() { return Math.floor(Date.now() / 1000); }

    function waitFor(failures) {
      if (failures < FREE_ATTEMPTS) return 0;
      var w = BASE_WAIT_S;
      for (var step = Math.floor((failures - FREE_ATTEMPTS) / FREE_ATTEMPTS); step > 0 && w < MAX_WAIT_S; step--) w *= 2;
      return Math.min(w, MAX_WAIT_S);
    }

    function retryAfterMs(st, now) {
      var left = st.waitUntil - now;
      return left > 0 ? Math.min(left, MAX_WAIT_S) * 1000 : 0;
    }

    function save(st) {
      settingsSvc._updateSetting('lock', 'hash', st.hash);
      settingsSvc._updateSetting('lock', 'failures', st.failures);
      settingsSvc._updateSetting('lock', 'waitUntil', st.waitUntil);
    }

    /* Earlier builds kept the PIN in plain text under security.pin: move it */
    function load() {
      return Promise.all([settingsSvc._getSetting('lock'), settingsSvc._getSetting('security')])
        .then(function (r) {
          var l = r[0] || {}, sec = r[1] || {};
          var st = { hash: l.hash || '', failures: l.failures || 0, waitUntil: l.waitUntil || 0 };
          if (st.hash || !sec.pin) return st;
          return hashPin(String(sec.pin)).then(function (h) {
            st.hash = h;
            save(st);
            settingsSvc._updateSetting('security', 'pin', '');
            return st;
          });
        });
    }

    function result(ok, reason, st, now) {
      return { ok: ok, reason: reason, hasPin: !!st.hash, retryAfterMs: retryAfterMs(st, now) };
    }

    /* One attempt: 'ok', 'wrong' or 'wait' (not even tried while waiting) */
    function check(st, pin, now) {
      if (retryAfterMs(st, now)) return Promise.resolve('wait');
      return matches(st.hash, String(pin || '')).then(function (ok) {
        if (ok) { st.failures = 0; st.waitUntil = 0; }
        else {
          st.failures++;
          var w = waitFor(st.failures);
          st.waitUntil = w ? now + w : 0;
        }
        save(st);
        return ok ? 'ok' : 'wrong';
      });
    }

    function verifyPin(p) {
      var now = nowS();
      return load().then(function (st) {
        if (!st.hash) return result(true, '', st, now);
        return check(st, p && p.pin, now).then(function (r) {
          return result(r === 'ok', r === 'ok' ? '' : r === 'wait' ? 'locked_out' : 'wrong_pin', st, now);
        });
      });
    }

    function setPin(p) {
      var now = nowS();
      var pin = String((p && p.pin) || '');
      if (pin && !/^\d{4,16}$/.test(pin)) {
        return load().then(function (st) { return result(false, 'format', st, now); });
      }
      return load().then(function (st) {
        var verified = st.hash ? check(st, p && p.current, now) : Promise.resolve('ok');
        return verified.then(function (r) {
          if (r === 'wait') return result(false, 'locked_out', st, now);
          if (r === 'wrong') return result(false, 'wrong_pin', st, now);
          return (pin ? hashPin(pin) : Promise.resolve('')).then(function (h) {
            st.hash = h; st.failures = 0; st.waitUntil = 0;
            save(st);
            return result(true, '', st, now);
          });
        });
      });
    }

    return {
      getState: function () {
        var now = nowS();
        return load().then(function (st) { return result(true, '', st, now); });
      },
      verifyPin: verifyPin,
      setPin: setPin
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
