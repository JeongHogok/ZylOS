// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: App store service — the store's signed catalog; install, update, remove
// Scope: getCatalog ({ state, apps }), install / uninstall / installFile
//        ({ ok, reason }) — the same contract and rules as
//        runtime/services/appstore: the catalog and every package checked against
//        the store key, invalid entries skipped, the package's app.json must be
//        the catalog's app and version with its entry page, never a downgrade,
//        the image's apps are not the store's. Every package carries its seal
//        (app-seal.js): from the catalog the store's own only; from the user's
//        files (installFile) also a development build provisioned for this
//        device. The catalog's revoked developer certificates are kept
//        (store_revoked). Removing an app takes its whole container and makes
//        the OS forget it (forgetApp). Configuration: the OS image's system/store.json
// Dependency Direction: Domain -> invoker (store_*, fs_read_binary), app seal,
//        apps service, forgetApp
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
  /* Where a package from the user's files is unpacked before its id is known */
  var FILE_STAGE = 'local.package';

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

  /* The developer certificates the store revoked (their SHA-256s) */
  function parseRevoked(root) {
    return (Array.isArray(root.revoked) ? root.revoked : []).filter(function (h) {
      return typeof h === 'string' && HEX64.test(h);
    }).map(function (h) { return h.toLowerCase(); });
  }

  /* The valid entries, the first of an id winning; null when not a catalog */
  function parseCatalog(root, url) {
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
    var seal = deps.seal;
    var bytesOf = seal.bytesOf;
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
          return seal.verify(bytesOf(r[0]), null, sig, cfg.publicKey).then(function (ok) {
            if (!ok) return { state: 'untrusted' };       /* not signed by the store's key */
            var root = null;
            try { root = JSON.parse(text); } catch (e) { root = null; }
            var apps = parseCatalog(root, cfg.url);
            if (!apps) return { state: 'invalid' };
            /* What the store revoked stops development builds signed by it */
            return Promise.resolve(invoke('store_revoked', { list: parseRevoked(root) })).catch(function () {})
              .then(function () { return { state: 'ok', apps: apps, cfg: cfg }; });
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

    /* The unpacked app.json, if it is a valid app with its entry page (and, when
       given, the expected id and version); else null */
    function unpackedManifest(u, id, version) {
      var m;
      try { m = JSON.parse(u.manifest); } catch (e) { m = null; }
      var entry = (m && m.entry) || 'index.html';
      if (!m || !validId(m.id) || typeof m.version !== 'string' || !VERSION.test(m.version) ||
          (id && (m.id !== id || m.version !== version)) || !safeName(entry) ||
          !u.hashes || typeof u.hashes[entry] !== 'string') return null;
      return m;
    }

    /* Commit if the seal holds, else discard; the outcome */
    function commitSealed(u, m, stage, storeOnly) {
      return seal.check(u, m.id, m.version, storeOnly).then(function (why) {
        if (why) {
          invoke('store_discard', { appId: stage });
          return outcome(false, why);
        }
        return invoke('store_commit', { appId: m.id, stage: stage }).then(function () {
          appsSvc._invalidate();
          return appsSvc.getInstalled().then(function () {
            var r = outcome(true);
            r.appId = m.id;
            return r;
          });
        });
      });
    }

    function install(appId) {
      return Promise.all([catalog(), installed()]).then(function (r) {
        var c = r[0], have = r[1];
        if (c.state !== 'ok') return outcome(false, c.state);
        var a = c.apps.filter(function (e) { return e.id === appId; })[0];
        if (!a) return outcome(false, 'not_in_store');
        if (have.image[a.id]) return outcome(false, 'system_app');
        if (have.store[a.id] && versionCmp(have.store[a.id], a.version) >= 0) return outcome(false, 'up_to_date');
        return invoke('store_download', { url: a.packageUrl, max: a.size }).then(function (b64) {
          return seal.verify(bytesOf(b64), a.sha256, a.signature, c.cfg.publicKey).then(function (ok) {
            if (!ok) return outcome(false, 'bad_package');   /* tampered, corrupt, or not the store's */
            return invoke('store_unpack', { appId: a.id, 'package': b64 }).then(function (u) {
              var m = unpackedManifest(u, a.id, a.version);
              if (!m) {
                invoke('store_discard', { appId: a.id });
                return outcome(false, 'invalid_package');
              }
              return commitSealed(u, m, a.id, true);     /* the store's own seal only */
            }, function () { return outcome(false, 'invalid_package'); });
          });
        }, function () { return outcome(false, 'download_failed'); });
      });
    }

    /* A package from the user's files: a development build provisioned for this
       device, or the store's own. The same version again is allowed (a developer's
       next build), never a downgrade */
    function installFile(path) {
      if (typeof path !== 'string' || !safeName(path.replace(/^\/+/, ''))) {
        return Promise.resolve(outcome(false, 'invalid_package'));
      }
      return Promise.all([config(), installed()]).then(function (r) {
        if (!r[0]) return outcome(false, 'unconfigured');
        var have = r[1];
        return Promise.resolve(invoke('fs_read_binary', { path: path })).then(function (b64) {
          if (typeof b64 !== 'string' || b64.length > Math.ceil(MAX_PACKAGE / 3) * 4) return null;
          return invoke('store_unpack', { appId: FILE_STAGE, 'package': b64 });
        }).catch(function () { return null; }).then(function (u) {
          var m = u && unpackedManifest(u, null, null);
          var why = !m ? 'invalid_package' : have.image[m.id] ? 'system_app'
            : (have.store[m.id] && versionCmp(have.store[m.id], m.version) > 0) ? 'up_to_date' : '';
          if (why) {
            invoke('store_discard', { appId: FILE_STAGE });
            return outcome(false, why);
          }
          return commitSealed(u, m, FILE_STAGE, false);
        });
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
      installFile: function (p) {
        return exclusive(function () { return installFile(p && p.path); });
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
