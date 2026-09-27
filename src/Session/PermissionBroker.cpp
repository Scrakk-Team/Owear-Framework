// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/PermissionBroker.cpp
//
#include "PermissionBroker.hpp"

#include "../Control/ControlServer.hpp"
#include "ow/Json.h"

namespace ow {

PermissionBroker& PermissionBroker::Get() {
    static PermissionBroker instance;
    return instance;
}

void PermissionBroker::SetEnabled(bool on) { enabled_ = on; }

void PermissionBroker::Request(const std::string& permission, const std::string& origin,
                               ResolveCb cb) {
    if (!enabled_) {
        if (cb) cb(false);
        return;
    }
    const uint64_t id = nextId_++;
    pending_[id] = std::move(cb);

    json::Object p;
    p.emplace_back("id", json::Value(static_cast<int64_t>(id)));
    p.emplace_back("permission", json::Value(permission));
    p.emplace_back("origin", json::Value(origin));
    ControlServer::Get().BroadcastEvent("session.permissionRequest",
                                        json::Value(std::move(p)).Serialize());
}

void PermissionBroker::Resolve(uint64_t id, bool allow) {
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    ResolveCb cb = std::move(it->second);
    pending_.erase(it);
    if (cb) cb(allow);
}

} // namespace ow
