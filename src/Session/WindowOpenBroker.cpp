// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/WindowOpenBroker.cpp
//
#include "WindowOpenBroker.hpp"

#include "../Control/ControlServer.hpp"
#include "ow/Json.h"

namespace ow {

WindowOpenBroker& WindowOpenBroker::Get() {
    static WindowOpenBroker instance;
    return instance;
}

void WindowOpenBroker::SetEnabled(bool on) { enabled_ = on; }

void WindowOpenBroker::Request(const std::string& url, ResolveCb cb) {
    if (!enabled_) {
        if (cb) cb(true); // sin handler → comportamiento por defecto
        return;
    }
    const uint64_t id = nextId_++;
    pending_[id] = std::move(cb);
    json::Object p;
    p.emplace_back("id", json::Value(static_cast<int64_t>(id)));
    p.emplace_back("url", json::Value(url));
    ControlServer::Get().BroadcastEvent("webContents.windowOpen",
                                        json::Value(std::move(p)).Serialize());
}

void WindowOpenBroker::Resolve(uint64_t id, bool allow) {
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    ResolveCb cb = std::move(it->second);
    pending_.erase(it);
    if (cb) cb(allow);
}

} // namespace ow
