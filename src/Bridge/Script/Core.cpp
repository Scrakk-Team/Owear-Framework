// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/Script/Core.cpp — parte base del bridge (transporte + _apply/_applyShm).
#include "Internal.hpp"

#include <string>

namespace ow {

std::string ScriptCore() {
    return R"JS((function() {
  // Charter: the privileged surface belongs to the TOP frame only. WebKitGTK and
  // WebView2 both inject this script into subframes, so an extension or remote
  // iframe would otherwise reach the kernel. Comparing window.top never throws.
  if (window.top !== window.self) return;
  if (window.__ow) return;
  var pending = new Map();
  var nextId = 1;
  var listeners = new Map();
  var ports = new Map();

  // Capture the transport (and JSON.stringify) at document-start, before any
  // page script runs. Looking them up again on every call let the page hook the
  // channel and read or rewrite whatever the app sends.
  var nativePost = null;
  if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
    nativePost = function(s) { window.chrome.webview.postMessage(s); };
  } else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ow) {
    var owHandler = window.webkit.messageHandlers.ow;
    nativePost = function(s) { owHandler.postMessage(s); };
  }
  var owStringify = JSON.stringify;

  function send(obj) {
    try {
      if (!nativePost) { console.error('[ow] transport no disponible'); return; }
      nativePost(owStringify(obj));
    } catch (e) { console.error('[ow] send error', e); }
  }

)JS";
}

} // namespace ow
