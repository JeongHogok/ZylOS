// ──────────────────────────────────────────────────────────
// [Clean Architecture] Presentation Layer - Page
//
// Role: Recovery screen — the boot whose user data partition did not mount
//       (the OS shows it then and only then: shell policy, runtime/lib/recovery.h)
// Scope: restart (power.reboot) to try again, or a factory reset the user
//        confirms (storage.eraseUserData: the OS erases the partition and
//        restarts; it refuses on any other boot)
// Dependency: ZylBridge (power, storage services), zylI18n
// SOLID: SRP — asking only; deciding and erasing are the OS's
//
// Clean Architecture, SOLID principles, i18n rules strictly followed
// Built for real device runtime; no mock/demo or hardcoded data allowed
// ──────────────────────────────────────────────────────────

(function () {
  'use strict';

  var statusEl = document.getElementById('status');
  var actionsEl = document.getElementById('actions');
  var confirmEl = document.getElementById('confirm');

  function t(key) { return zylI18n.t(key); }

  function say(key) {
    statusEl.textContent = t(key);
    statusEl.classList.remove('hidden');
  }

  function busy(on) {
    var buttons = document.querySelectorAll('button');
    for (var i = 0; i < buttons.length; i++) buttons[i].disabled = on;
  }

  document.getElementById('restart').addEventListener('click', function () {
    busy(true);
    ZylBridge.requestService('power', 'reboot').catch(function () { busy(false); });
  });

  document.getElementById('reset').addEventListener('click', function () {
    confirmEl.classList.remove('hidden');
    document.getElementById('cancel').focus();
  });

  document.getElementById('cancel').addEventListener('click', function () {
    confirmEl.classList.add('hidden');
  });

  document.getElementById('erase').addEventListener('click', function () {
    confirmEl.classList.add('hidden');
    busy(true);
    ZylBridge.requestService('storage', 'eraseUserData').then(function (started) {
      if (started !== true) throw new Error('refused');
      actionsEl.classList.add('hidden');
      say('recovery.erasing');
    }).catch(function () {
      busy(false);
      say('recovery.failed');
    });
  });

  zylI18n.onLocaleChange(function () { zylI18n.applyTranslations(); });
  zylI18n.applyTranslations();
})();
