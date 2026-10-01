// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/Charter.hpp — capability policy for the renderer surface.
//
// A "charter" is the set of kernel capabilities a window's own document is
// allowed to reach (fs, net, shell, node, ...). It is the middle ground between
// Electron (no model at all: whatever the preload exposes is reachable) and
// Tauri (declarative capabilities baked into a config file):
//
//   · it is declared in TypeScript and applied at runtime, per window;
//   · it is deny-by-default: once a window has a charter, every call it did not
//     grant is refused by the kernel;
//   · it is revocable while the app is running.
//
// Enforcement happens on the RENDERER paths only (WebView messages, ow-rpc://,
// WebView2 messages). The main process (control socket) is trusted and is never
// filtered, so `module.invoke` from @owear/core keeps working.
//
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ow {

class Charter {
public:
    static Charter& Get();

    /// Allowed/denied entries. Entries are `module`, `module:fn` or `module:*`.
    struct Spec {
        bool enforce = false;
        std::vector<std::string> allow;
        std::vector<std::string> deny;
    };

    /// Installs (or replaces) a window charter. `spec.enforce == false` clears it.
    void Set(uint32_t windowId, Spec spec);

    /// Removes the charter: the window goes back to the default (no filtering).
    void Clear(uint32_t windowId);

    /// Drops the charter of a destroyed window.
    void Forget(uint32_t windowId);

    bool Enforced(uint32_t windowId) const;

    /// true when the window may call `module`/`fn`. Always true without a charter.
    bool Allows(uint32_t windowId, const std::string& module, const std::string& fn) const;

    /// Current charter (enforce=false when the window has none).
    Spec Get(uint32_t windowId) const;

    /// `module:fn` — the key used in errors, audits and introspection.
    static std::string Key(const std::string& module, const std::string& fn);

    /// Matcher for a single entry (`*`, `module`, `module:*`, `module:fn`).
    static bool MatchEntry(const std::string& entry, const std::string& module,
                           const std::string& fn);

private:
    Charter() = default;

    mutable std::mutex mu_;
    std::map<uint32_t, Spec> specs_;
};

/// Convenience guard for the renderer paths. Returns true when the call is
/// allowed; when it is not, `error` carries the refusal message and a
/// `charter.denied` audit event is broadcast to the main process.
bool GuardRendererCall(uint32_t windowId, const std::string& module,
                       const std::string& fn, std::string& error);

/// Same check, but for paths whose answer is an `ow-rpc://` style envelope
/// (`{"ok":false,"r":{...}}`). Returns true when allowed; otherwise `body`
/// already carries the refusal and nothing else must be executed.
bool RendererCallAllowed(uint32_t windowId, const std::string& module,
                         const std::string& fn, std::string& body);

} // namespace ow
