// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/Script/Core.cpp — parte base del bridge (transporte + _apply/_applyShm).
#include "Internal.hpp"

#include <string>

namespace ow {

std::string ScriptCore() {
    return R"JS((function() {
  if (window.__ow) return;
  var pending = new Map();
  var nextId = 1;
  var listeners = new Map();
  var ports = new Map();

  function send(obj) {
    try {
      if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
        window.chrome.webview.postMessage(JSON.stringify(obj));
      } else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ow) {
        window.webkit.messageHandlers.ow.postMessage(JSON.stringify(obj));
      } else {
        console.error('[ow] transport no disponible');
      }
    } catch (e) { console.error('[ow] send error', e); }
  }

)JS";
}

} // namespace ow
