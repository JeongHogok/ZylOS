// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: DeviceInfo service — device metadata and uptime
// Scope: deviceName, osVersion, SoC, RAM, kernel, build info
// Dependency Direction: Domain -> none (static data)
// SOLID: SRP — device info only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.device = function (/* deps */) {
    var info = {
      deviceName: 'Zyl OS Device',
      osVersion: '0.1.0',
      soc: 'SpacemiT K1 (RISC-V)',
      ram: '2GB LPDDR4X',
      kernel: 'Linux 6.6.0-spacemit',
      build: 'ZYL.20260329',
      resolution: '1080x2400',
      hostname: 'zylos',
      username: 'user',
      bootTime: Date.now()
    };

    return {
      getInfo:   function () { return info; },
      getUptime: function () { return Promise.resolve(Math.floor((Date.now() - info.bootTime) / 1000)); },
      _applyProfile: function (profile) {
        if (!profile) return;
        info.deviceName = profile.name || info.deviceName;
        info.soc = profile.soc ? profile.soc + ' (RISC-V)' : info.soc;
        info.ram = profile.ram ? profile.ram + ' LPDDR4X' : info.ram;
        info.resolution = profile.screen || info.resolution;
        if (profile.id) {
          info.hostname = profile.id.replace(/^zyl-/, '').replace(/-/g, '_');
        }
      },
      _getRef: function () { return info; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
