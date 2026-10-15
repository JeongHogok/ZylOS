// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: App store service — the store's signed catalog; install, update, remove
// Scope: getCatalog ({ state, apps }), install / uninstall ({ ok, reason }) —
//        the same contract and rules as runtime/services/appstore: the catalog
//        and every package checked against the store key (RSA-SHA256 over the
//        SHA-256, WebCrypto), invalid entries skipped, the package's app.json
//        must be the catalog's app and version with its entry page, never a
//        downgrade, the image's apps are not the store's. Removing an app takes
//        its whole container and makes the OS forget it (forgetApp).
//        Configuration: the OS image's system/store.json
// Dependency Direction: Domain -> invoker (store_*), apps service, forgetApp
// SOLID: SRP — the store flow only; files are the container's (store.rs)
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  var MAX_PACKAGE = 50 * 1024 * 1024;
  var CATALOG_MAX = 1024 * 1024;
  var ID = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;
  var VERSION = /^[0-9]+(\.[0-9]+)*$/;
  var HEX64 = /^[0-9a-fA-F]{64}$/;
  var B64 = /^[A-Za-z0-9+\/=]+$/;

  /* Names a directory, reverse-domain (as store_catalog.c) */
  function validId(id) {
    return typeof id === 'string' && id.length <= 128 && ID.test(id) &&
      id.indexOf('..') === -1 && id.indexOf('.') !== -1;
  }

  function versionCmp(a, b) {
    var x = String(a || '').split('.'), y = String(b || '').split('.');
    for (var i = 0; i < Math.max(x.length, y.length); i++) {
      var m = parseInt(x[i] || '0', 10) || 0, n = parseInt(y[i] || '0', 10) || 0;
      if (m !== n) return m < n ? -1 : 1;
    }
    return 0;
  }

  function resolveUrl(base, ref) {
    try {
      var u = new URL(ref, base).href;
      return u.indexOf('https://') === 0 ? u : null;
    } catch (e) { return null; }
  }

  function bytesOf(b64) {
    var bin = atob(b64), out = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  function hex(buf) {
    var b = new Uint8Array(buf), s = '';
    for (var i = 0; i < b.length; i++) s += (b[i] < 16 ? '0' : '') + b[i].toString(16);
    return s;
  }

  /* RSA-SHA256 over the SHA-256 of bytes — what tools/build-store.py signs.
     sha: the expected digest, or null */
  function verify(bytes, sha, signature, pem) {
    var subtle = crypto.subtle;
    return subtle.digest('SHA-256', bytes).then(function (digest) {
      if (sha && hex(digest) !== sha.toLowerCase()) return false;
      var der = bytesOf(String(pem).replace(/-----[^-]+-----/g, '').replace(/\s+/g, ''));
      return subtle.importKey('spki', der, { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['verify'])
        .then(function (key) {
          return subtle.verify('RSASSA-PKCS1-v1_5', key, bytesOf(signature), digest);
        });
    }).catch(function () { return false; });
  }

  /* The valid entries, the first of an id winning; null when not a catalog */
  function parseCatalog(text, url) {
    var root;
    try { root = JSON.parse(text); } catch (e) { return null; }
    if (!root || !Array.isArray(root.apps)) return null;
    var seen = {}, out = [];
    root.apps.forEach(function (a) {
      if (!a || typeof a !== 'object') return;
      var pkg = typeof a['package'] === 'string' ? resolveUrl(url, a['package']) : null;
      var size = a.size;
      if (!validId(a.id) || seen[a.id] || typeof a.name !== 'string' || !a.name ||
          typeof a.version !== 'string' || !VERSION.test(a.version) ||
          typeof a.sha256 !== 'string' || !HEX64.test(a.sha256) ||
          typeof a.signature !== 'string' || !B64.test(a.signature) || !pkg ||
          typeof size !== 'number' || size <= 0 || size > MAX_PACKAGE || size % 1) return;
      seen[a.id] = true;
      out.push({
        id: a.id, name: a.name, version: a.version,
        description: typeof a.description === 'string' ? a.description : '',
        developer: typeof a.developer === 'string' ? a.developer : '',
        category: typeof a.category === 'string' ? a.category : '',
        size: size, sha256: a.sha256.toLowerCase(), signature: a.signature, packageUrl: pkg,
        permissions: Array.isArray(a.permissions)
          ? a.permissions.filter(function (p) { return typeof p === 'string'; }) : []
      });
    });
    return out;
  }

  /* A relative path of plain names (as store_package.c) */
  function safeName(name) {
    return typeof name === 'string' && !!name && name.charAt(0) !== '/' && name.indexOf('\\') === -1 &&
      name.split('/').every(function (p) { return p && p !== '.' && p !== '..'; });
  }

  ns.appstore = function (deps) {
    var invoke = deps.invoke;
    var appsSvc = deps.appsSvc;
    var SYSTEM_APPS = deps.SYSTEM_APPS || [];
    var _busy = false;

    function config() {
      return Promise.resolve(invoke('store_config')).then(function (text) {
        var c = JSON.parse(text);
        return (c && typeof c.url === 'string' && c.url.indexOf('https://') === 0 && c.publicKey) ? c : null;
      }).catch(function () { return null; });
    }

    /* { state } or { state: 'ok', apps, cfg } */
    function catalog() {
      return config().then(function (cfg) {
        if (!cfg) return { state: 'unconfigured' };
        return Promise.all([
          invoke('store_download', { url: cfg.url, max: CATALOG_MAX }),
          invoke('store_download', { url: cfg.url + '.sig', max: 4096 })
        ]).then(function (r) {
          var text = new TextDecoder().decode(bytesOf(r[0]));
          var sig = new TextDecoder().decode(bytesOf(r[1])).trim();
          return verify(bytesOf(r[0]), null, sig, cfg.publicKey).then(function (ok) {
            if (!ok) return { state: 'untrusted' };       /* not signed by the store's key */
            var apps = parseCatalog(text, cfg.url);
            return apps ? { state: 'ok', apps: apps, cfg: cfg } : { state: 'invalid' };
          });
        }, function () { return { state: 'unreachable' }; });
      });
    }

    /* What is on the device: store apps' versions, and the image's ids */
    function installed() {
      return appsSvc.getInstalled().then(function (list) {
        var store = {}, image = {};
        (list || []).forEach(function (a) {
          if (a.source === 'store') store[a.id] = a.version; else image[a.id] = true;
        });
        SYSTEM_APPS.forEach(function (id) { image[id] = true; });
        return { store: store, image: image };
      });
    }

    function outcome(ok, reason) { return { ok: ok, reason: reason || '' }; }

    function install(appId) {
      return Promise.all([catalog(), installed()]).then(function (r) {
        var c = r[0], have = r[1];
        if (c.state !== 'ok') return outcome(false, c.state);
        var a = c.apps.filter(function (e) { return e.id === appId; })[0];
        if (!a) return outcome(false, 'not_in_store');
        if (have.image[a.id]) return outcome(false, 'system_app');
        if (have.store[a.id] && versionCmp(have.store[a.id], a.version) >= 0) return outcome(false, 'up_to_date');
        return invoke('store_download', { url: a.packageUrl, max: a.size }).then(function (b64) {
          return verify(bytesOf(b64), a.sha256, a.signature, c.cfg.publicKey).then(function (ok) {
            if (!ok) return outcome(false, 'bad_package');   /* tampered, corrupt, or not the store's */
            return invoke('store_unpack', { appId: a.id, 'package': b64 }).then(function (u) {
              var m;
              try { m = JSON.parse(u.manifest); } catch (e) { m = null; }
              var entry = (m && m.entry) || 'index.html';
              if (!m || m.id !== a.id || m.version !== a.version || !safeName(entry) ||
                  (u.files || []).indexOf(entry) === -1) {
                invoke('store_discard', { appId: a.id });
                return outcome(false, 'invalid_package');
              }
              return invoke('store_commit', { appId: a.id }).then(function () {
                appsSvc._invalidate();
                return appsSvc.getInstalled().then(function () { return outcome(true); });
              });
            }, function () { return outcome(false, 'invalid_package'); });
          });
        }, function () { return outcome(false, 'download_failed'); });
      });
    }

    function exclusive(fn) {
      if (_busy) return Promise.resolve(outcome(false, 'busy'));
      _busy = true;
      return fn().then(function (r) { _busy = false; return r; }, function () {
        _busy = false;
        return outcome(false, 'io');
      });
    }

    return {
      getCatalog: function () {
        return Promise.all([catalog(), installed()]).then(function (r) {
          var c = r[0], have = r[1];
          if (c.state !== 'ok') return { state: c.state, apps: [] };
          return {
            state: 'ok',
            apps: c.apps.filter(function (a) { return !have.image[a.id]; }).map(function (a) {
              var v = have.store[a.id];
              return {
                id: a.id, name: a.name, version: a.version, description: a.description,
                developer: a.developer, category: a.category, size: a.size, permissions: a.permissions,
                installed: !!v, installedVersion: v || '', update: !!v && versionCmp(v, a.version) < 0
              };
            })
          };
        });
      },
      install: function (p) {
        return exclusive(function () { return install(p.appId); });
      },
      /* The app and everything it made: its container, and what the OS kept for it */
      uninstall: function (p) {
        return exclusive(function () {
          return installed().then(function (have) {
            if (!validId(p.appId) || have.image[p.appId]) return outcome(false, 'system_app');
            if (!have.store[p.appId]) return outcome(false, 'not_installed');
            return invoke('store_remove', { appId: p.appId }).then(function () {
              deps.forgetApp(p.appId);
              return appsSvc.getInstalled().then(function () { return outcome(true); });
            });
          });
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
