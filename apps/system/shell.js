// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Shell Policy
//
// Role: Which app may be in front — the OS's decision, not the container's
// Scope: First setup (OOBE) and the lock screen, and what they allow: during
//        setup only setup is shown, while locked only the lock screen; calls,
//        emergency alerts and the lock screen's camera shortcut may show over
//        both. Home, back-exit, recents and app switching follow from that.
//        Only the lock screen unlocks and only setup finishes setup — the OS
//        names the sender, never the message. Home is never closed.
//        The system UI (status bar, notification panel, recents) is a surface
//        over every app: a bar, a banner for a new notification, or the whole
//        screen — only it asks, and the whole screen only when the user may
//        move between apps.
//        Same rules as runtime/wam/src/shell/shell.c
// Dependency Direction: Domain -> none (the container launches what it says)
// SOLID: SRP — shell policy only; launching and drawing are the container's
//
// ES5 only. No let/const/arrow.
// ----------------------------------------------------------

window.ZylShell = (function () {
  'use strict';

  var HOME = 'com.zylos.home';
  var LOCK = 'com.zylos.lockscreen';
  var SETUP = 'com.zylos.oobe';
  var SYSTEM_UI = 'com.zylos.statusbar';
  /* Shown over the lock screen and setup: an incoming or emergency call, an
     emergency alert, the lock screen's camera shortcut. Leaving them returns
     to the lock screen, and they cannot open anything else while locked. */
  var OVER_LOCK = ['com.zylos.phone', 'com.zylos.alerts', 'com.zylos.camera'];

  var _setup = false;
  var _locked = true;
  var _top = '';

  function overLock(appId) { return OVER_LOCK.indexOf(appId) !== -1; }

  /* What to bring to the front, or null when it is already there */
  function unless(appId) { return _top === appId ? null : appId; }

  /**
   * The device started: setup until it is done, then the lock screen.
   * @returns {string} the app to show
   */
  function boot(setupDone) {
    _setup = !setupDone;
    _locked = !!setupDone;
    _top = '';
    return _setup ? SETUP : LOCK;
  }

  /** May this app come to the front now? (an app, a notification, an intent) */
  function mayLaunch(appId) {
    if (!appId || appId === SYSTEM_UI) return false;   /* a surface, never the app in front */
    if (_setup) return appId === SETUP || overLock(appId);
    if (_locked) return appId === LOCK || overLock(appId);
    return appId !== LOCK && appId !== SETUP;
  }

  /** Home, and where an app the user backs out of goes. @returns {string|null} */
  function home() {
    if (_setup) return null;
    return unless(_locked ? LOCK : HOME);
  }

  /** May the user move between apps — recents, switching, the quick panel? */
  function navigable() { return !_setup && !_locked; }

  /** May this app be closed? Home never; the lock screen and setup not while they hold the device */
  function mayClose(appId) {
    if (appId === HOME || appId === SYSTEM_UI) return false;
    if (appId === LOCK) return !_locked;
    if (appId === SETUP) return !_setup;
    return true;
  }

  /** Lock now (the user, or the screen went off). Setup never locks. @returns {string|null} */
  function lock() {
    if (_setup) return null;
    _locked = true;
    return unless(LOCK);
  }

  /** The screen came on: what holds the device, unless a call or alert is in front. @returns {string|null} */
  function screenOn() {
    if (overLock(_top)) return null;
    if (_setup) return unless(SETUP);
    return _locked ? unless(LOCK) : null;
  }

  /**
   * The lock screen says the user got in (it asked the lock service).
   * @param {string} source the sending app, as the OS knows it
   * @param {string} [appId] what the user was opening (a notification's app)
   * @returns {string|null} the app to show, null when refused
   */
  function unlock(source, appId) {
    if (_setup || !_locked || source !== LOCK) return null;
    _locked = false;
    return appId && mayLaunch(appId) ? appId : HOME;
  }

  /** Setup says it is finished. @returns {string|null} home, null when refused */
  function setupDone(source) {
    if (!_setup || source !== SETUP) return null;
    _setup = false;
    _locked = false;
    return HOME;
  }

  /**
   * The system UI asks for its surface: 'bar', 'banner' (a new notification)
   * or 'full' (the panel, recents). @returns {string|null} what it gets, null
   * when the sender is not the system UI
   */
  function panelMode(source, mode) {
    if (source !== SYSTEM_UI) return null;
    if (mode === 'full') return navigable() ? 'full' : 'bar';
    if (mode === 'banner') return (_setup || _locked) ? 'bar' : 'banner';
    return 'bar';
  }

  /** The container brought this app to the front */
  function shown(appId) { _top = String(appId || ''); }

  return {
    boot: boot,
    mayLaunch: mayLaunch,
    home: home,
    navigable: navigable,
    mayClose: mayClose,
    lock: lock,
    screenOn: screenOn,
    unlock: unlock,
    setupDone: setupDone,
    shown: shown,
    panelMode: panelMode,
    systemUi: function () { return SYSTEM_UI; },
    /* The app in front — in the emulator the only one whose page is alive */
    front: function () { return _top; },
    isLocked: function () { return _locked; },
    isSetup: function () { return _setup; }
  };
})();
