// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Updater service — OTA update check and apply
// Scope: checkForUpdate, getState, applyUpdate
// Dependency Direction: Domain -> device info
// SOLID: SRP — OS update lifecycle only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.updater = function (deps) {
    var deviceRef = deps.deviceRef;

    return {
      checkForUpdate: function () {
        return Promise.resolve({
          available: false,
          currentVersion: deviceRef.osVersion,
          latestVersion: deviceRef.osVersion,
          channel: 'stable',
          updateServer: 'https://updates.zylos.dev/v1',
          message: 'Your OS is up to date.',
          updateProtocol: {
            partitionScheme: 'A/B', imageFormat: 'osimg',
            hashAlgorithm: 'SHA-256', signatureAlgorithm: 'RSA-2048',
            rollbackSupported: true
          }
        });
      },
      getState: function () {
        return Promise.resolve({
          state: 'UP_TO_DATE',
          version: deviceRef.osVersion,
          lastChecked: new Date().toISOString(),
          downloadProgress: 0,
          partitionScheme: 'A/B'
        });
      },
      applyUpdate: function () {
        return Promise.resolve({
          success: false, reason: 'emulator',
          message: 'In emulator mode, replace the OS image (.osimg) file to update.',
          instructions: [
            '1. Download the new OS image from updates.zylos.dev',
            '2. Place the .osimg file in the emulator data directory',
            '3. Restart the emulator to boot the new version'
          ]
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
