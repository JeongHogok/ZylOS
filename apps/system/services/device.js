// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: DeviceInfo service — device metadata and uptime
// Scope: deviceName, osVersion, securityPatch, SoC, RAM, kernel, build info;
//        deviceId (the hardware's own ID — what development profiles list) is
//        getDeviceId only, the system's (permissions.js): it outlives a factory
//        reset, a tracking ID if every app could read it — as the device's GetDeviceId
// Dependency Direction: Domain -> none (static data)
// SOLID: SRP — device info only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.device = function (/* deps */) {
    /* Version and patch level come from the OS image (apps/system/release.js),
       not from a copy here — there used to be four copies of the version */
    var release = (typeof window !== 'undefined' && window.ZylRelease) || {};
    /* What the virtual device is comes from its profile (_applyProfile);
       it has no kernel of its own — same fields as the device's GetInfo */
    var info = {
      deviceName: '',
      osVersion: release.version || 'unknown',
      securityPatch: release.securityPatch || 'unknown',
      soc: '',
      ram: '',
      kernel: '',
      build: release.version || 'unknown',
      resolution: '',
      hostname: '',
      username: 'user',
      deviceId: '',
      bootTime: Date.now()
    };

    return {
      getInfo:   function () {
        var out = {};
        Object.keys(info).forEach(function (k) { if (k !== 'deviceId') out[k] = info[k]; });
        return out;
      },
      getDeviceId: function () { return { deviceId: info.deviceId }; },
      getUptime: function () { return Promise.resolve(Math.floor((Date.now() - info.bootTime) / 1000)); },
      _applyProfile: function (profile) {
        if (!profile) return;
        info.deviceName = profile.name || info.deviceName;
        info.soc = profile.soc ? profile.soc + ' (RISC-V)' : info.soc;
        info.ram = profile.ram ? profile.ram + ' LPDDR4X' : info.ram;
        info.resolution = profile.screen || info.resolution;
        info.deviceId = profile.deviceId || info.deviceId;
        if (profile.id) {
          info.hostname = profile.id.replace(/^zyl-/, '').replace(/-/g, '_');
        }
      },
      _getRef: function () { return info; }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
