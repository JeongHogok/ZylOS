// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Terminal service — shell command execution proxy
// Scope: exec command via backend invoker
// Dependency Direction: Domain -> invoker abstraction
// SOLID: SRP — terminal exec only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.terminal = function (deps) {
    var invoke = deps.invoke;

    return {
      exec: function (p) { return invoke('exec_command', { command: p.command }); }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
