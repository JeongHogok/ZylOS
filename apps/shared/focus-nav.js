// ----------------------------------------------------------
// [Clean Architecture] Shared Layer - Input Adapter
//
// Role: Directional focus navigation — makes the UI operable without a
//       pointer (TV remote D-pad, rotary dial, watch crown)
// Scope: Focusable element discovery, spatial direction scoring, focus move,
//        activation, focus ring, re-seeding on DOM change
// Dependency Direction: Shared -> ZylFormFactor (policy). No service calls.
// SOLID: SRP — focus movement and activation only. Back/exit stays in the
//        OS navigation.back protocol and is deliberately not handled here.
//        DIP — activation is decided by the form-factor policy, not by UA sniffing
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// This file belongs to the OS image, not the emulator
// ----------------------------------------------------------

var ZylFocusNav = (function () {
  'use strict';

  var FOCUSABLE =
    'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), ' +
    'textarea:not([disabled]), [tabindex]:not([tabindex="-1"]), [data-focusable]';

  var FOCUS_CLASS = 'zyl-focus';

  /* Weight applied to off-axis distance. Above 1 so that an aligned but
     distant target beats a near but badly misaligned one — grids and
     lists both navigate predictably at 2.5. */
  var OFF_AXIS_WEIGHT = 2.5;

  var _enabled = false;
  var _current = null;
  var _observer = null;
  var _installed = false;   /* keydown listener registered exactly once */

  /* ═══ Candidate discovery ═══════════════════════════════ */
  function isVisible(el) {
    if (!el || el.disabled) return false;
    if (el.getAttribute && el.getAttribute('aria-hidden') === 'true') return false;
    if (el.offsetWidth > 0 || el.offsetHeight > 0) return true;
    return !!(el.getClientRects && el.getClientRects().length);
  }

  function candidates() {
    var all = document.querySelectorAll(FOCUSABLE);
    var out = [];
    var i;
    for (i = 0; i < all.length; i++) {
      if (isVisible(all[i])) out.push(all[i]);
    }
    return out;
  }

  /* ═══ Spatial scoring ═══════════════════════════════════
     Returns a non-negative cost, or -1 when the candidate is not in
     the requested direction. Lower cost wins.
     Rects are {left, top, width, height} — DOMRect compatible, which
     also makes this unit-testable without a DOM.
     ═══════════════════════════════════════════════════════════ */
  function score(from, to, dir) {
    var cx = from.left + from.width / 2;
    var cy = from.top + from.height / 2;
    var nx = to.left + to.width / 2;
    var ny = to.top + to.height / 2;
    var dx = nx - cx;
    var dy = ny - cy;
    var primary, offAxis;

    if (dir === 'left') {
      if (dx >= -1) return -1;
      primary = -dx; offAxis = dy < 0 ? -dy : dy;
    } else if (dir === 'right') {
      if (dx <= 1) return -1;
      primary = dx; offAxis = dy < 0 ? -dy : dy;
    } else if (dir === 'up') {
      if (dy >= -1) return -1;
      primary = -dy; offAxis = dx < 0 ? -dx : dx;
    } else if (dir === 'down') {
      if (dy <= 1) return -1;
      primary = dy; offAxis = dx < 0 ? -dx : dx;
    } else {
      return -1;
    }
    return primary + offAxis * OFF_AXIS_WEIGHT;
  }

  function rectOf(el) {
    var r = el.getBoundingClientRect();
    return { left: r.left, top: r.top, width: r.width, height: r.height };
  }

  /* ═══ Focus application ═════════════════════════════════ */
  function setFocus(el) {
    if (!el) return false;
    if (_current && _current.classList) _current.classList.remove(FOCUS_CLASS);
    _current = el;
    if (el.classList) el.classList.add(FOCUS_CLASS);
    try {
      el.focus();
    } catch (e) { /* element may be detached mid-move — ring class still applies */ }
    scrollIntoView(el);
    return true;
  }

  function scrollIntoView(el) {
    if (!el.scrollIntoView) return;
    try {
      el.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    } catch (e) {
      el.scrollIntoView(false); /* older WebKit: no options object */
    }
  }

  function focusFirst() {
    var list = candidates();
    if (!list.length) return false;
    /* Topmost-then-leftmost so the entry point is predictable */
    var best = list[0];
    var bestRect = rectOf(best);
    var i, r;
    for (i = 1; i < list.length; i++) {
      r = rectOf(list[i]);
      if (r.top < bestRect.top - 1 || (Math.abs(r.top - bestRect.top) <= 1 && r.left < bestRect.left)) {
        best = list[i]; bestRect = r;
      }
    }
    return setFocus(best);
  }

  /* ═══ Movement ══════════════════════════════════════════ */
  function move(dir) {
    var list = candidates();
    if (!list.length) return false;
    if (!_current || !isVisible(_current)) return focusFirst();

    var from = rectOf(_current);
    var best = null;
    var bestCost = -1;
    var i, cost;

    for (i = 0; i < list.length; i++) {
      if (list[i] === _current) continue;
      cost = score(from, rectOf(list[i]), dir);
      if (cost < 0) continue;
      if (bestCost < 0 || cost < bestCost) { bestCost = cost; best = list[i]; }
    }
    if (!best) return false;
    return setFocus(best);
  }

  function activate() {
    if (!_current) return false;
    if (_current.click) { _current.click(); return true; }
    return false;
  }

  /* ═══ Key handling ══════════════════════════════════════ */
  var KEYS = {
    ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right',
    Up: 'up', Down: 'down', Left: 'left', Right: 'right' /* older WebKit key names */
  };

  function onKeyDown(e) {
    if (!_enabled) return;

    var dir = KEYS[e.key];
    if (dir) {
      /* Let text inputs keep horizontal arrows for caret movement */
      var t = e.target;
      var isText = t && t.tagName &&
        (t.tagName === 'TEXTAREA' ||
         (t.tagName === 'INPUT' && (t.type === 'text' || t.type === 'password' ||
                                    t.type === 'search' || t.type === 'tel' ||
                                    t.type === 'email' || t.type === 'number')));
      if (isText && (dir === 'left' || dir === 'right')) return;

      if (move(dir)) e.preventDefault();
      return;
    }

    if (e.key === 'Enter' || e.key === ' ' || e.key === 'Spacebar') {
      /* Native controls already activate on Enter/Space — do not double-fire */
      if (document.activeElement === _current) return;
      if (activate()) e.preventDefault();
    }
  }

  /* ═══ Lifecycle ═════════════════════════════════════════ */
  function observe() {
    if (_observer || !window.MutationObserver) return;
    _observer = new window.MutationObserver(function () {
      /* Re-seed when the focused element was removed by a re-render */
      if (!_current || !isVisible(_current)) focusFirst();
    });
    _observer.observe(document.body, { childList: true, subtree: true });
  }

  /**
   * Enable directional navigation when the active form factor needs it.
   * No-op on pointer/gesture devices, so every app can call this blindly.
   * Idempotent: this module self-initialises on load, so an app calling it
   * again must not end up with two listeners moving focus twice per key.
   * @returns {boolean} whether directional navigation is enabled
   */
  function init() {
    var navModel = (window.ZylFormFactor && window.ZylFormFactor.getProfile().navModel) || 'gesture';
    if (navModel !== 'focus') return false;
    if (_installed) return true;

    _installed = true;
    _enabled = true;
    document.addEventListener('keydown', onKeyDown, true);
    focusFirst();
    observe();
    return true;
  }

  function setEnabled(on) { _enabled = !!on; }
  function isInstalled() { return _installed; }
  function getCurrent() { return _current; }
  function refresh() { if (_enabled && (!_current || !isVisible(_current))) focusFirst(); }

  return {
    init: init,
    move: move,
    activate: activate,
    focusFirst: focusFirst,
    refresh: refresh,
    setEnabled: setEnabled,
    isInstalled: isInstalled,
    getCurrent: getCurrent,
    /* Exposed for unit tests — pure function, no DOM needed */
    _score: score
  };
})();

if (typeof window !== 'undefined') { window.ZylFocusNav = ZylFocusNav; }

/* OS runtime module: self-initialises once the DOM is ready. init() is a
   no-op on gesture/pointer devices, so loading this everywhere is free. */
if (typeof document !== 'undefined') {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () { ZylFocusNav.init(); });
  } else {
    ZylFocusNav.init();
  }
}
