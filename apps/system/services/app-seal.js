// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: App seal — a store-installed app's code is what was signed
// Scope: the rules of runtime/lib/app_seal.c: two keys — the root (the image's
//        store.json rootKey) signs a signing certificate (.zyl-seal/signing.json:
//        serial, public key, the serials it revokes), and that store key signs
//        .zyl-seal/seal.json (every file's SHA-256), or a developer key that a
//        certificate (expiry, revocation) and provisioning profile (app id
//        pattern, this device's ID, expiry) it signed allow; the files exactly
//        the sealed set; nothing of it revoked (the app, the certificate, the
//        signing key). check() gives '' or the reason the store reports
//        (bad_package, not_provisioned, expired, revoked); sealedOnly() keeps the
//        store apps whose seal holds, as the device's WAM registers. Also the
//        signature check the store uses for catalogs and packages (RSA-SHA256
//        over the SHA-256, WebCrypto) and the signing certificate check
// Dependency Direction: Domain -> invoker (store_config, store_revocations,
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

    function rootKey() {
      return Promise.resolve(invoke('store_config')).then(function (text) {
        var c = object(text);
        return c && typeof c.rootKey === 'string' ? c.rootKey : '';
      }).catch(function () { return ''; });
    }

    /* What the store revoked, as the device keeps it (app_seal.c's format) */
    function revocations() {
      function strings(v) { return Array.isArray(v) ? v.filter(function (x) { return typeof x === 'string' && x; }) : []; }
      return Promise.resolve(invoke('store_revocations', {})).then(function (r) {
        r = r && typeof r === 'object' ? r : {};
        return { issued: typeof r.issued === 'number' ? r.issued : 0,
                 certs: strings(r.certs), apps: strings(r.apps), keys: strings(r.keys) };
      }).catch(function () { return { issued: 0, certs: [], apps: [], keys: [] }; });
    }

    /* The root vouches for a store key: { why: '', pem, revokes } when it signed
       json (with sig) and rev does not revoke its serial */
    function signingKey(json, sig, root, rev) {
      var holder = { 'signing.json': json, 'signing.sig': sig };
      return signedBy(holder, 'signing', root).then(function (ok) {
        var c = ok ? object(json) : null;
        if (!c) return { why: 'bad_package' };
        var serial = c.serial, own = (Array.isArray(c.revokedSerials) ? c.revokedSerials : []).filter(function (n) {
          return typeof n === 'number' && n % 1 === 0 && n > 0;
        }).map(String);
        if (typeof serial !== 'number' || serial % 1 !== 0 || serial < 1 ||
            typeof c.publicKey !== 'string' || !c.publicKey) return { why: 'bad_package' };
        if (rev.keys.indexOf(String(serial)) !== -1) return { why: 'revoked' };
        return { why: '', pem: c.publicKey, revokes: own };
      });
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
      return Promise.all([rootKey(), revocations()]).then(function (r) {
        var rev = r[1];
        if (rev.apps.indexOf(appId) !== -1) return { why: 'revoked' };          /* the store took it back */
        return signingKey(s['signing.json'], s['signing.sig'], r[0], rev).then(function (k) {
          if (k.why) return k;
          return developer ? developerKey(s, appId, hashes[SEAL + 'cert.json'], k.pem, rev.certs)
                           : { why: '', pem: k.pem };
        });
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

    /* Who signed it: a development build's developer key, '' for the store's own
       (as the device's zyl_seal_signer_key — read, not verified) */
    function signerKey(found) {
      var s = (found && found.seal) || {};
      if (typeof s['cert.json'] !== 'string') return '';
      var cert = object(s['cert.json']);
      return (cert && typeof cert.publicKey === 'string' && cert.publicKey) || '?';
    }

    return { verify: verify, bytesOf: bytesOf, check: check, sealedOnly: sealedOnly, signerKey: signerKey,
             signingKey: signingKey, revocations: revocations };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
