// ----------------------------------------------------------
// [Clean Architecture] Shared Layer - Policy
//
// Role: Form-factor policy — resolves the active device class and the
//       UI/input model every app must adapt to
// Scope: Profile table (phone/tablet/tv/auto/watch/desktop/appliance/pda),
//        runtime detection, explicit override, CSS class + variable application,
//        per-app availability check
// Dependency Direction: Shared -> none (pure policy; DOM write is the only effect)
// SOLID: SRP — form-factor policy only, no layout and no input handling
//        OCP — new form factors are added to PROFILES without touching consumers
//        DIP — apps depend on getProfile(), never on screen size directly
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

window.ZylFormFactor = (function () {
  'use strict';

  var STORAGE_KEY = 'zyl.formFactor.override';

  /* ═══════════════════════════════════════════════════════════
     Profile table — the single source of truth for device classes.
     navModel  : 'gesture' | 'softkeys' | 'focus' | 'pointer'
     inputModel: primary input the UI must be operable with
     fontScale : base font multiplier (10-foot UI needs bigger text)
     touchTarget: minimum interactive size in px
     ═══════════════════════════════════════════════════════════ */
  var PROFILES = {
    phone: {
      id: 'phone', navModel: 'gesture', inputModel: 'touch',
      fontScale: 1.0, touchTarget: 44, density: 'compact', statusBar: true
    },
    tablet: {
      id: 'tablet', navModel: 'gesture', inputModel: 'touch',
      fontScale: 1.1, touchTarget: 48, density: 'regular', statusBar: true
    },
    tv: {
      id: 'tv', navModel: 'focus', inputModel: 'dpad',
      fontScale: 1.8, touchTarget: 64, density: 'spacious', statusBar: false
    },
    auto: {
      id: 'auto', navModel: 'focus', inputModel: 'touch-rotary',
      fontScale: 1.5, touchTarget: 76, density: 'spacious', statusBar: false
    },
    watch: {
      id: 'watch', navModel: 'focus', inputModel: 'crown',
      fontScale: 0.85, touchTarget: 40, density: 'micro', statusBar: false
    },
    desktop: {
      id: 'desktop', navModel: 'pointer', inputModel: 'mouse-keyboard',
      fontScale: 1.0, touchTarget: 28, density: 'regular', statusBar: true
    },
    appliance: {
      id: 'appliance', navModel: 'focus', inputModel: 'dpad',
      fontScale: 1.3, touchTarget: 64, density: 'spacious', statusBar: false
    },
    pda: {
      id: 'pda', navModel: 'softkeys', inputModel: 'touch-keypad',
      fontScale: 1.0, touchTarget: 48, density: 'compact', statusBar: true
    }
  };

  var DEFAULT_PROFILE = 'phone';

  var _active = null;

  /* ═══ Detection ═══════════════════════════════════════════
     Resolution order: container-reported input hardware (?zylInput=) first,
     then geometry + pointer capability. On a shipped device the build profile
     (meson -Dprofile) or provisioning is authoritative; the user override
     below outranks everything. Detection exists so the emulator and
     unprovisioned boards still render correctly.

     Note: Zyl OS maps device pixels to layout pixels 1:1 — a 1080x2400
     panel reports innerWidth 1080 — so size alone cannot separate a
     phone from a tablet. Aspect ratio is the usable signal: phones are
     16:9 or taller, tablets are 16:10 or squarer.

     shortcut: aspect cannot separate a 16:9 phone from a 1920x1080
     landscape touch panel (both 1.78) — such a panel needs the override.
     Same for auto, appliance and pda, which have no geometric signature.
     Set the override from OOBE or a board provisioning step.
     ═══════════════════════════════════════════════════════════ */
  var TABLET_MAX_ASPECT = 1.75;  /* just under 16:9 (1.778) */
  var TABLET_MIN_SHORT_SIDE = 600;

  /**
   * Input hardware reported by the runtime container (compositor/WAM/emulator)
   * via the app URL, e.g. "?zylInput=dpad". Media queries cannot see this: a
   * host mouse answers `pointer: fine` even when the device only has a remote.
   * The container states the hardware fact; this module still decides the class.
   * @returns {string|null} 'touch' | 'dpad' | 'crown' | 'mouse-keyboard' | ...
   */
  var _injectedInput = null;

  function hardwareInput() {
    if (_injectedInput) return _injectedInput;
    try {
      var m = /[?&]zylInput=([a-z-]+)/.exec(window.location.search || '');
      return m ? m[1] : null;
    } catch (e) {
      return null; /* no location (sandboxed document) — fall back to geometry */
    }
  }

  /**
   * Declare the input hardware directly, for containers that host OS UI in
   * their own document rather than launching it by URL — the compositor
   * mounting the keyboard, for instance. Same contract as ?zylInput=.
   * @param {string} model e.g. 'dpad', 'crown', 'touch'
   */
  function setHardwareInput(model) {
    _injectedInput = model || null;
    _active = null;        /* re-resolve on next use */
  }

  function detect() {
    var w = window.innerWidth || 0;
    var h = window.innerHeight || 0;
    var shortSide = w < h ? w : h;
    var longSide = w < h ? h : w;
    var aspect = shortSide > 0 ? (longSide / shortSide) : 1;

    /* Input hardware is decisive where it exists: a crown means a watch, a
       D-pad on a large panel means a TV, a rotary dial means a dashboard. */
    var input = hardwareInput();
    if (input === 'crown') return 'watch';
    if (input === 'touch-rotary') return 'auto';
    if (input === 'touch-keypad') return 'pda';
    if (input === 'dpad') return longSide >= 1280 ? 'tv' : 'appliance';
    if (input === 'mouse-keyboard') return 'desktop';

    var hasFinePointer = false;
    var hasNoPointer = false;
    if (window.matchMedia) {
      hasFinePointer = window.matchMedia('(pointer: fine)').matches;
      hasNoPointer = window.matchMedia('(pointer: none)').matches;
    }

    /* Watch: small and nearly square */
    if (longSide <= 520 && (shortSide / longSide) > 0.8) return 'watch';

    /* TV: large with no pointing device at all — a remote only sends keys */
    if (longSide >= 1280 && hasNoPointer) return 'tv';

    /* Desktop: large with a mouse */
    if (longSide >= 1024 && hasFinePointer) return 'desktop';

    /* Tablet: physically large and squarer than a phone */
    if (shortSide >= TABLET_MIN_SHORT_SIDE && aspect <= TABLET_MAX_ASPECT) return 'tablet';

    return DEFAULT_PROFILE;
  }

  /* ═══ Override — persisted device class for undetectable devices ═══ */
  function getOverride() {
    try {
      var v = window.localStorage.getItem(STORAGE_KEY);
      return (v && PROFILES[v]) ? v : null;
    } catch (e) {
      return null; /* storage unavailable (private mode / no quota) — detection only */
    }
  }

  function setOverride(id) {
    if (!PROFILES[id]) return false;
    try {
      window.localStorage.setItem(STORAGE_KEY, id);
    } catch (e) {
      return false;
    }
    return true;
  }

  function clearOverride() {
    try {
      window.localStorage.removeItem(STORAGE_KEY);
    } catch (e) { /* nothing to clear if storage is unavailable */ }
  }

  /* ═══ Resolution + application ═══════════════════════════ */
  function resolve() {
    if (_active) return _active;
    var id = getOverride() || detect();
    _active = PROFILES[id] || PROFILES[DEFAULT_PROFILE];
    return _active;
  }

  function getProfile() { return resolve(); }
  function getId() { return resolve().id; }
  function getAllIds() { return Object.keys(PROFILES); }

  /**
   * Write the active profile onto an element so CSS and focus-nav can react.
   * Idempotent — safe to call again after an override change.
   * @param {Element} [target] defaults to <html>. Pass a container to scope
   *        the form factor to OS UI hosted inside a non-OS document (the
   *        emulator chrome must not be scaled along with it).
   */
  function apply(target) {
    var p = resolve();
    var root = target || document.documentElement;
    if (!root) return p;

    getAllIds().forEach(function (id) {
      root.classList.remove('ff-' + id);
    });
    root.classList.add('ff-' + p.id);
    root.setAttribute('data-form-factor', p.id);
    root.setAttribute('data-nav-model', p.navModel);

    var style = root.style;
    style.setProperty('--ff-font-scale', String(p.fontScale));
    style.setProperty('--ff-touch-target', p.touchTarget + 'px');
    style.setProperty('--ff-density', p.density);

    /* Root scaling adapts the px-authored app CSS to this device class.
       Set here rather than in responsive.css because the legacy `zoom`
       property is parsed outside the standard cascade and silently ignores
       var() — verified in WebKit: `zoom: var(--x)` computes to 1 while a
       literal applies. Keeping the factor in PROFILES avoids a second copy. */
    style.zoom = p.fontScale === 1 ? '' : String(p.fontScale);

    /* Viewport units are not affected by zoom: inside a root scaled by Z,
       100vh still means 100% of the unscaled viewport, so it renders Z times
       too tall. Apps use var(--ff-vh)/var(--ff-vw) instead of vh/vw. */
    if (p.fontScale === 1) {
      style.setProperty('--ff-vh', '100vh');
      style.setProperty('--ff-vw', '100vw');
    } else {
      style.setProperty('--ff-vh', 'calc(100vh / ' + p.fontScale + ')');
      style.setProperty('--ff-vw', 'calc(100vw / ' + p.fontScale + ')');
    }
    return p;
  }

  /**
   * Re-resolve after an override change and re-apply.
   * @returns {Object} the newly active profile
   */
  function reload(target) {
    _active = null;
    return apply(target);
  }

  /* ═══ Per-app availability ═══════════════════════════════
     An app declares supported form factors in app.json.
     Absent or '*' means universal — keeps older manifests working.
     ═══════════════════════════════════════════════════════════ */
  function supportsApp(formFactors) {
    if (!formFactors || !formFactors.length) return true;
    if (formFactors.indexOf('*') !== -1) return true;
    return formFactors.indexOf(getId()) !== -1;
  }

  return {
    PROFILES: PROFILES,
    detect: detect,
    hardwareInput: hardwareInput,
    setHardwareInput: setHardwareInput,
    getProfile: getProfile,
    getId: getId,
    getAllIds: getAllIds,
    apply: apply,
    reload: reload,
    getOverride: getOverride,
    setOverride: setOverride,
    clearOverride: clearOverride,
    supportsApp: supportsApp
  };
})();

/* OS runtime module: applying itself on load is what makes every app
   form-factor aware without each app opting in. Idempotent. */
if (typeof document !== 'undefined' && document.documentElement) {
  window.ZylFormFactor.apply();
}
