// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: App seal — a store-installed app's code is what was signed
// Scope: the rules of runtime/services/appstore/app_seal.c: .zyl-seal/seal.json
//        (every file's SHA-256) signed by the store's key, or by a developer key
//        that a store-signed certificate (expiry, revocation) and provisioning
//        profile (app id pattern, this device's ID, expiry) allow; the files
//        exactly the sealed set. check() gives '' or the reason the store
//        reports (bad_package, not_provisioned, expired, revoked); sealedOnly()
//        keeps the store apps whose seal holds, as the device's WAM registers.
//        Also the signature check the store uses for catalogs and packages
//        (RSA-SHA256 over the SHA-256, WebCrypto)
// Dependency Direction: Domain -> invoker (store_config, store_revoked,
//        store_inspect), deviceRef (this device's ID)
// SOLID: SRP — signatures and seals only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  var SEAL = '.zyl-seal/';

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

  function object(text) {
    var o = null;
    try { o = JSON.parse(text); } catch (e) { return null; }
    return o && typeof o === 'object' && !Array.isArray(o) ? o : null;
  }

  /* <name>.json signed (<name>.sig) by pem */
  function signedBy(seal, name, pem) {
    var json = seal[name + '.json'], sig = seal[name + '.sig'];
    if (typeof json !== 'string' || typeof sig !== 'string' || typeof pem !== 'string' || !pem) {
      return Promise.resolve(false);
    }
    return verify(new TextEncoder().encode(json), null, sig.trim(), pem);
  }

  /* An exact id, or a prefix "com.acme.*" (any id under com.acme.) */
  function appAllowed(patterns, id) {
    return Array.isArray(patterns) && patterns.some(function (p) {
      if (typeof p !== 'string') return false;
      var stem = p.slice(0, -1);
      return p === id || (p.length > 2 && p.slice(-2) === '.*' && id.indexOf(stem) === 0 && id.length > stem.length);
    });
  }

  /* Every file but the seal's own is sealed with its hash, and nothing else is */
  function filesMatch(sealed, hashes) {
    var names = Object.keys(hashes).filter(function (n) { return n.indexOf(SEAL) !== 0; });
    return names.length === Object.keys(sealed).length && names.every(function (n) {
      return typeof sealed[n] === 'string' && sealed[n].toLowerCase() === hashes[n];
    });
  }

  ns.appSeal = function (deps) {
    var invoke = deps.invoke;
    var deviceRef = deps.deviceRef || {};

    function storeKey() {
      return Promise.resolve(invoke('store_config')).then(function (text) {
        var c = object(text);
        return c && typeof c.publicKey === 'string' ? c.publicKey : '';
      }).catch(function () { return ''; });
    }

    function revoked() {
      return Promise.resolve(invoke('store_revoked', {})).then(function (list) {
        return Array.isArray(list) ? list : [];
      }).catch(function () { return []; });
    }

    /* A development build: the store vouches for the developer and says where it
       may run. '' and the developer's key, or the reason */
    function developerKey(s, appId, fp, pem, rev) {
      var cert = object(s['cert.json']), prof = object(s['profile.json']);
      return Promise.all([signedBy(s, 'cert', pem), signedBy(s, 'profile', pem)]).then(function (ok) {
        if (!cert || !prof || !ok[0] || !ok[1]) return { why: 'bad_package' };
        if (rev.indexOf(fp) !== -1) return { why: 'revoked' };
        var now = Date.now() / 1000, device = deviceRef.deviceId || '';
        if (!(cert.notAfter >= now) || !(prof.notAfter >= now)) return { why: 'expired' };
        if (prof.cert !== fp || !appAllowed(prof.apps, appId) || !device ||
            !Array.isArray(prof.devices) || prof.devices.indexOf(device) === -1) return { why: 'not_provisioned' };
        return { why: '', pem: cert.publicKey };
      });
    }

    /* found: { hashes, seal } as the container gives them (store_unpack, store_inspect).
       storeOnly: the store's own seal only (a catalog install) */
    function check(found, appId, version, storeOnly) {
      var s = (found && found.seal) || {}, hashes = (found && found.hashes) || {};
      var seal = object(s['seal.json']);
      if (!seal || !seal.files || typeof seal.files !== 'object' ||
          seal.id !== appId || (version && seal.appVersion !== version)) return Promise.resolve('bad_package');
      var developer = typeof s['cert.json'] === 'string';
      return Promise.all([storeKey(), revoked()]).then(function (r) {
        var pem = r[0];
        return (developer ? developerKey(s, appId, hashes[SEAL + 'cert.json'], pem, r[1])
                          : Promise.resolve({ why: '', pem: pem }));
      }).then(function (k) {
        if (k.why) return k.why;
        return signedBy(s, 'seal', k.pem).then(function (ok) {
          if (!ok || !filesMatch(seal.files, hashes) || (storeOnly && developer)) return 'bad_package';
          return '';
        });
      });
    }

    /* The list without the store apps whose seal does not hold */
    function sealedOnly(list) {
      return Promise.all((list || []).map(function (m) {
        if (m.source !== 'store') return true;
        return Promise.resolve(invoke('store_inspect', { appId: m.id })).then(function (found) {
          return check(found, m.id, m.version, false).then(function (why) { return !why; });
        }, function () { return false; });
      })).then(function (keep) {
        return (list || []).filter(function (m, i) { return keep[i]; });
      });
    }

    return { verify: verify, bytesOf: bytesOf, check: check, sealedOnly: sealedOnly };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
