// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/WindowOpenBroker.hpp — control de ventanas emergentes
// (window.open / target=_blank). La app decide allow/deny desde el main.
//
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace ow {

class WindowOpenBroker {
public:
    static WindowOpenBroker& Get();

    void SetEnabled(bool on);
    bool Enabled() const { return enabled_; }

    using ResolveCb = std::function<void(bool allow)>;
    void Request(const std::string& url, ResolveCb cb);
    void Resolve(uint64_t id, bool allow);

private:
    bool enabled_ = false;
    std::map<uint64_t, ResolveCb> pending_;
    uint64_t nextId_ = 1;
};

} // namespace ow
