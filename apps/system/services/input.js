// ----------------------------------------------------------
// [Clean Architecture] Domain Layer - Service Module
//
// Role: Input service — keyboard visibility and layout
// Scope: show/hide keyboard, state query
// Dependency Direction: Domain -> none (in-memory state)
// SOLID: SRP — input state only
// ----------------------------------------------------------

(function (ns) {
  'use strict';

  ns.input = function (/* deps */) {
    var inputState = { visible: false, layout: 'en' };

    return {
      showKeyboard: function (p) {
        inputState.visible = true;
        inputState.layout = p.layout || inputState.layout;
        return Promise.resolve({ visible: true, layout: inputState.layout });
      },
      hideKeyboard: function () {
        inputState.visible = false;
        return Promise.resolve({ visible: false });
      },
      getState: function () {
        return Promise.resolve({ visible: inputState.visible, layout: inputState.layout });
      }
    };
  };
})(window.ZylServiceModules = window.ZylServiceModules || {});
