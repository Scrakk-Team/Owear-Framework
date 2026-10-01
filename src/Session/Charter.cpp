// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/Charter.cpp — capability policy for the renderer surface.
#include "Charter.hpp"

#include "../Control/ControlServer.hpp"
#include "ow/Json.h"

#include <algorithm>

namespace ow {

namespace {

/// Wildcards: a bare `*` grants everything.
bool IsWildcard(const std::string& s) { return s == "*"; }

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

} // namespace

Charter& Charter::Get() {
    static Charter instance;
    return instance;
}

std::string Charter::Key(const std::string& module, const std::string& fn) {
    return module + ":" + fn;
}

bool Charter::MatchEntry(const std::string& rawEntry, const std::string& module,
                         const std::string& fn) {
    const std::string entry = Trim(rawEntry);
    if (entry.empty()) return false;
    if (IsWildcard(entry)) return true;

    const auto colon = entry.find(':');
    if (colon == std::string::npos) return entry == module; // whole module

    const std::string mod = entry.substr(0, colon);
    const std::string method = entry.substr(colon + 1);
    if (!(IsWildcard(mod) || mod == module)) return false;
    return IsWildcard(method) || method == fn;
}

void Charter::Set(uint32_t windowId, Spec spec) {
    std::lock_guard lock(mu_);
    if (!spec.enforce) {
        specs_.erase(windowId);
        return;
    }
    specs_[windowId] = std::move(spec);
}

void Charter::Clear(uint32_t windowId) {
    std::lock_guard lock(mu_);
    specs_.erase(windowId);
}

void Charter::Forget(uint32_t windowId) { Clear(windowId); }

Charter::Spec Charter::Get(uint32_t windowId) const {
    std::lock_guard lock(mu_);
    auto it = specs_.find(windowId);
    return it == specs_.end() ? Spec{} : it->second;
}

bool Charter::Enforced(uint32_t windowId) const {
    std::lock_guard lock(mu_);
    return specs_.find(windowId) != specs_.end();
}

bool Charter::Allows(uint32_t windowId, const std::string& module,
                     const std::string& fn) const {
    Spec spec;
    {
        std::lock_guard lock(mu_);
        auto it = specs_.find(windowId);
        if (it == specs_.end()) return true; // no charter → default behaviour
        spec = it->second;
    }
    // deny always wins: an explicit refusal is checked first.
    for (const auto& d : spec.deny)
        if (MatchEntry(d, module, fn)) return false;
    for (const auto& a : spec.allow)
        if (MatchEntry(a, module, fn)) return true;
    return false; // deny-by-default
}

bool GuardRendererCall(uint32_t windowId, const std::string& module,
                       const std::string& fn, std::string& error) {
    if (Charter::Get().Allows(windowId, module, fn)) return true;

    const std::string key = Charter::Key(module, fn);
    error = "charter: capability '" + key + "' is not granted to this window";

    json::Object payload;
    payload.emplace_back("windowId", json::Value(static_cast<int64_t>(windowId)));
    payload.emplace_back("capability", json::Value(key));
    payload.emplace_back("module", json::Value(module));
    payload.emplace_back("fn", json::Value(fn));
    ControlServer::Get().BroadcastEvent(
        "charter.denied", json::Value(std::move(payload)).Serialize());
    return false;
}

bool RendererCallAllowed(uint32_t windowId, const std::string& module,
                         const std::string& fn, std::string& body) {
    std::string error;
    if (GuardRendererCall(windowId, module, fn, error)) return true;
    json::Object refusal;
    refusal.emplace_back("message", json::Value(error));
    body = "{\"ok\":false,\"r\":" + json::Value(std::move(refusal)).Serialize() + "}";
    return false;
}

} // namespace ow
