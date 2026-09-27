// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Session/WebRequestBroker.cpp
//
#include "WebRequestBroker.hpp"

#include "../Control/ControlServer.hpp"
#include "ow/Json.h"

#include <regex>

namespace ow {

WebRequestBroker& WebRequestBroker::Get() {
    static WebRequestBroker instance;
    return instance;
}

bool WebRequestBroker::GlobMatch(const std::string& pattern, const std::string& url) {
    if (pattern.empty()) return false;
    if (pattern == "<all_urls>" || pattern == "*") return true;
    std::string re = "^";
    for (char c : pattern) {
        if (c == '*') {
            re += ".*";
        } else if (c == '?') {
            re += ".";
        } else if (std::string(".+^$()[]{}|\\").find(c) != std::string::npos) {
            re += '\\';
            re += c;
        } else {
            re += c;
        }
    }
    re += "$";
    try {
        return std::regex_match(url, std::regex(re));
    } catch (...) {
        return false;
    }
}

void WebRequestBroker::Register(const std::vector<std::string>& patterns) {
    patterns_ = patterns;
    enabled_ = true;
}

void WebRequestBroker::Unregister() {
    enabled_ = false;
    patterns_.clear();
}

bool WebRequestBroker::Matches(const std::string& url) const {
    if (!enabled_) return false;
    if (patterns_.empty()) return true;
    for (const auto& p : patterns_)
        if (GlobMatch(p, url)) return true;
    return false;
}

void WebRequestBroker::BeforeRequest(const std::string& url, const std::string& method,
                                     const std::string& headersJson, Cb cb) {
    if (!Matches(url)) {
        if (cb) cb(Action{});
        return;
    }
    const uint64_t id = nextId_++;
    pending_[id] = std::move(cb);

    json::Object p;
    p.emplace_back("id", json::Value(static_cast<int64_t>(id)));
    p.emplace_back("url", json::Value(url));
    p.emplace_back("method", json::Value(method));
    auto headers = json::Parse(headersJson);
    p.emplace_back("headers", headers.value ? std::move(*headers.value)
                                            : json::Value(json::Object{}));
    ControlServer::Get().BroadcastEvent("webRequest.request",
                                        json::Value(std::move(p)).Serialize());
}

void WebRequestBroker::Resolve(uint64_t id, bool cancel, const std::string& redirectUrl) {
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    Cb cb = std::move(it->second);
    pending_.erase(it);
    Action a;
    a.cancel = cancel;
    a.redirectUrl = redirectUrl;
    if (cb) cb(a);
}

} // namespace ow
