// ──────────────────────────────────────────────────────────
// [Clean Architecture] Shared Layer - Runtime
//
// Role: OS unified touch engine — scroll, page swipe, panel drag
// Scope: Vertical scroll on overflow containers, horizontal page snap,
//        vertical panel open/close — single implementation for all
// Dependency: None (standalone, loaded by every app + emulator compositor)
// SOLID: SRP — touch gesture handling only
//        OCP — new gesture types via createXxx() factory methods
//        DIP — consumers depend on abstract API, not implementation
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

window.ZylTouch = (function () {
  'use strict';

  /* ═══ Shared constants ═══ */
  var FRICTION = 0.95;
  var MIN_VELOCITY = 0.5;

  /* ═══ Utility ═══ */
  function getY(e) {
    if (e.touches) return e.touches[0].clientY;
    if (e.changedTouches) return e.changedTouches[0].clientY;
    return e.clientY || 0;
  }
  function getX(e) {
    if (e.touches) return e.touches[0].clientX;
    if (e.changedTouches) return e.changedTouches[0].clientX;
    return e.clientX || 0;
  }

  /* ═══════════════════════════════════════════════════════
     1. Vertical Scroll — auto-attaches to overflow containers
     ═══════════════════════════════════════════════════════ */
  function findScrollParent(el) {
    while (el && el !== document.body && el !== document.documentElement) {
      var style = window.getComputedStyle(el);
      var ov = style.overflowY;
      if ((ov === 'auto' || ov === 'scroll') && el.scrollHeight > el.clientHeight) {
        return el;
      }
      el = el.parentElement;
    }
    if (document.documentElement.scrollHeight > window.innerHeight) {
      return document.documentElement;
    }
    return null;
  }

  function enableScroll(root) {
    var doc = root || document;
    var active = null;
    var momId = null;

    function stopMomentum() {
      if (momId) { cancelAnimationFrame(momId); momId = null; }
    }

    function momentum(container, vel) {
      stopMomentum();
      (function step() {
        if (Math.abs(vel) < MIN_VELOCITY) return;
        container.scrollTop -= vel;
        vel *= FRICTION;
        momId = requestAnimationFrame(step);
      })();
    }

    doc.addEventListener('touchstart', function (e) {
      stopMomentum();
      var sp = findScrollParent(e.target);
      if (!sp) { active = null; return; }
      active = { el: sp, lastY: getY(e), lastT: Date.now(), vel: 0 };
    }, { passive: true });

    doc.addEventListener('touchmove', function (e) {
      if (!active) return;
      var y = getY(e);
      var dt = Date.now() - active.lastT;
      if (dt > 0) active.vel = (y - active.lastY) / dt * 16;
      active.el.scrollTop -= (y - active.lastY);
      active.lastY = y;
      active.lastT = Date.now();
    }, { passive: true });

    doc.addEventListener('touchend', function () {
      if (!active) return;
      if (Math.abs(active.vel) > MIN_VELOCITY) momentum(active.el, active.vel);
      active = null;
    }, { passive: true });

    /* Mouse fallback for desktop */
    var mDrag = null;
    doc.addEventListener('mousedown', function (e) {
      stopMomentum();
      var sp = findScrollParent(e.target);
      if (!sp) return;
      var tag = e.target.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'BUTTON' || tag === 'SELECT') return;
      if (e.target.closest('button, a, label, .toggle, .app-item, .dock-app, .kb-key')) return;
      mDrag = { el: sp, lastY: e.clientY, lastT: Date.now(), vel: 0, moved: false };
    });
    doc.addEventListener('mousemove', function (e) {
      if (!mDrag) return;
      var dy = e.clientY - mDrag.lastY;
      var dt = Date.now() - mDrag.lastT;
      if (dt > 0) mDrag.vel = dy / dt * 16;
      mDrag.el.scrollTop -= dy;
      mDrag.lastY = e.clientY;
      mDrag.lastT = Date.now();
      if (Math.abs(dy) > 3) mDrag.moved = true;
    });
    doc.addEventListener('mouseup', function () {
      if (!mDrag) return;
      if (mDrag.moved && Math.abs(mDrag.vel) > MIN_VELOCITY) momentum(mDrag.el, mDrag.vel);
      mDrag = null;
    });
  }


  /* ═══════════════════════════════════════════════════════
     2. Horizontal Page Swipe — snap to pages with momentum
     ═══════════════════════════════════════════════════════ */
  function createPageSwipe(viewport, track, opts) {
    opts = opts || {};
    var currentPage = 0;
    var totalPages = opts.totalPages || 1;
    var threshold = opts.threshold || 40;
    var velThreshold = opts.velocityThreshold || 0.3;
    var onPageChange = opts.onPageChange || function () {};
    var rubberBand = opts.rubberBand !== false;
    var drag = { active: false, startX: 0, startTime: 0, moved: false };

    function snapTo(idx, animate) {
      currentPage = Math.max(0, Math.min(idx, totalPages - 1));
      track.style.transition = (animate !== false)
        ? 'transform 0.3s cubic-bezier(0.25,0.46,0.45,0.94)' : 'none';
      track.style.transform = 'translateX(-' + (currentPage * 100) + '%)';
      onPageChange(currentPage);
    }

    function start(x) {
      drag.active = true;
      drag.startX = x;
      drag.startTime = Date.now();
      drag.moved = false;
      track.style.transition = 'none';
    }

    function move(x) {
      if (!drag.active) return;
      var dx = x - drag.startX;
      if (Math.abs(dx) > 5) drag.moved = true;
      var base = -(currentPage * 100);
      var vw = viewport.offsetWidth || 393;
      var pct = (dx / vw) * 100;
      if (rubberBand) {
        if ((currentPage === 0 && dx > 0) || (currentPage >= totalPages - 1 && dx < 0)) {
          pct *= 0.3;
        }
      }
      track.style.transform = 'translateX(' + (base + pct) + '%)';
    }

    function end(x) {
      if (!drag.active) return;
      drag.active = false;
      var dx = x - drag.startX;
      var dt = Date.now() - drag.startTime;
      var vel = Math.abs(dx) / Math.max(dt, 1);
      var next = currentPage;
      if (Math.abs(dx) > threshold || vel > velThreshold) {
        if (dx < 0 && currentPage < totalPages - 1) next++;
        else if (dx > 0 && currentPage > 0) next--;
      }
      snapTo(next, true);
    }

    /* Touch */
    viewport.addEventListener('touchstart', function (e) { start(getX(e)); }, { passive: true });
    viewport.addEventListener('touchmove', function (e) { move(getX(e)); }, { passive: true });
    viewport.addEventListener('touchend', function (e) { end(e.changedTouches[0].clientX); });
    /* Mouse */
    viewport.addEventListener('mousedown', function (e) { start(e.clientX); });
    document.addEventListener('mousemove', function (e) { if (drag.active) move(e.clientX); });
    document.addEventListener('mouseup', function (e) { if (drag.active) end(e.clientX); });

    return {
      snapTo: snapTo,
      getPage: function () { return currentPage; },
      setTotalPages: function (n) { totalPages = n; if (currentPage >= n) snapTo(n - 1, false); },
      hasMoved: function () { return drag.moved; },
      isActive: function () { return drag.active; }
    };
  }


  /* ═══════════════════════════════════════════════════════
     3. Vertical Panel Drag — pull-down/push-up with snap
     ═══════════════════════════════════════════════════════ */
  function createPanelDrag(handle, panel, opts) {
    opts = opts || {};
    var direction = opts.direction || 'down';  /* 'down' = pull to open, 'up' = push to open */
    var threshold = opts.threshold || 40;
    var onOpen = opts.onOpen || function () {};
    var onClose = opts.onClose || function () {};
    var getHeight = opts.getHeight || function () { return panel.offsetHeight || 400; };
    var isOpen = false;
    var drag = { active: false, startY: 0, currentY: 0 };

    function setTransition(on) {
      panel.style.transition = on ? 'transform 0.35s cubic-bezier(0.4,0,0.2,1), opacity 0.35s' : 'none';
    }

    function start(y, blockIframe) {
      drag.active = true;
      drag.startY = y;
      drag.currentY = y;
      setTransition(false);
      if (blockIframe) blockIframe(true);
    }

    function move(y) {
      if (!drag.active) return;
      drag.currentY = y;
      var dy = y - drag.startY;
      var h = getHeight();

      if (direction === 'down') {
        if (!isOpen) {
          var progress = Math.max(0, Math.min(dy / h, 1));
          panel.style.transform = 'translateY(' + (-h + h * progress) + 'px)';
          panel.style.opacity = String(progress);
        } else {
          var upDy = Math.min(0, dy);
          panel.style.transform = 'translateY(' + upDy + 'px)';
          panel.style.opacity = String(1 - Math.min(Math.abs(upDy) / h, 1));
        }
      }
    }

    function end(blockIframe) {
      if (!drag.active) return;
      drag.active = false;
      setTransition(true);
      if (blockIframe) blockIframe(false);

      var dy = drag.currentY - drag.startY;

      if (direction === 'down') {
        if (!isOpen && dy > threshold) {
          isOpen = true;
          panel.style.transform = 'translateY(0)';
          panel.style.opacity = '1';
          onOpen();
        } else if (isOpen && dy < -threshold) {
          isOpen = false;
          panel.style.transform = '';
          panel.style.opacity = '';
          onClose();
        } else {
          /* Snap back */
          if (isOpen) {
            panel.style.transform = 'translateY(0)';
            panel.style.opacity = '1';
          } else {
            panel.style.transform = '';
            panel.style.opacity = '';
          }
        }
      }
    }

    return {
      start: start,
      move: move,
      end: end,
      isOpen: function () { return isOpen; },
      setOpen: function (v) { isOpen = v; },
      isActive: function () { return drag.active; }
    };
  }


  /* ═══════════════════════════════════════════════════════
     4. Gesture Bar — multi-directional (swipe up/left/right)
     ═══════════════════════════════════════════════════════ */
  function createGestureBar(element, opts) {
    opts = opts || {};
    var onSwipeUp = opts.onSwipeUp || function () {};       /* big swipe up → home */
    var onSwipeUpSmall = opts.onSwipeUpSmall || function () {}; /* small swipe up → recents */
    var onSwipeLeft = opts.onSwipeLeft || function () {};    /* swipe left → next app */
    var onSwipeRight = opts.onSwipeRight || function () {};  /* swipe right → prev app */
    var upBigThreshold = opts.upBigThreshold || 60;
    var upSmallThreshold = opts.upSmallThreshold || 20;
    var horizontalThreshold = opts.horizontalThreshold || 30;
    var velocityThreshold = opts.velocityThreshold || 400;

    var state = { active: false, startX: 0, startY: 0, startTime: 0 };

    function start(x, y) {
      state.active = true;
      state.startX = x;
      state.startY = y;
      state.startTime = Date.now();
      element.classList.add('dragging');
    }

    function end(x, y) {
      if (!state.active) return;
      state.active = false;
      element.classList.remove('dragging');

      var dx = x - state.startX;
      var dy = state.startY - y; /* positive = up */
      var elapsed = Date.now() - state.startTime;
      var velocity = dy / Math.max(elapsed, 1) * 1000;

      if (Math.abs(dy) > upSmallThreshold && Math.abs(dy) > Math.abs(dx)) {
        if (dy > upBigThreshold || velocity > velocityThreshold) {
          onSwipeUp();
        } else if (dy > upSmallThreshold) {
          onSwipeUpSmall();
        }
      } else if (Math.abs(dx) > horizontalThreshold && Math.abs(dx) > Math.abs(dy)) {
        if (dx < 0) onSwipeLeft();
        else onSwipeRight();
      }
    }

    /* Touch */
    element.addEventListener('touchstart', function (e) {
      start(e.touches[0].clientX, e.touches[0].clientY);
    }, { passive: true });
    document.addEventListener('touchend', function (e) {
      if (state.active) end(
        e.changedTouches[0].clientX,
        e.changedTouches[0].clientY
      );
    });
    /* Mouse */
    element.addEventListener('mousedown', function (e) {
      start(e.clientX, e.clientY);
    });
    document.addEventListener('mouseup', function (e) {
      if (state.active) end(e.clientX, e.clientY);
    });

    /* Click fallback: tap (no significant movement) → onSwipeUp (home) */
    element.addEventListener('click', function (e) {
      /* Only trigger if no drag occurred */
      if (state.active) return;
    });

    return {
      isActive: function () { return state.active; }
    };
  }


  /* ═══ Auto-enable scroll on document load ═══ */
  if (typeof document !== 'undefined') {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', function () { enableScroll(); });
    } else {
      enableScroll();
    }
  }


  return {
    enableScroll: enableScroll,
    createPageSwipe: createPageSwipe,
    createPanelDrag: createPanelDrag,
    createGestureBar: createGestureBar,
    findScrollParent: findScrollParent
  };
})();
