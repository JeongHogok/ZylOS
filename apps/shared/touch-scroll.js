// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Runtime
//
// Role: OS touch scroll engine — enables smooth scroll on all scrollable containers
// Scope: Detects overflow containers, attaches touch handlers, applies momentum scroll
// Dependency: None (standalone, loaded by every app)
// SOLID: SRP — touch scroll behavior only
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  /* Configuration */
  var FRICTION = 0.95;
  var MIN_VELOCITY = 0.5;
  var SCROLL_MULTIPLIER = 1;

  /* Track active scrolls */
  var activeScroll = null;
  var momentumId = null;

  function findScrollParent(el) {
    while (el && el !== document.body) {
      var style = window.getComputedStyle(el);
      var overflowY = style.overflowY;
      if ((overflowY === 'auto' || overflowY === 'scroll') && el.scrollHeight > el.clientHeight) {
        return el;
      }
      el = el.parentElement;
    }
    /* Fallback: body/documentElement if content overflows viewport */
    if (document.documentElement.scrollHeight > window.innerHeight) {
      return document.documentElement;
    }
    return null;
  }

  function stopMomentum() {
    if (momentumId) {
      cancelAnimationFrame(momentumId);
      momentumId = null;
    }
  }

  function applyMomentum(container, velocity) {
    stopMomentum();
    function step() {
      if (Math.abs(velocity) < MIN_VELOCITY) return;
      container.scrollTop -= velocity;
      velocity *= FRICTION;
      momentumId = requestAnimationFrame(step);
    }
    step();
  }

  document.addEventListener('touchstart', function (e) {
    stopMomentum();
    var target = e.target;
    var scrollParent = findScrollParent(target);
    if (!scrollParent) { activeScroll = null; return; }

    activeScroll = {
      el: scrollParent,
      startY: e.touches[0].clientY,
      lastY: e.touches[0].clientY,
      lastTime: Date.now(),
      velocity: 0,
      scrollStart: scrollParent.scrollTop,
      moved: false
    };
  }, { passive: true });

  document.addEventListener('touchmove', function (e) {
    if (!activeScroll) return;
    var y = e.touches[0].clientY;
    var dy = y - activeScroll.lastY;
    var now = Date.now();
    var dt = now - activeScroll.lastTime;

    if (dt > 0) {
      activeScroll.velocity = dy / dt * 16; /* normalize to ~60fps */
    }

    activeScroll.el.scrollTop -= dy * SCROLL_MULTIPLIER;
    activeScroll.lastY = y;
    activeScroll.lastTime = now;

    if (Math.abs(y - activeScroll.startY) > 5) {
      activeScroll.moved = true;
    }
  }, { passive: true });

  document.addEventListener('touchend', function () {
    if (!activeScroll) return;
    if (activeScroll.moved && Math.abs(activeScroll.velocity) > MIN_VELOCITY) {
      applyMomentum(activeScroll.el, activeScroll.velocity);
    }
    activeScroll = null;
  }, { passive: true });

  /* Also handle mouse drag for desktop emulator testing */
  var mouseDrag = null;

  document.addEventListener('mousedown', function (e) {
    stopMomentum();
    var scrollParent = findScrollParent(e.target);
    if (!scrollParent) return;
    /* Only start if not on interactive elements */
    var tag = e.target.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'BUTTON' || tag === 'SELECT') return;
    if (e.target.closest('button, a, label, .toggle, .app-item, .dock-app')) return;

    mouseDrag = {
      el: scrollParent,
      startY: e.clientY,
      lastY: e.clientY,
      lastTime: Date.now(),
      velocity: 0,
      moved: false
    };
  });

  document.addEventListener('mousemove', function (e) {
    if (!mouseDrag) return;
    var dy = e.clientY - mouseDrag.lastY;
    var now = Date.now();
    var dt = now - mouseDrag.lastTime;

    if (dt > 0) {
      mouseDrag.velocity = dy / dt * 16;
    }

    mouseDrag.el.scrollTop -= dy * SCROLL_MULTIPLIER;
    mouseDrag.lastY = e.clientY;
    mouseDrag.lastTime = now;

    if (Math.abs(e.clientY - mouseDrag.startY) > 5) {
      mouseDrag.moved = true;
    }
  });

  document.addEventListener('mouseup', function () {
    if (!mouseDrag) return;
    if (mouseDrag.moved && Math.abs(mouseDrag.velocity) > MIN_VELOCITY) {
      applyMomentum(mouseDrag.el, mouseDrag.velocity);
    }
    mouseDrag = null;
  });

})();
