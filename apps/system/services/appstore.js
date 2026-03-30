// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: AppStore service — install, uninstall, verify, package info
// Scope: app lifecycle management with persistence
// Dependency Direction: Domain -> settings, apps services
// SOLID: SRP — app store operations only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.appstore = function (deps) {
    var settingsSvc = deps.settingsSvc;
    var appsSvc = deps.appsSvc;
    var SYSTEM_APPS = deps.SYSTEM_APPS;

    function getInstalledApps() {
      return appsSvc.getInstalled();
    }

    return {
      install: function (p) {
        return settingsSvc._getSetting('appstore').then(function (store) {
          var installed = (store && store.installed) ? store.installed.split(',') : [];
          if (installed.indexOf(p.appId) === -1) {
            installed.push(p.appId);
          }
          var uninstalled = (store && store.uninstalled) ? store.uninstalled.split(',') : [];
          uninstalled = uninstalled.filter(function (id) { return id !== p.appId; });
          settingsSvc._updateSetting('appstore', 'installed', installed.join(','));
          settingsSvc._updateSetting('appstore', 'uninstalled', uninstalled.join(','));
          appsSvc._invalidate();
          return { success: true, appId: p.appId };
        });
      },
      uninstall: function (p) {
        if (SYSTEM_APPS.indexOf(p.appId) !== -1) {
          return Promise.resolve({ success: false, error: 'System app cannot be uninstalled' });
        }
        return settingsSvc._getSetting('appstore').then(function (store) {
          var uninstalled = (store && store.uninstalled) ? store.uninstalled.split(',') : [];
          if (uninstalled.indexOf(p.appId) === -1) {
            uninstalled.push(p.appId);
          }
          settingsSvc._updateSetting('appstore', 'uninstalled', uninstalled.join(','));
          appsSvc._invalidate();
          return { success: true, appId: p.appId };
        });
      },
      verify: function (p) {
        return getInstalledApps().then(function (list) {
          var app = null;
          for (var i = 0; i < list.length; i++) {
            if (list[i].id === p.appId) { app = list[i]; break; }
          }
          if (!app) {
            return { valid: false, appId: p.appId, error: 'App not found' };
          }
          var required = ['id', 'name', 'version'];
          var missing = [];
          for (var j = 0; j < required.length; j++) {
            if (!app[required[j]]) missing.push(required[j]);
          }
          if (missing.length > 0) {
            return { valid: false, appId: p.appId, error: 'Missing fields: ' + missing.join(', ') };
          }
          var verRegex = /^\d+\.\d+(\.\d+)?$/;
          if (!verRegex.test(app.version || '')) {
            return { valid: false, appId: p.appId, error: 'Invalid version format' };
          }
          return {
            valid: true, appId: p.appId, name: app.name, version: app.version,
            system: SYSTEM_APPS.indexOf(app.id) !== -1,
            hashAlgorithm: 'SHA-256', signatureAlgorithm: 'RSA-2048'
          };
        });
      },
      getAvailable: function () {
        return getInstalledApps().then(function (list) {
          return settingsSvc._getSetting('appstore').then(function (store) {
            var uninstalled = (store && store.uninstalled) ? store.uninstalled.split(',') : [];
            return list.map(function (app) {
              return {
                id: app.id, name: app.name, version: app.version,
                description: app.description,
                system: SYSTEM_APPS.indexOf(app.id) !== -1,
                installed: uninstalled.indexOf(app.id) === -1
              };
            });
          });
        });
      },
      getPackageInfo: function (p) {
        return getInstalledApps().then(function (list) {
          var app = null;
          for (var i = 0; i < list.length; i++) {
            if (list[i].id === p.appId) { app = list[i]; break; }
          }
          if (!app) return { error: 'App not found' };
          return {
            format: 'ospkg', id: app.id, name: app.name, version: app.version,
            permissions: app.permissions || [],
            packageStructure: {
              manifest: 'manifest.json', signature: 'signature.sig',
              signatureAlgorithm: 'RSA-2048', hashAlgorithm: 'SHA-256',
              entryPoint: 'index.html'
            }
          };
        });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
